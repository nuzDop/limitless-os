/*
 * NVMe Driver for Continuum Kernel
 * High-performance NVM Express storage driver
 */

#include "nvme.h"
#include "../resonance.h"
#include "../../flux_memory.h"
#include "../../conduit_ipc.h"

// =============================================================================
// NVMe Controller State
// =============================================================================

static nvme_controller_t* g_nvme_controllers[MAX_NVME_CONTROLLERS];
static uint32_t g_nvme_count = 0;
static spinlock_t g_nvme_lock = SPINLOCK_INIT;

// =============================================================================
// NVMe Queue Management
// =============================================================================

static nvme_queue_t* nvme_create_queue(nvme_controller_t* ctrl, 
                                       uint16_t qid, uint16_t size) {
    nvme_queue_t* queue = flux_allocate(NULL, sizeof(nvme_queue_t),
                                        FLUX_ALLOC_KERNEL | FLUX_ALLOC_ZERO);
    if (!queue) {
        return NULL;
    }
    
    queue->qid = qid;
    queue->size = size;
    queue->sq_tail = 0;
    queue->cq_head = 0;
    queue->cq_phase = 1;
    
    // Allocate submission queue (64-byte aligned)
    size_t sq_size = size * sizeof(nvme_command_t);
    queue->sq_dma = resonance_alloc_dma(sq_size, DMA_FLAG_COHERENT);
    if (!queue->sq_dma) {
        flux_free(queue);
        return NULL;
    }
    queue->sq = (nvme_command_t*)queue->sq_dma->virtual_addr;
    
    // Allocate completion queue (16-byte aligned)
    size_t cq_size = size * sizeof(nvme_completion_t);
    queue->cq_dma = resonance_alloc_dma(cq_size, DMA_FLAG_COHERENT);
    if (!queue->cq_dma) {
        resonance_free_dma(queue->sq_dma);
        flux_free(queue);
        return NULL;
    }
    queue->cq = (nvme_completion_t*)queue->cq_dma->virtual_addr;
    
    // Initialize command tracking
    queue->commands = flux_allocate(NULL, size * sizeof(nvme_cmd_info_t),
                                    FLUX_ALLOC_KERNEL | FLUX_ALLOC_ZERO);
    spinlock_init(&queue->lock);
    
    return queue;
}

static void nvme_destroy_queue(nvme_queue_t* queue) {
    if (!queue) {
        return;
    }
    
    if (queue->sq_dma) {
        resonance_free_dma(queue->sq_dma);
    }
    if (queue->cq_dma) {
        resonance_free_dma(queue->cq_dma);
    }
    if (queue->commands) {
        flux_free(queue->commands);
    }
    
    flux_free(queue);
}

// =============================================================================
// NVMe Command Submission
// =============================================================================

static uint16_t nvme_submit_command(nvme_queue_t* queue, nvme_command_t* cmd,
                                    void* completion_context) {
    spinlock_acquire(&queue->lock);
    
    uint16_t tail = queue->sq_tail;
    uint16_t next_tail = (tail + 1) % queue->size;
    
    if (next_tail == queue->cq_head) {
        // Queue full
        spinlock_release(&queue->lock);
        return 0xFFFF;
    }
    
    // Copy command to submission queue
    queue->sq[tail] = *cmd;
    
    // Track command info
    queue->commands[tail].submitted = true;
    queue->commands[tail].completion_context = completion_context;
    queue->commands[tail].submit_time = continuum_get_time();
    
    // Update tail pointer
    queue->sq_tail = next_tail;
    
    // Write tail doorbell
    nvme_controller_t* ctrl = queue->controller;
    if (queue->qid == 0) {
        // Admin queue
        mmio_write32(ctrl->bar0 + NVME_REG_ASQ_TAIL, next_tail);
    } else {
        // I/O queue
        uint32_t doorbell_offset = 0x1000 + (2 * queue->qid * ctrl->doorbell_stride);
        mmio_write32(ctrl->bar0 + doorbell_offset, next_tail);
    }
    
    spinlock_release(&queue->lock);
    
    return tail;
}

static bool nvme_process_completion(nvme_queue_t* queue) {
    bool processed = false;
    
    spinlock_acquire(&queue->lock);
    
    while (1) {
        nvme_completion_t* cqe = &queue->cq[queue->cq_head];
        
        // Check phase bit
        if ((cqe->status & 0x01) != queue->cq_phase) {
            break;
        }
        
        // Process completion
        uint16_t sqid = cqe->sq_id;
        uint16_t sq_head = cqe->sq_head;
        
        if (queue->commands[sqid].submitted) {
            queue->commands[sqid].submitted = false;
            
            // Call completion handler if registered
            if (queue->commands[sqid].completion_context) {
                nvme_completion_handler_t handler = 
                    (nvme_completion_handler_t)queue->commands[sqid].completion_context;
                handler(cqe);
            }
        }
        
        // Advance completion queue head
        queue->cq_head = (queue->cq_head + 1) % queue->size;
        if (queue->cq_head == 0) {
            queue->cq_phase = !queue->cq_phase;
        }
        
        processed = true;
    }
    
    if (processed) {
        // Write completion queue head doorbell
        nvme_controller_t* ctrl = queue->controller;
        if (queue->qid == 0) {
            // Admin queue
            mmio_write32(ctrl->bar0 + NVME_REG_ACQ_HEAD, queue->cq_head);
        } else {
            // I/O queue
            uint32_t doorbell_offset = 0x1000 + ((2 * queue->qid + 1) * ctrl->doorbell_stride);
            mmio_write32(ctrl->bar0 + doorbell_offset, queue->cq_head);
        }
    }
    
    spinlock_release(&queue->lock);
    
    return processed;
}

// =============================================================================
// NVMe Admin Commands
// =============================================================================

static int nvme_identify_controller(nvme_controller_t* ctrl) {
    // Allocate DMA buffer for identify data
    dma_region_t* identify_dma = resonance_alloc_dma(4096, DMA_FLAG_COHERENT);
    if (!identify_dma) {
        return -1;
    }
    
    // Build identify command
    nvme_command_t cmd = {0};
    cmd.opcode = NVME_ADMIN_IDENTIFY;
    cmd.nsid = 0;
    cmd.prp1 = identify_dma->physical_addr;
    cmd.cdw10 = 0x01;  // Controller identify
    
    // Submit command
    nvme_submit_command(ctrl->admin_queue, &cmd, NULL);
    
    // Wait for completion (simplified - should use interrupts)
    uint64_t timeout = continuum_get_time() + 1000000;  // 1 second
    while (continuum_get_time() < timeout) {
        if (nvme_process_completion(ctrl->admin_queue)) {
            break;
        }
    }
    
    // Parse identify data
    nvme_identify_controller_t* identify = 
        (nvme_identify_controller_t*)identify_dma->virtual_addr;
    
    ctrl->vid = identify->vid;
    ctrl->ssvid = identify->ssvid;
    memcpy(ctrl->serial_number, identify->sn, 20);
    memcpy(ctrl->model_number, identify->mn, 40);
    memcpy(ctrl->firmware_rev, identify->fr, 8);
    
    ctrl->max_queue_entries = identify->mqes + 1;
    ctrl->num_namespaces = identify->nn;
    
    resonance_free_dma(identify_dma);
    
    return 0;
}

static int nvme_create_io_queue(nvme_controller_t* ctrl, uint16_t qid) {
    // Create completion queue first
    nvme_command_t cmd = {0};
    cmd.opcode = NVME_ADMIN_CREATE_CQ;
    cmd.prp1 = ctrl->io_queues[qid - 1]->cq_dma->physical_addr;
    cmd.cdw10 = ((ctrl->queue_size - 1) << 16) | qid;
    cmd.cdw11 = 0x01;  // Physically contiguous, interrupts enabled
    
    nvme_submit_command(ctrl->admin_queue, &cmd, NULL);
    
    // Wait for completion
    uint64_t timeout = continuum_get_time() + 1000000;
    while (continuum_get_time() < timeout) {
        if (nvme_process_completion(ctrl->admin_queue)) {
            break;
        }
    }
    
    // Create submission queue
    cmd.opcode = NVME_ADMIN_CREATE_SQ;
    cmd.prp1 = ctrl->io_queues[qid - 1]->sq_dma->physical_addr;
    cmd.cdw10 = ((ctrl->queue_size - 1) << 16) | qid;
    cmd.cdw11 = (qid << 16) | 0x01;  // CQ ID and priorities
    
    nvme_submit_command(ctrl->admin_queue, &cmd, NULL);
    
    // Wait for completion
    timeout = continuum_get_time() + 1000000;
    while (continuum_get_time() < timeout) {
        if (nvme_process_completion(ctrl->admin_queue)) {
            break;
        }
    }
    
    return 0;
}

// =============================================================================
// NVMe I/O Commands
// =============================================================================

static int nvme_read_write(nvme_namespace_t* ns, uint64_t lba, uint32_t count,
                          void* buffer, bool is_write) {
    nvme_controller_t* ctrl = ns->controller;
    nvme_queue_t* queue = ctrl->io_queues[0];  // Use first I/O queue
    
    // Allocate DMA buffer
    size_t size = count * ns->block_size;
    dma_region_t* dma = resonance_alloc_dma(size, DMA_FLAG_COHERENT);
    if (!dma) {
        return -1;
    }
    
    if (is_write) {
        memcpy(dma->virtual_addr, buffer, size);
    }
    
    // Build I/O command
    nvme_command_t cmd = {0};
    cmd.opcode = is_write ? NVME_IO_WRITE : NVME_IO_READ;
    cmd.nsid = ns->nsid;
    cmd.prp1 = dma->physical_addr;
    if (size > 4096) {
        // Need PRP list for larger transfers
        // Simplified - would need proper PRP list setup
        cmd.prp2 = dma->physical_addr + 4096;
    }
    cmd.cdw10 = lba & 0xFFFFFFFF;
    cmd.cdw11 = lba >> 32;
    cmd.cdw12 = (count - 1) & 0xFFFF;
    
    // Submit command
    nvme_submit_command(queue, &cmd, NULL);
    
    // Wait for completion
    uint64_t timeout = continuum_get_time() + 5000000;  // 5 seconds
    while (continuum_get_time() < timeout) {
        if (nvme_process_completion(queue)) {
            break;
        }
    }
    
    if (!is_write) {
        memcpy(buffer, dma->virtual_addr, size);
    }
    
    resonance_free_dma(dma);
    
    return 0;
}

int nvme_read(nvme_namespace_t* ns, uint64_t lba, uint32_t count, void* buffer) {
    return nvme_read_write(ns, lba, count, buffer, false);
}

int nvme_write(nvme_namespace_t* ns, uint64_t lba, uint32_t count, void* buffer) {
    return nvme_read_write(ns, lba, count, buffer, true);
}

// =============================================================================
// NVMe Controller Initialization
// =============================================================================

static int nvme_init_controller(nvme_controller_t* ctrl) {
    // Disable controller
    uint32_t cc = mmio_read32(ctrl->bar0 + NVME_REG_CC);
    cc &= ~NVME_CC_ENABLE;
    mmio_write32(ctrl->bar0 + NVME_REG_CC, cc);
    
    // Wait for controller to be disabled
    uint64_t timeout = continuum_get_time() + 5000000;
    while (continuum_get_time() < timeout) {
        uint32_t csts = mmio_read32(ctrl->bar0 + NVME_REG_CSTS);
        if (!(csts & NVME_CSTS_RDY)) {
            break;
        }
        io_wait();
    }
    
    // Read capabilities
    uint64_t cap = mmio_read64(ctrl->bar0 + NVME_REG_CAP);
    ctrl->max_queue_entries = ((cap >> 0) & 0xFFFF) + 1;
    ctrl->doorbell_stride = 4 << ((cap >> 32) & 0xF);
    
    // Create admin queue
    ctrl->admin_queue = nvme_create_queue(ctrl, 0, 64);
    if (!ctrl->admin_queue) {
        return -1;
    }
    ctrl->admin_queue->controller = ctrl;
    
    // Configure admin queue base addresses
    mmio_write64(ctrl->bar0 + NVME_REG_ASQ, ctrl->admin_queue->sq_dma->physical_addr);
    mmio_write64(ctrl->bar0 + NVME_REG_ACQ, ctrl->admin_queue->cq_dma->physical_addr);
    
    // Configure admin queue attributes
    uint32_t aqa = ((63 << 16) | 63);  // 64 entries each
    mmio_write32(ctrl->bar0 + NVME_REG_AQA, aqa);
    
    // Enable controller
    cc = 0;
    cc |= (0 << 20);  // NVM command set
    cc |= (6 << 16);  // I/O submission queue entry size (2^6 = 64)
    cc |= (4 << 11);  // I/O completion queue entry size (2^4 = 16)
    cc |= (0 << 7);   // Arbitration mechanism
    cc |= (4 << 4);   // Memory page size (2^(12+4) = 64KB)
    cc |= NVME_CC_ENABLE;
    mmio_write32(ctrl->bar0 + NVME_REG_CC, cc);
    
    // Wait for controller ready
    timeout = continuum_get_time() + 5000000;
    while (continuum_get_time() < timeout) {
        uint32_t csts = mmio_read32(ctrl->bar0 + NVME_REG_CSTS);
        if (csts & NVME_CSTS_RDY) {
            break;
        }
        io_wait();
    }
    
    // Identify controller
    if (nvme_identify_controller(ctrl) != 0) {
        return -1;
    }
    
    // Create I/O queues
    ctrl->queue_size = 256;  // Default I/O queue size
    ctrl->num_io_queues = 1;  // Start with one I/O queue
    
    for (uint16_t i = 0; i < ctrl->num_io_queues; i++) {
        ctrl->io_queues[i] = nvme_create_queue(ctrl, i + 1, ctrl->queue_size);
        if (!ctrl->io_queues[i]) {
            return -1;
        }
        ctrl->io_queues[i]->controller = ctrl;
        
        if (nvme_create_io_queue(ctrl, i + 1) != 0) {
            return -1;
        }
    }
    
    // Identify namespaces
    for (uint32_t nsid = 1; nsid <= ctrl->num_namespaces; nsid++) {
        nvme_namespace_t* ns = flux_allocate(NULL, sizeof(nvme_namespace_t),
                                            FLUX_ALLOC_KERNEL | FLUX_ALLOC_ZERO);
        if (!ns) {
            continue;
        }
        
        ns->controller = ctrl;
        ns->nsid = nsid;
        
        // Identify namespace
        dma_region_t* identify_dma = resonance_alloc_dma(4096, DMA_FLAG_COHERENT);
        if (identify_dma) {
            nvme_command_t cmd = {0};
            cmd.opcode = NVME_ADMIN_IDENTIFY;
            cmd.nsid = nsid;
            cmd.prp1 = identify_dma->physical_addr;
            cmd.cdw10 = 0x00;  // Namespace identify
            
            nvme_submit_command(ctrl->admin_queue, &cmd, NULL);
            
            // Wait for completion
            timeout = continuum_get_time() + 1000000;
            while (continuum_get_time() < timeout) {
                if (nvme_process_completion(ctrl->admin_queue)) {
                    break;
                }
            }
            
            nvme_identify_namespace_t* id_ns = 
                (nvme_identify_namespace_t*)identify_dma->virtual_addr;
            
            ns->size = id_ns->nsze;
            ns->capacity = id_ns->ncap;
            ns->utilization = id_ns->nuse;
            
            // Get block size from first LBA format
            uint32_t lbaf = id_ns->flbas & 0xF;
            ns->block_size = 1 << id_ns->lbaf[lbaf].ds;
            
            resonance_free_dma(identify_dma);
        }
        
        ctrl->namespaces[nsid - 1] = ns;
    }
    
    return 0;
}

// =============================================================================
// Driver Interface
// =============================================================================

static void* nvme_probe(device_node_t* node) {
    if (node->class_code != 0x01 || node->subclass_code != 0x08) {
        return NULL;  // Not NVMe
    }
    
    nvme_controller_t* ctrl = flux_allocate(NULL, sizeof(nvme_controller_t),
                                           FLUX_ALLOC_KERNEL | FLUX_ALLOC_ZERO);
    if (!ctrl) {
        return NULL;
    }
    
    // Get PCI resources
    pci_device_info_t* pci_info = (pci_device_info_t*)node->bus_specific_data;
    
    // Map BAR0 (controller registers)
    ctrl->bar0 = (void*)(uintptr_t)(pci_info->bars[0] & ~0xF);
    
    // Initialize controller
    if (nvme_init_controller(ctrl) != 0) {
        flux_free(ctrl);
        return NULL;
    }
    
    // Add to global list
    spinlock_acquire(&g_nvme_lock);
    g_nvme_controllers[g_nvme_count++] = ctrl;
    spinlock_release(&g_nvme_lock);
    
    return ctrl;
}

static int nvme_attach(device_handle_t* handle) {
    nvme_controller_t* ctrl = (nvme_controller_t*)handle->driver_data;
    ctrl->state = NVME_STATE_READY;
    return 0;
}

static void nvme_detach(device_handle_t* handle) {
    nvme_controller_t* ctrl = (nvme_controller_t*)handle->driver_data;
    ctrl->state = NVME_STATE_DISABLED;
    
    // Disable controller
    uint32_t cc = mmio_read32(ctrl->bar0 + NVME_REG_CC);
    cc &= ~NVME_CC_ENABLE;
    mmio_write32(ctrl->bar0 + NVME_REG_CC, cc);
}

static io_result_t nvme_io_request(device_handle_t* handle, io_packet_t* packet) {
    nvme_controller_t* ctrl = (nvme_controller_t*)handle->driver_data;
    
    // Dispatch based on operation
    // Simplified - would handle various I/O operations
    
    return IO_SUCCESS;
}

// Driver registration
static resonance_driver_t nvme_driver = {
    .name = "nvme",
    .vendor_ids = {0x8086, 0x144d, 0x1c5c, 0},  // Intel, Samsung, SK Hynix
    .device_ids = {0},  // Match all devices
    .class_code = 0x01,     // Mass storage
    .subclass_code = 0x08,  // NVMe
    .probe = nvme_probe,
    .attach = nvme_attach,
    .detach = nvme_detach,
    .io_request = nvme_io_request
};

void nvme_init(void) {
    resonance_register_driver(&nvme_driver);
}
