/*
 * Intel Network Driver for Continuum Kernel
 * Supports Intel 82540EM, 82545EM, 82546EB, I217, I218, I219 series
 */

#include "intel.h"
#include "../resonance.h"
#include "../../flux_memory.h"

// =============================================================================
// Global Intel NIC State
// =============================================================================

static intel_nic_t* g_intel_nics[MAX_INTEL_NICS];
static uint32_t g_intel_nic_count = 0;
static spinlock_t g_intel_lock = SPINLOCK_INIT;

// Supported device IDs
static const uint16_t intel_device_ids[] = {
    0x100E,  // 82540EM
    0x100F,  // 82545EM
    0x1019,  // 82547EI
    0x101E,  // 82540EP
    0x1026,  // 82545GM
    0x1027,  // 82545GM
    0x1028,  // 82545GM
    0x1075,  // 82547GI
    0x1076,  // 82541GI
    0x1077,  // 82541GI
    0x1078,  // 82541ER
    0x1079,  // 82546GB
    0x107A,  // 82546GB
    0x107B,  // 82546GB
    0x107C,  // 82541PI
    0x10B9,  // 82572EI
    0x1533,  // I210
    0x1539,  // I211
    0x153A,  // I217-LM
    0x153B,  // I217-V
    0x1559,  // I218-V
    0x155A,  // I218-LM
    0x156F,  // I219-LM
    0x1570,  // I219-V
    0
};

// =============================================================================
// Register Access
// =============================================================================

static uint32_t intel_read32(intel_nic_t* nic, uint32_t reg) {
    return mmio_read32(nic->mmio_base + reg);
}

static void intel_write32(intel_nic_t* nic, uint32_t reg, uint32_t value) {
    mmio_write32(nic->mmio_base + reg, value);
}

static void intel_write_flush(intel_nic_t* nic) {
    intel_read32(nic, INTEL_REG_STATUS);
}

// =============================================================================
// EEPROM Operations
// =============================================================================

static uint16_t intel_eeprom_read(intel_nic_t* nic, uint8_t addr) {
    uint32_t eerd = intel_read32(nic, INTEL_REG_EERD);
    
    // Start read
    intel_write32(nic, INTEL_REG_EERD, INTEL_EERD_START | (addr << 8));
    
    // Wait for completion
    uint64_t timeout = continuum_get_time() + 1000000;
    while (continuum_get_time() < timeout) {
        eerd = intel_read32(nic, INTEL_REG_EERD);
        if (eerd & INTEL_EERD_DONE) {
            return (uint16_t)(eerd >> 16);
        }
        io_wait();
    }
    
    return 0xFFFF;
}

static void intel_read_mac_address(intel_nic_t* nic) {
    // Try to read from RAL/RAH first
    uint32_t ral = intel_read32(nic, INTEL_REG_RAL(0));
    uint32_t rah = intel_read32(nic, INTEL_REG_RAH(0));
    
    if (rah & INTEL_RAH_AV) {
        // Valid MAC in registers
        nic->mac_addr[0] = ral & 0xFF;
        nic->mac_addr[1] = (ral >> 8) & 0xFF;
        nic->mac_addr[2] = (ral >> 16) & 0xFF;
        nic->mac_addr[3] = (ral >> 24) & 0xFF;
        nic->mac_addr[4] = rah & 0xFF;
        nic->mac_addr[5] = (rah >> 8) & 0xFF;
    } else {
        // Read from EEPROM
        for (int i = 0; i < 3; i++) {
            uint16_t word = intel_eeprom_read(nic, i);
            nic->mac_addr[i * 2] = word & 0xFF;
            nic->mac_addr[i * 2 + 1] = word >> 8;
        }
    }
}

// =============================================================================
// Receive Descriptor Ring
// =============================================================================

static int intel_init_rx_ring(intel_nic_t* nic) {
    // Allocate descriptor ring
    size_t ring_size = INTEL_RX_DESC_COUNT * sizeof(intel_rx_desc_t);
    nic->rx_ring_dma = resonance_alloc_dma(ring_size, DMA_FLAG_COHERENT);
    if (!nic->rx_ring_dma) {
        return -1;
    }
    
    nic->rx_ring = (intel_rx_desc_t*)nic->rx_ring_dma->virtual_addr;
    memset(nic->rx_ring, 0, ring_size);
    
    // Allocate receive buffers
    for (int i = 0; i < INTEL_RX_DESC_COUNT; i++) {
        nic->rx_buffers[i] = resonance_alloc_dma(INTEL_RX_BUFFER_SIZE,
                                                 DMA_FLAG_COHERENT);
        if (!nic->rx_buffers[i]) {
            // Clean up
            for (int j = 0; j < i; j++) {
                resonance_free_dma(nic->rx_buffers[j]);
            }
            resonance_free_dma(nic->rx_ring_dma);
            return -1;
        }
        
        nic->rx_ring[i].addr = nic->rx_buffers[i]->physical_addr;
        nic->rx_ring[i].status = 0;
    }
    
    // Configure receive registers
    intel_write32(nic, INTEL_REG_RDBAL, nic->rx_ring_dma->physical_addr & 0xFFFFFFFF);
    intel_write32(nic, INTEL_REG_RDBAH, nic->rx_ring_dma->physical_addr >> 32);
    intel_write32(nic, INTEL_REG_RDLEN, ring_size);
    intel_write32(nic, INTEL_REG_RDH, 0);
    intel_write32(nic, INTEL_REG_RDT, INTEL_RX_DESC_COUNT - 1);
    
    // Configure receive control
    uint32_t rctl = INTEL_RCTL_EN |        // Enable receiver
                    INTEL_RCTL_SBP |       // Store bad packets
                    INTEL_RCTL_UPE |       // Unicast promiscuous
                    INTEL_RCTL_MPE |       // Multicast promiscuous
                    INTEL_RCTL_LBM_NO |    // No loopback
                    INTEL_RCTL_RDMTS_HALF | // Rx desc min threshold
                    INTEL_RCTL_BAM |       // Broadcast accept mode
                    INTEL_RCTL_BSIZE_2048; // Buffer size 2KB
    
    intel_write32(nic, INTEL_REG_RCTL, rctl);
    
    nic->rx_cur = 0;
    
    return 0;
}

// =============================================================================
// Transmit Descriptor Ring
// =============================================================================

static int intel_init_tx_ring(intel_nic_t* nic) {
    // Allocate descriptor ring
    size_t ring_size = INTEL_TX_DESC_COUNT * sizeof(intel_tx_desc_t);
    nic->tx_ring_dma = resonance_alloc_dma(ring_size, DMA_FLAG_COHERENT);
    if (!nic->tx_ring_dma) {
        return -1;
    }
    
    nic->tx_ring = (intel_tx_desc_t*)nic->tx_ring_dma->virtual_addr;
    memset(nic->tx_ring, 0, ring_size);
    
    // Allocate transmit buffers
    for (int i = 0; i < INTEL_TX_DESC_COUNT; i++) {
        nic->tx_buffers[i] = resonance_alloc_dma(INTEL_TX_BUFFER_SIZE,
                                                 DMA_FLAG_COHERENT);
        if (!nic->tx_buffers[i]) {
            // Clean up
            for (int j = 0; j < i; j++) {
                resonance_free_dma(nic->tx_buffers[j]);
            }
            resonance_free_dma(nic->tx_ring_dma);
            return -1;
        }
    }
    
    // Configure transmit registers
    intel_write32(nic, INTEL_REG_TDBAL, nic->tx_ring_dma->physical_addr & 0xFFFFFFFF);
    intel_write32(nic, INTEL_REG_TDBAH, nic->tx_ring_dma->physical_addr >> 32);
    intel_write32(nic, INTEL_REG_TDLEN, ring_size);
    intel_write32(nic, INTEL_REG_TDH, 0);
    intel_write32(nic, INTEL_REG_TDT, 0);
    
    // Configure transmit control
    uint32_t tctl = INTEL_TCTL_EN |        // Enable transmitter
                    INTEL_TCTL_PSP |       // Pad short packets
                    (15 << INTEL_TCTL_CT_SHIFT) |  // Collision threshold
                    (64 << INTEL_TCTL_COLD_SHIFT); // Collision distance
    
    intel_write32(nic, INTEL_REG_TCTL, tctl);
    
    // Configure transmit IPG
    intel_write32(nic, INTEL_REG_TIPG, 0x0060200A);
    
    nic->tx_cur = 0;
    
    return 0;
}

// =============================================================================
// Packet Transmission
// =============================================================================

int intel_send_packet(intel_nic_t* nic, void* data, size_t length) {
    if (!nic || !data || length > INTEL_TX_BUFFER_SIZE) {
        return -1;
    }
    
    spinlock_acquire(&nic->tx_lock);
    
    uint32_t tail = nic->tx_cur;
    intel_tx_desc_t* desc = &nic->tx_ring[tail];
    
    // Check if descriptor is still in use
    if (!(desc->status & INTEL_TX_STATUS_DD)) {
        spinlock_release(&nic->tx_lock);
        return -1;  // Ring full
    }
    
    // Copy packet to DMA buffer
    memcpy(nic->tx_buffers[tail]->virtual_addr, data, length);
    
    // Setup descriptor
    desc->addr = nic->tx_buffers[tail]->physical_addr;
    desc->length = length;
    desc->cso = 0;
    desc->cmd = INTEL_TX_CMD_EOP | INTEL_TX_CMD_IFCS | INTEL_TX_CMD_RS;
    desc->status = 0;
    desc->css = 0;
    desc->special = 0;
    
    // Update tail pointer
    nic->tx_cur = (tail + 1) % INTEL_TX_DESC_COUNT;
    intel_write32(nic, INTEL_REG_TDT, nic->tx_cur);
    
    nic->stats.tx_packets++;
    nic->stats.tx_bytes += length;
    
    spinlock_release(&nic->tx_lock);
    
    return 0;
}

// =============================================================================
// Packet Reception
// =============================================================================

int intel_receive_packet(intel_nic_t* nic, void* buffer, size_t max_len) {
    if (!nic || !buffer) {
        return -1;
    }
    
    spinlock_acquire(&nic->rx_lock);
    
    uint32_t cur = nic->rx_cur;
    intel_rx_desc_t* desc = &nic->rx_ring[cur];
    
    // Check if packet available
    if (!(desc->status & INTEL_RX_STATUS_DD)) {
        spinlock_release(&nic->rx_lock);
        return 0;  // No packet
    }
    
    // Check for errors
    if (desc->errors) {
        // Handle error
        desc->status = 0;
        nic->rx_cur = (cur + 1) % INTEL_RX_DESC_COUNT;
        intel_write32(nic, INTEL_REG_RDT, cur);
        nic->stats.rx_errors++;
        spinlock_release(&nic->rx_lock);
        return -1;
    }
    
    // Get packet length
    uint16_t length = desc->length;
    if (length > max_len) {
        length = max_len;
    }
    
    // Copy packet data
    memcpy(buffer, nic->rx_buffers[cur]->virtual_addr, length);
    
    // Reset descriptor
    desc->status = 0;
    
    // Update tail pointer
    nic->rx_cur = (cur + 1) % INTEL_RX_DESC_COUNT;
    intel_write32(nic, INTEL_REG_RDT, cur);
    
    nic->stats.rx_packets++;
    nic->stats.rx_bytes += length;
    
    spinlock_release(&nic->rx_lock);
    
    return length;
}

// =============================================================================
// Link Management
// =============================================================================

static void intel_check_link(intel_nic_t* nic) {
    uint32_t status = intel_read32(nic, INTEL_REG_STATUS);
    
    nic->link_up = (status & INTEL_STATUS_LU) ? true : false;
    
    if (nic->link_up) {
        // Get link speed
        uint32_t speed_bits = (status & INTEL_STATUS_SPEED_MASK) >> 6;
        switch (speed_bits) {
            case 0:
                nic->link_speed = 10;
                break;
            case 1:
                nic->link_speed = 100;
                break;
            case 2:
            case 3:
                nic->link_speed = 1000;
                break;
        }
        
        nic->full_duplex = (status & INTEL_STATUS_FD) ? true : false;
    }
}

// =============================================================================
// Device Initialization
// =============================================================================

static int intel_init_device(intel_nic_t* nic) {
    // Reset device
    intel_write32(nic, INTEL_REG_CTRL, INTEL_CTRL_RST);
    
    // Wait for reset to complete
    uint64_t timeout = continuum_get_time() + 1000000;
    while (continuum_get_time() < timeout) {
        if (!(intel_read32(nic, INTEL_REG_CTRL) & INTEL_CTRL_RST)) {
            break;
        }
        io_wait();
    }
    
    // Disable interrupts during init
    intel_write32(nic, INTEL_REG_IMC, 0xFFFFFFFF);
    
    // Read MAC address
    intel_read_mac_address(nic);
    
    // Initialize descriptor rings
    if (intel_init_rx_ring(nic) != 0) {
        return -1;
    }
    
    if (intel_init_tx_ring(nic) != 0) {
        // Clean up RX ring
        for (int i = 0; i < INTEL_RX_DESC_COUNT; i++) {
            if (nic->rx_buffers[i]) {
                resonance_free_dma(nic->rx_buffers[i]);
            }
        }
        resonance_free_dma(nic->rx_ring_dma);
        return -1;
    }
    
    // Configure link
    uint32_t ctrl = intel_read32(nic, INTEL_REG_CTRL);
    ctrl |= INTEL_CTRL_SLU;  // Set link up
    ctrl &= ~INTEL_CTRL_LRST;  // Clear link reset
    ctrl &= ~INTEL_CTRL_ILOS;  // Clear ILOS
    intel_write32(nic, INTEL_REG_CTRL, ctrl);
    
    // Enable interrupts
    intel_write32(nic, INTEL_REG_IMS, INTEL_INT_RXT0 | INTEL_INT_RXDMT0 |
                                      INTEL_INT_RXO | INTEL_INT_LSC);
    
    // Check link status
    intel_check_link(nic);
    
    return 0;
}

// =============================================================================
// Driver Interface
// =============================================================================

static void* intel_probe(device_node_t* node) {
    if (node->vendor_id != 0x8086) {
        return NULL;  // Not Intel
    }
    
    // Check if device ID is supported
    bool supported = false;
    for (int i = 0; intel_device_ids[i] != 0; i++) {
        if (node->device_id == intel_device_ids[i]) {
            supported = true;
            break;
        }
    }
    
    if (!supported) {
        return NULL;
    }
    
    intel_nic_t* nic = flux_allocate(NULL, sizeof(intel_nic_t),
                                    FLUX_ALLOC_KERNEL | FLUX_ALLOC_ZERO);
    if (!nic) {
        return NULL;
    }
    
    // Get PCI resources
    pci_device_info_t* pci_info = (pci_device_info_t*)node->bus_specific_data;
    
    // Map MMIO registers (BAR0)
    nic->mmio_base = (void*)(uintptr_t)(pci_info->bars[0] & ~0x0F);
    
    spinlock_init(&nic->rx_lock);
    spinlock_init(&nic->tx_lock);
    
    // Initialize device
    if (intel_init_device(nic) != 0) {
        flux_free(nic);
        return NULL;
    }
    
    // Add to global list
    spinlock_acquire(&g_intel_lock);
    g_intel_nics[g_intel_nic_count++] = nic;
    spinlock_release(&g_intel_lock);
    
    return nic;
}

static int intel_attach(device_handle_t* handle) {
    intel_nic_t* nic = (intel_nic_t*)handle->driver_data;
    nic->state = INTEL_STATE_UP;
    return 0;
}

static void intel_detach(device_handle_t* handle) {
    intel_nic_t* nic = (intel_nic_t*)handle->driver_data;
    
    // Disable device
    intel_write32(nic, INTEL_REG_RCTL, 0);
    intel_write32(nic, INTEL_REG_TCTL, 0);
    intel_write32(nic, INTEL_REG_IMC, 0xFFFFFFFF);
    
    nic->state = INTEL_STATE_DOWN;
}

// Driver registration
static resonance_driver_t intel_driver = {
    .name = "intel-ethernet",
    .vendor_ids = {0x8086, 0},
    .device_ids = {0},  // Check in probe
    .probe = intel_probe,
    .attach = intel_attach,
    .detach = intel_detach
};

void intel_nic_init(void) {
    resonance_register_driver(&intel_driver);
}
