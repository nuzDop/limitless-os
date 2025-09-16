/*
 * xHCI USB 3.0 Host Controller Driver for Continuum Kernel
 * Extensible Host Controller Interface implementation
 */

#include "xhci.h"
#include "../resonance.h"
#include "../../flux_memory.h"

// =============================================================================
// Global xHCI State
// =============================================================================

static xhci_controller_t* g_xhci_controllers[MAX_XHCI_CONTROLLERS];
static uint32_t g_xhci_count = 0;
static spinlock_t g_xhci_lock = SPINLOCK_INIT;

// =============================================================================
// Register Access
// =============================================================================

static uint32_t xhci_read32(xhci_controller_t* xhci, uint32_t offset) {
    return mmio_read32(xhci->cap_regs + offset);
}

static void xhci_write32(xhci_controller_t* xhci, uint32_t offset, uint32_t value) {
    mmio_write32(xhci->cap_regs + offset, value);
}

static uint32_t xhci_op_read32(xhci_controller_t* xhci, uint32_t offset) {
    return mmio_read32(xhci->op_regs + offset);
}

static void xhci_op_write32(xhci_controller_t* xhci, uint32_t offset, uint32_t value) {
    mmio_write32(xhci->op_regs + offset, value);
}

static uint64_t xhci_op_read64(xhci_controller_t* xhci, uint32_t offset) {
    return mmio_read64(xhci->op_regs + offset);
}

static void xhci_op_write64(xhci_controller_t* xhci, uint32_t offset, uint64_t value) {
    mmio_write64(xhci->op_regs + offset, value);
}

// =============================================================================
// Event Ring Management
// =============================================================================

static int xhci_init_event_ring(xhci_controller_t* xhci, uint32_t interrupter) {
    xhci_event_ring_t* er = &xhci->event_rings[interrupter];
    
    // Allocate event ring segment table
    er->erst_dma = resonance_alloc_dma(sizeof(xhci_erst_entry_t), DMA_FLAG_COHERENT);
    if (!er->erst_dma) {
        return -1;
    }
    er->erst = (xhci_erst_entry_t*)er->erst_dma->virtual_addr;
    
    // Allocate event ring
    size_t ring_size = XHCI_EVENT_RING_SIZE * sizeof(xhci_trb_t);
    er->ring_dma = resonance_alloc_dma(ring_size, DMA_FLAG_COHERENT);
    if (!er->ring_dma) {
        resonance_free_dma(er->erst_dma);
        return -1;
    }
    er->ring = (xhci_trb_t*)er->ring_dma->virtual_addr;
    memset(er->ring, 0, ring_size);
    
    // Setup ERST entry
    er->erst->base = er->ring_dma->physical_addr;
    er->erst->size = XHCI_EVENT_RING_SIZE;
    er->erst->reserved = 0;
    
    // Setup runtime registers for this interrupter
    xhci_rt_regs_t* rt = xhci->rt_regs;
    xhci_interrupter_t* ir = &rt->interrupters[interrupter];
    
    // Set ERST size
    ir->erstsz = 1;
    
    // Set ERSTBA (Event Ring Segment Table Base Address)
    ir->erstba = er->erst_dma->physical_addr;
    
    // Set ERDP (Event Ring Dequeue Pointer)
    ir->erdp = er->ring_dma->physical_addr;
    
    er->dequeue = er->ring;
    er->cycle_state = 1;
    
    return 0;
}

// =============================================================================
// Command Ring Management
// =============================================================================

static int xhci_init_command_ring(xhci_controller_t* xhci) {
    // Allocate command ring
    size_t ring_size = XHCI_CMD_RING_SIZE * sizeof(xhci_trb_t);
    xhci->cmd_ring_dma = resonance_alloc_dma(ring_size, DMA_FLAG_COHERENT);
    if (!xhci->cmd_ring_dma) {
        return -1;
    }
    
    xhci->cmd_ring = (xhci_trb_t*)xhci->cmd_ring_dma->virtual_addr;
    memset(xhci->cmd_ring, 0, ring_size);
    
    // Setup link TRB at the end
    xhci_trb_t* link_trb = &xhci->cmd_ring[XHCI_CMD_RING_SIZE - 1];
    link_trb->parameter = xhci->cmd_ring_dma->physical_addr;
    link_trb->status = 0;
    link_trb->control = TRB_TYPE(TRB_LINK) | TRB_TC;
    
    // Set CRCR (Command Ring Control Register)
    uint64_t crcr = xhci->cmd_ring_dma->physical_addr | XHCI_CRCR_RCS;
    xhci_op_write64(xhci, XHCI_OP_CRCR, crcr);
    
    xhci->cmd_enqueue = xhci->cmd_ring;
    xhci->cmd_cycle = 1;
    
    return 0;
}

// =============================================================================
// Device Context Management
// =============================================================================

static int xhci_init_dcbaa(xhci_controller_t* xhci) {
    // Allocate Device Context Base Address Array
    size_t dcbaa_size = (xhci->max_slots + 1) * sizeof(uint64_t);
    xhci->dcbaa_dma = resonance_alloc_dma(dcbaa_size, DMA_FLAG_COHERENT);
    if (!xhci->dcbaa_dma) {
        return -1;
    }
    
    xhci->dcbaa = (uint64_t*)xhci->dcbaa_dma->virtual_addr;
    memset(xhci->dcbaa, 0, dcbaa_size);
    
    // Set DCBAAP (Device Context Base Address Array Pointer)
    xhci_op_write64(xhci, XHCI_OP_DCBAAP, xhci->dcbaa_dma->physical_addr);
    
    return 0;
}

// =============================================================================
// Transfer Ring Management
// =============================================================================

static xhci_transfer_ring_t* xhci_alloc_transfer_ring(uint32_t size) {
    xhci_transfer_ring_t* ring = flux_allocate(NULL, sizeof(xhci_transfer_ring_t),
                                              FLUX_ALLOC_KERNEL | FLUX_ALLOC_ZERO);
    if (!ring) {
        return NULL;
    }
    
    // Allocate ring segments
    size_t ring_bytes = size * sizeof(xhci_trb_t);
    ring->ring_dma = resonance_alloc_dma(ring_bytes, DMA_FLAG_COHERENT);
    if (!ring->ring_dma) {
        flux_free(ring);
        return NULL;
    }
    
    ring->ring = (xhci_trb_t*)ring->ring_dma->virtual_addr;
    memset(ring->ring, 0, ring_bytes);
    
    // Setup link TRB
    xhci_trb_t* link_trb = &ring->ring[size - 1];
    link_trb->parameter = ring->ring_dma->physical_addr;
    link_trb->control = TRB_TYPE(TRB_LINK) | TRB_TC;
    
    ring->size = size;
    ring->enqueue = ring->ring;
    ring->dequeue = ring->ring;
    ring->cycle_state = 1;
    
    return ring;
}

// =============================================================================
// Port Management
// =============================================================================

static void xhci_handle_port_status(xhci_controller_t* xhci, uint32_t port_id) {
    if (port_id > xhci->num_ports) {
        return;
    }
    
    xhci_port_regs_t* port = &xhci->op_regs->ports[port_id - 1];
    uint32_t portsc = port->portsc;
    
    // Clear change bits by writing 1
    port->portsc = portsc;
    
    if (portsc & XHCI_PORTSC_CSC) {
        // Connect status change
        if (portsc & XHCI_PORTSC_CCS) {
            // Device connected
            xhci->ports[port_id - 1].connected = true;
            
            // Reset port
            port->portsc |= XHCI_PORTSC_PR;
        } else {
            // Device disconnected
            xhci->ports[port_id - 1].connected = false;
        }
    }
    
    if (portsc & XHCI_PORTSC_PRC) {
        // Port reset complete
        if (portsc & XHCI_PORTSC_PED) {
            // Port is enabled, device can be addressed
            xhci->ports[port_id - 1].enabled = true;
            
            // Get port speed
            uint32_t speed = (portsc & XHCI_PORTSC_SPEED_MASK) >> XHCI_PORTSC_SPEED_SHIFT;
            xhci->ports[port_id - 1].speed = speed;
        }
    }
}

// =============================================================================
// Command Submission
// =============================================================================

static int xhci_submit_command(xhci_controller_t* xhci, xhci_trb_t* trb) {
    spinlock_acquire(&xhci->cmd_lock);
    
    // Copy TRB to command ring
    *xhci->cmd_enqueue = *trb;
    xhci->cmd_enqueue->control |= xhci->cmd_cycle;
    
    // Advance enqueue pointer
    xhci->cmd_enqueue++;
    if (TRB_TYPE_GET(xhci->cmd_enqueue->control) == TRB_LINK) {
        xhci->cmd_enqueue = xhci->cmd_ring;
        xhci->cmd_cycle ^= 1;
    }
    
    // Ring doorbell
    xhci->db_regs[0] = 0;
    
    spinlock_release(&xhci->cmd_lock);
    
    // Wait for completion (simplified - should use events)
    uint64_t timeout = continuum_get_time() + 1000000;
    while (continuum_get_time() < timeout) {
        // Check event ring for command completion
        // Simplified implementation
        io_wait();
    }
    
    return 0;
}

// =============================================================================
// Device Slot Management
// =============================================================================

static int xhci_enable_slot(xhci_controller_t* xhci, uint8_t* slot_id) {
    xhci_trb_t trb = {0};
    trb.control = TRB_TYPE(TRB_ENABLE_SLOT);
    
    if (xhci_submit_command(xhci, &trb) != 0) {
        return -1;
    }
    
    // Get slot ID from completion event (simplified)
    *slot_id = 1;  // Should get from event
    
    return 0;
}

static int xhci_address_device(xhci_controller_t* xhci, uint8_t slot_id,
                              xhci_input_context_t* input_ctx, bool bsr) {
    xhci_trb_t trb = {0};
    trb.parameter = (uintptr_t)input_ctx;
    trb.control = TRB_TYPE(TRB_ADDRESS_DEVICE) | TRB_SLOT(slot_id);
    
    if (bsr) {
        trb.control |= TRB_BSR;
    }
    
    return xhci_submit_command(xhci, &trb);
}

// =============================================================================
// Controller Reset and Initialization
// =============================================================================

static int xhci_reset(xhci_controller_t* xhci) {
    // Stop the controller
    uint32_t cmd = xhci_op_read32(xhci, XHCI_OP_USBCMD);
    cmd &= ~XHCI_CMD_RUN;
    xhci_op_write32(xhci, XHCI_OP_USBCMD, cmd);
    
    // Wait for HCHalted
    uint64_t timeout = continuum_get_time() + 1000000;
    while (continuum_get_time() < timeout) {
        if (xhci_op_read32(xhci, XHCI_OP_USBSTS) & XHCI_STS_HCH) {
            break;
        }
        io_wait();
    }
    
    // Reset controller
    cmd = xhci_op_read32(xhci, XHCI_OP_USBCMD);
    cmd |= XHCI_CMD_RESET;
    xhci_op_write32(xhci, XHCI_OP_USBCMD, cmd);
    
    // Wait for reset to complete
    timeout = continuum_get_time() + 1000000;
    while (continuum_get_time() < timeout) {
        if (!(xhci_op_read32(xhci, XHCI_OP_USBCMD) & XHCI_CMD_RESET)) {
            break;
        }
        io_wait();
    }
    
    // Wait for CNR (Controller Not Ready) to clear
    timeout = continuum_get_time() + 1000000;
    while (continuum_get_time() < timeout) {
        if (!(xhci_op_read32(xhci, XHCI_OP_USBSTS) & XHCI_STS_CNR)) {
            break;
        }
        io_wait();
    }
    
    return 0;
}

static int xhci_init_controller(xhci_controller_t* xhci) {
    // Read capability registers
    uint32_t hcsparams1 = xhci_read32(xhci, XHCI_CAP_HCSPARAMS1);
    uint32_t hcsparams2 = xhci_read32(xhci, XHCI_CAP_HCSPARAMS2);
    uint32_t hccparams1 = xhci_read32(xhci, XHCI_CAP_HCCPARAMS1);
    
    xhci->max_slots = XHCI_HCS1_MAX_SLOTS(hcsparams1);
    xhci->max_intrs = XHCI_HCS1_MAX_INTRS(hcsparams1);
    xhci->num_ports = XHCI_HCS1_MAX_PORTS(hcsparams1);
    
    // Get operational register offset
    uint8_t caplength = mmio_read8(xhci->cap_regs + XHCI_CAP_CAPLENGTH);
    xhci->op_regs = (xhci_op_regs_t*)(xhci->cap_regs + caplength);
    
    // Get runtime register offset
    uint32_t rtsoff = xhci_read32(xhci, XHCI_CAP_RTSOFF);
    xhci->rt_regs = (xhci_rt_regs_t*)(xhci->cap_regs + (rtsoff & ~0x1F));
    
    // Get doorbell register offset
    uint32_t dboff = xhci_read32(xhci, XHCI_CAP_DBOFF);
    xhci->db_regs = (uint32_t*)(xhci->cap_regs + (dboff & ~0x3));
    
    // Reset controller
    if (xhci_reset(xhci) != 0) {
        return -1;
    }
    
    // Set max device slots
    xhci_op_write32(xhci, XHCI_OP_CONFIG, xhci->max_slots);
    
    // Initialize DCBAA
    if (xhci_init_dcbaa(xhci) != 0) {
        return -1;
    }
    
    // Initialize command ring
    if (xhci_init_command_ring(xhci) != 0) {
        return -1;
    }
    
    // Initialize event ring
    if (xhci_init_event_ring(xhci, 0) != 0) {
        return -1;
    }
    
    // Enable interrupts
    xhci_op_write32(xhci, XHCI_OP_USBCMD,
                    xhci_op_read32(xhci, XHCI_OP_USBCMD) | XHCI_CMD_INTE);
    
    // Start controller
    xhci_op_write32(xhci, XHCI_OP_USBCMD,
                    xhci_op_read32(xhci, XHCI_OP_USBCMD) | XHCI_CMD_RUN);
    
    // Wait for running
    uint64_t timeout = continuum_get_time() + 1000000;
    while (continuum_get_time() < timeout) {
        if (!(xhci_op_read32(xhci, XHCI_OP_USBSTS) & XHCI_STS_HCH)) {
            break;
        }
        io_wait();
    }
    
    return 0;
}

// =============================================================================
// Driver Interface
// =============================================================================

static void* xhci_probe(device_node_t* node) {
    // Check for xHCI controller (class 0x0C, subclass 0x03, interface 0x30)
    if (node->class_code != 0x0C || node->subclass_code != 0x03 ||
        node->interface != 0x30) {
        return NULL;
    }
    
    xhci_controller_t* xhci = flux_allocate(NULL, sizeof(xhci_controller_t),
                                           FLUX_ALLOC_KERNEL | FLUX_ALLOC_ZERO);
    if (!xhci) {
        return NULL;
    }
    
    // Get PCI resources
    pci_device_info_t* pci_info = (pci_device_info_t*)node->bus_specific_data;
    
    // Map capability registers (BAR0)
    xhci->cap_regs = (uint8_t*)(uintptr_t)(pci_info->bars[0] & ~0x0F);
    
    spinlock_init(&xhci->cmd_lock);
    spinlock_init(&xhci->event_lock);
    
    // Initialize controller
    if (xhci_init_controller(xhci) != 0) {
        flux_free(xhci);
        return NULL;
    }
    
    // Add to global list
    spinlock_acquire(&g_xhci_lock);
    g_xhci_controllers[g_xhci_count++] = xhci;
    spinlock_release(&g_xhci_lock);
    
    return xhci;
}

static int xhci_attach(device_handle_t* handle) {
    xhci_controller_t* xhci = (xhci_controller_t*)handle->driver_data;
    xhci->state = XHCI_STATE_RUNNING;
    return 0;
}

static void xhci_detach(device_handle_t* handle) {
    xhci_controller_t* xhci = (xhci_controller_t*)handle->driver_data;
    
    // Stop controller
    uint32_t cmd = xhci_op_read32(xhci, XHCI_OP_USBCMD);
    cmd &= ~XHCI_CMD_RUN;
    xhci_op_write32(xhci, XHCI_OP_USBCMD, cmd);
    
    xhci->state = XHCI_STATE_HALTED;
}

// Driver registration
static resonance_driver_t xhci_driver = {
    .name = "xhci",
    .class_code = 0x0C,
    .subclass_code = 0x03,
    .interface = 0x30,
    .probe = xhci_probe,
    .attach = xhci_attach,
    .detach = xhci_detach
};

void xhci_init(void) {
    resonance_register_driver(&xhci_driver);
}
