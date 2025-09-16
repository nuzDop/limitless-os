/*
 * VirtIO Block Driver for Continuum Kernel
 * Virtual I/O block device driver for virtualized environments
 */

#include "virtio_block.h"
#include "../resonance.h"
#include "../../flux_memory.h"

// =============================================================================
// Global VirtIO Block State
// =============================================================================

static virtio_blk_device_t* g_virtio_blk_devices[MAX_VIRTIO_BLK_DEVICES];
static uint32_t g_virtio_blk_count = 0;
static spinlock_t g_virtio_blk_lock = SPINLOCK_INIT;

// =============================================================================
// VirtIO Configuration Space Access
// =============================================================================

static uint8_t virtio_read8(virtio_blk_device_t* dev, uint32_t offset) {
    if (dev->common_cfg) {
        return mmio_read8(dev->common_cfg + offset);
    } else {
        return inb(dev->io_base + offset);
    }
}

static uint16_t virtio_read16(virtio_blk_device_t* dev, uint32_t offset) {
    if (dev->common_cfg) {
        return mmio_read16(dev->common_cfg + offset);
    } else {
        return inw(dev->io_base + offset);
    }
}

static uint32_t virtio_read32(virtio_blk_device_t* dev, uint32_t offset) {
    if (dev->common_cfg) {
        return mmio_read32(dev->common_cfg + offset);
    } else {
        return inl(dev->io_base + offset);
    }
}

static void virtio_write8(virtio_blk_device_t* dev, uint32_t offset, uint8_t value) {
    if (dev->common_cfg) {
        mmio_write8(dev->common_cfg + offset, value);
    } else {
        outb(dev->io_base + offset, value);
    }
}

static void virtio_write16(virtio_blk_device_t* dev, uint32_t offset, uint16_t value) {
    if (dev->common_cfg) {
        mmio_write16(dev->common_cfg + offset, value);
    } else {
        outw(dev->io_base + offset, value);
    }
}

static void virtio_write32(virtio_blk_device_t* dev, uint32_t offset, uint32_t value) {
    if (dev->common_cfg) {
        mmio_write32(dev->common_cfg + offset, value);
    } else {
        outl(dev->io_base + offset, value);
    }
}

// =============================================================================
// VirtQueue Management
// =============================================================================

static virtqueue_t* virtqueue_create(virtio_blk_device_t* dev, uint16_t queue_idx,
                                     uint16_t queue_size) {
    virtqueue_t* vq = flux_allocate(NULL, sizeof(virtqueue_t),
                                   FLUX_ALLOC_KERNEL | FLUX_ALLOC_ZERO);
    if (!vq) {
        return NULL;
    }
    
    vq->queue_idx = queue_idx;
    vq->queue_size = queue_size;
    vq->last_used_idx = 0;
    vq->last_avail_idx = 0;
    vq->device = dev;
    
    // Calculate memory requirements
    size_t desc_size = queue_size * sizeof(virtq_desc_t);
    size_t avail_size = sizeof(virtq_avail_t) + queue_size * sizeof(uint16_t);
    size_t used_size = sizeof(virtq_used_t) + queue_size * sizeof(virtq_used_elem_t);
    size_t total_size = desc_size + avail_size + used_size;
    
    // Allocate queue memory (must be physically contiguous)
    vq->queue_dma = resonance_alloc_dma(total_size, DMA_FLAG_COHERENT);
    if (!vq->queue_dma) {
        flux_free(vq);
        return NULL;
    }
    
    // Setup queue pointers
    vq->desc = (virtq_desc_t*)vq->queue_dma->virtual_addr;
    vq->avail = (virtq_avail_t*)((uint8_t*)vq->desc + desc_size);
    vq->used = (virtq_used_t*)((uint8_t*)vq->avail + avail_size);
    
    // Clear queue memory
    memset(vq->desc, 0, desc_size);
    memset(vq->avail, 0, avail_size);
    memset(vq->used, 0, used_size);
    
    // Initialize free descriptor list
    vq->free_head = 0;
    for (uint16_t i = 0; i < queue_size - 1; i++) {
        vq->desc[i].next = i + 1;
    }
    vq->desc[queue_size - 1].next = 0xFFFF;  // End marker
    
    spinlock_init(&vq->lock);
    
    return vq;
}

static void virtqueue_destroy(virtqueue_t* vq) {
    if (!vq) {
        return;
    }
    
    if (vq->queue_dma) {
        resonance_free_dma(vq->queue_dma);
    }
    
    flux_free(vq);
}

static uint16_t virtqueue_alloc_desc(virtqueue_t* vq) {
    if (vq->free_head == 0xFFFF) {
        return 0xFFFF;  // No free descriptors
    }
    
    uint16_t desc_idx = vq->free_head;
    vq->free_head = vq->desc[desc_idx].next;
    return desc_idx;
}

static void virtqueue_free_desc(virtqueue_t* vq, uint16_t desc_idx) {
    vq->desc[desc_idx].next = vq->free_head;
    vq->free_head = desc_idx;
}

static int virtqueue_add_buffer(virtqueue_t* vq, virtio_blk_request_t* req) {
    spinlock_acquire(&vq->lock);
    
    // Allocate descriptors for request header, data, and status
    uint16_t head_desc = virtqueue_alloc_desc(vq);
    uint16_t data_desc = virtqueue_alloc_desc(vq);
    uint16_t status_desc = virtqueue_alloc_desc(vq);
    
    if (head_desc == 0xFFFF || data_desc == 0xFFFF || status_desc == 0xFFFF) {
        // Free any allocated descriptors
        if (head_desc != 0xFFFF) virtqueue_free_desc(vq, head_desc);
        if (data_desc != 0xFFFF) virtqueue_free_desc(vq, data_desc);
        if (status_desc != 0xFFFF) virtqueue_free_desc(vq, status_desc);
        spinlock_release(&vq->lock);
        return -1;
    }
    
    // Setup request header descriptor
    vq->desc[head_desc].addr = req->header_phys;
    vq->desc[head_desc].len = sizeof(virtio_blk_req_header_t);
    vq->desc[head_desc].flags = VIRTQ_DESC_F_NEXT;
    vq->desc[head_desc].next = data_desc;
    
    // Setup data descriptor
    vq->desc[data_desc].addr = req->data_phys;
    vq->desc[data_desc].len = req->data_len;
    vq->desc[data_desc].flags = VIRTQ_DESC_F_NEXT;
    if (req->header->type == VIRTIO_BLK_T_IN) {
        vq->desc[data_desc].flags |= VIRTQ_DESC_F_WRITE;  // Device writes to buffer
    }
    vq->desc[data_desc].next = status_desc;
    
    // Setup status descriptor
    vq->desc[status_desc].addr = req->status_phys;
    vq->desc[status_desc].len = 1;
    vq->desc[status_desc].flags = VIRTQ_DESC_F_WRITE;  // Device writes status
    
    // Add to available ring
    uint16_t avail_idx = vq->avail->idx % vq->queue_size;
    vq->avail->ring[avail_idx] = head_desc;
    __sync_synchronize();  // Memory barrier
    vq->avail->idx++;
    
    // Store request for completion handling
    vq->requests[head_desc] = req;
    
    spinlock_release(&vq->lock);
    
    // Notify device
    if (vq->device->common_cfg) {
        mmio_write16(vq->device->common_cfg + VIRTIO_PCI_QUEUE_NOTIFY, vq->queue_idx);
    } else {
        outw(vq->device->io_base + VIRTIO_PCI_QUEUE_NOTIFY, vq->queue_idx);
    }
    
    return 0;
}

static virtio_blk_request_t* virtqueue_get_completed(virtqueue_t* vq) {
    spinlock_acquire(&vq->lock);
    
    if (vq->last_used_idx == vq->used->idx) {
        spinlock_release(&vq->lock);
        return NULL;  // No completed requests
    }
    
    // Get completed descriptor
    uint16_t used_idx = vq->last_used_idx % vq->queue_size;
    uint32_t desc_idx = vq->used->ring[used_idx].id;
    
    // Get request
    virtio_blk_request_t* req = vq->requests[desc_idx];
    vq->requests[desc_idx] = NULL;
    
    // Free descriptor chain
    uint16_t current = desc_idx;
    while (current != 0xFFFF) {
        uint16_t next = (vq->desc[current].flags & VIRTQ_DESC_F_NEXT) ?
                       vq->desc[current].next : 0xFFFF;
        virtqueue_free_desc(vq, current);
        current = next;
    }
    
    vq->last_used_idx++;
    
    spinlock_release(&vq->lock);
    
    return req;
}

// =============================================================================
// VirtIO Block Operations
// =============================================================================

static int virtio_blk_do_request(virtio_blk_device_t* dev, uint32_t type,
                                 uint64_t sector, void* buffer, size_t size) {
    // Allocate request structure
    virtio_blk_request_t* req = flux_allocate(NULL, sizeof(virtio_blk_request_t),
                                              FLUX_ALLOC_KERNEL | FLUX_ALLOC_ZERO);
    if (!req) {
        return -1;
    }
    
    // Allocate DMA regions
    req->header_dma = resonance_alloc_dma(sizeof(virtio_blk_req_header_t),
                                          DMA_FLAG_COHERENT);
    req->data_dma = resonance_alloc_dma(size, DMA_FLAG_COHERENT);
    req->status_dma = resonance_alloc_dma(1, DMA_FLAG_COHERENT);
    
    if (!req->header_dma || !req->data_dma || !req->status_dma) {
        if (req->header_dma) resonance_free_dma(req->header_dma);
        if (req->data_dma) resonance_free_dma(req->data_dma);
        if (req->status_dma) resonance_free_dma(req->status_dma);
        flux_free(req);
        return -1;
    }
    
    // Setup request header
    req->header = (virtio_blk_req_header_t*)req->header_dma->virtual_addr;
    req->header->type = type;
    req->header->reserved = 0;
    req->header->sector = sector;
    req->header_phys = req->header_dma->physical_addr;
    
    // Setup data buffer
    req->data = req->data_dma->virtual_addr;
    req->data_phys = req->data_dma->physical_addr;
    req->data_len = size;
    
    if (type == VIRTIO_BLK_T_OUT) {
        memcpy(req->data, buffer, size);
    }
    
    // Setup status
    req->status = (uint8_t*)req->status_dma->virtual_addr;
    req->status_phys = req->status_dma->physical_addr;
    *req->status = 0xFF;  // Initial status
    
    // Submit request
    if (virtqueue_add_buffer(dev->vq, req) != 0) {
        resonance_free_dma(req->header_dma);
        resonance_free_dma(req->data_dma);
        resonance_free_dma(req->status_dma);
        flux_free(req);
        return -1;
    }
    
    // Wait for completion (simplified - should use interrupts)
    uint64_t timeout = continuum_get_time() + 5000000;  // 5 seconds
    while (continuum_get_time() < timeout) {
        virtio_blk_request_t* completed = virtqueue_get_completed(dev->vq);
        if (completed == req) {
            // Check status
            uint8_t status = *req->status;
            
            // Copy data if read
            if (type == VIRTIO_BLK_T_IN && status == VIRTIO_BLK_S_OK) {
                memcpy(buffer, req->data, size);
            }
            
            // Free resources
            resonance_free_dma(req->header_dma);
            resonance_free_dma(req->data_dma);
            resonance_free_dma(req->status_dma);
            flux_free(req);
            
            return (status == VIRTIO_BLK_S_OK) ? 0 : -1;
        }
    }
    
    // Timeout
    return -1;
}

int virtio_blk_read(virtio_blk_device_t* dev, uint64_t sector,
                   uint32_t count, void* buffer) {
    return virtio_blk_do_request(dev, VIRTIO_BLK_T_IN, sector,
                                 buffer, count * 512);
}

int virtio_blk_write(virtio_blk_device_t* dev, uint64_t sector,
                    uint32_t count, void* buffer) {
    return virtio_blk_do_request(dev, VIRTIO_BLK_T_OUT, sector,
                                 buffer, count * 512);
}

int virtio_blk_flush(virtio_blk_device_t* dev) {
    return virtio_blk_do_request(dev, VIRTIO_BLK_T_FLUSH, 0, NULL, 0);
}

// =============================================================================
// VirtIO Block Device Initialization
// =============================================================================

static int virtio_blk_negotiate_features(virtio_blk_device_t* dev) {
    // Read device features
    dev->device_features = virtio_read32(dev, VIRTIO_PCI_DEVICE_FEATURES);
    
    // Select features we support
    dev->driver_features = 0;
    
    if (dev->device_features & VIRTIO_BLK_F_SIZE_MAX) {
        dev->driver_features |= VIRTIO_BLK_F_SIZE_MAX;
    }
    if (dev->device_features & VIRTIO_BLK_F_SEG_MAX) {
        dev->driver_features |= VIRTIO_BLK_F_SEG_MAX;
    }
    if (dev->device_features & VIRTIO_BLK_F_GEOMETRY) {
        dev->driver_features |= VIRTIO_BLK_F_GEOMETRY;
    }
    if (dev->device_features & VIRTIO_BLK_F_RO) {
        dev->driver_features |= VIRTIO_BLK_F_RO;
        dev->readonly = true;
    }
    if (dev->device_features & VIRTIO_BLK_F_BLK_SIZE) {
        dev->driver_features |= VIRTIO_BLK_F_BLK_SIZE;
    }
    if (dev->device_features & VIRTIO_BLK_F_FLUSH) {
        dev->driver_features |= VIRTIO_BLK_F_FLUSH;
    }
    
    // Write accepted features
    virtio_write32(dev, VIRTIO_PCI_DRIVER_FEATURES, dev->driver_features);
    
    return 0;
}

static int virtio_blk_read_config(virtio_blk_device_t* dev) {
    // Read capacity (in 512-byte sectors)
    dev->capacity = ((uint64_t)virtio_read32(dev, VIRTIO_BLK_CFG_CAPACITY + 4) << 32) |
                    virtio_read32(dev, VIRTIO_BLK_CFG_CAPACITY);
    
    // Read block size if supported
    if (dev->driver_features & VIRTIO_BLK_F_BLK_SIZE) {
        dev->block_size = virtio_read32(dev, VIRTIO_BLK_CFG_BLK_SIZE);
    } else {
        dev->block_size = 512;  // Default
    }
    
    // Read geometry if supported
    if (dev->driver_features & VIRTIO_BLK_F_GEOMETRY) {
        dev->geometry.cylinders = virtio_read16(dev, VIRTIO_BLK_CFG_GEOMETRY);
        dev->geometry.heads = virtio_read8(dev, VIRTIO_BLK_CFG_GEOMETRY + 2);
        dev->geometry.sectors = virtio_read8(dev, VIRTIO_BLK_CFG_GEOMETRY + 3);
    }
    
    return 0;
}

static int virtio_blk_init_device(virtio_blk_device_t* dev) {
    // Reset device
    virtio_write8(dev, VIRTIO_PCI_STATUS, 0);
    
    // Set ACKNOWLEDGE status bit
    virtio_write8(dev, VIRTIO_PCI_STATUS, VIRTIO_STATUS_ACKNOWLEDGE);
    
    // Set DRIVER status bit
    virtio_write8(dev, VIRTIO_PCI_STATUS, VIRTIO_STATUS_ACKNOWLEDGE |
                                          VIRTIO_STATUS_DRIVER);
    
    // Negotiate features
    if (virtio_blk_negotiate_features(dev) != 0) {
        return -1;
    }
    
    // Set FEATURES_OK status bit
    virtio_write8(dev, VIRTIO_PCI_STATUS, VIRTIO_STATUS_ACKNOWLEDGE |
                                          VIRTIO_STATUS_DRIVER |
                                          VIRTIO_STATUS_FEATURES_OK);
    
    // Re-read status to ensure FEATURES_OK is still set
    uint8_t status = virtio_read8(dev, VIRTIO_PCI_STATUS);
    if (!(status & VIRTIO_STATUS_FEATURES_OK)) {
        return -1;  // Device doesn't support our features
    }
    
    // Setup virtqueue
    uint16_t queue_size = virtio_read16(dev, VIRTIO_PCI_QUEUE_SIZE);
    if (queue_size == 0) {
        return -1;
    }
    
    dev->vq = virtqueue_create(dev, 0, queue_size);
    if (!dev->vq) {
        return -1;
    }
    
    // Configure queue
    virtio_write16(dev, VIRTIO_PCI_QUEUE_SEL, 0);
    virtio_write16(dev, VIRTIO_PCI_QUEUE_SIZE, queue_size);
    virtio_write32(dev, VIRTIO_PCI_QUEUE_PFN,
                   dev->vq->queue_dma->physical_addr >> 12);
    
    // Read device configuration
    if (virtio_blk_read_config(dev) != 0) {
        virtqueue_destroy(dev->vq);
        return -1;
    }
    
    // Set DRIVER_OK status bit
    virtio_write8(dev, VIRTIO_PCI_STATUS, VIRTIO_STATUS_ACKNOWLEDGE |
                                          VIRTIO_STATUS_DRIVER |
                                          VIRTIO_STATUS_FEATURES_OK |
                                          VIRTIO_STATUS_DRIVER_OK);
    
    return 0;
}

// =============================================================================
// Driver Interface
// =============================================================================

static void* virtio_blk_probe(device_node_t* node) {
    // Check for VirtIO block device (device ID 0x1001)
    if (node->vendor_id != 0x1AF4 || node->device_id != 0x1001) {
        return NULL;
    }
    
    virtio_blk_device_t* dev = flux_allocate(NULL, sizeof(virtio_blk_device_t),
                                            FLUX_ALLOC_KERNEL | FLUX_ALLOC_ZERO);
    if (!dev) {
        return NULL;
    }
    
    // Get PCI resources
    pci_device_info_t* pci_info = (pci_device_info_t*)node->bus_specific_data;
    
    // Check if using legacy I/O or modern MMIO
    if (pci_info->bars[0] & 0x01) {
        // I/O space
        dev->io_base = pci_info->bars[0] & ~0x03;
        dev->common_cfg = NULL;
    } else {
        // Memory space
        dev->common_cfg = (void*)(uintptr_t)(pci_info->bars[0] & ~0x0F);
        dev->io_base = 0;
    }
    
    // Initialize device
    if (virtio_blk_init_device(dev) != 0) {
        flux_free(dev);
        return NULL;
    }
    
    // Add to global list
    spinlock_acquire(&g_virtio_blk_lock);
    g_virtio_blk_devices[g_virtio_blk_count++] = dev;
    spinlock_release(&g_virtio_blk_lock);
    
    return dev;
}

static int virtio_blk_attach(device_handle_t* handle) {
    virtio_blk_device_t* dev = (virtio_blk_device_t*)handle->driver_data;
    dev->state = VIRTIO_BLK_STATE_READY;
    return 0;
}

static void virtio_blk_detach(device_handle_t* handle) {
    virtio_blk_device_t* dev = (virtio_blk_device_t*)handle->driver_data;
    
    // Reset device
    virtio_write8(dev, VIRTIO_PCI_STATUS, 0);
    
    // Free virtqueue
    if (dev->vq) {
        virtqueue_destroy(dev->vq);
    }
    
    dev->state = VIRTIO_BLK_STATE_DISABLED;
}

// Driver registration
static resonance_driver_t virtio_blk_driver = {
    .name = "virtio-blk",
    .vendor_ids = {0x1AF4, 0},
    .device_ids = {0x1001, 0},  // VirtIO block device
    .probe = virtio_blk_probe,
    .attach = virtio_blk_attach,
    .detach = virtio_blk_detach
};

void virtio_blk_init(void) {
    resonance_register_driver(&virtio_blk_driver);
}
