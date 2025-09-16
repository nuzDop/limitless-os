/*
 * Realtek Network Driver for Continuum Kernel
 * Supports RTL8139, RTL8169/8168/8111 series
 */

#include "realtek.h"
#include "../resonance.h"
#include "../../flux_memory.h"

// =============================================================================
// Global Realtek NIC State
// =============================================================================

static realtek_nic_t* g_realtek_nics[MAX_REALTEK_NICS];
static uint32_t g_realtek_nic_count = 0;
static spinlock_t g_realtek_lock = SPINLOCK_INIT;

// Supported device IDs
static const struct {
    uint16_t device_id;
    realtek_chip_t chip_type;
    const char* name;
} realtek_devices[] = {
    {0x8139, CHIP_RTL8139, "RTL8139"},
    {0x8168, CHIP_RTL8168, "RTL8168/8111"},
    {0x8169, CHIP_RTL8169, "RTL8169"},
    {0x8167, CHIP_RTL8169, "RTL8169SC"},
    {0x8136, CHIP_RTL8101, "RTL8101E"},
    {0}
};

// =============================================================================
// Register Access
// =============================================================================

static uint8_t rtl_read8(realtek_nic_t* nic, uint32_t reg) {
    if (nic->use_io) {
        return inb(nic->io_base + reg);
    } else {
        return mmio_read8(nic->mmio_base + reg);
    }
}

static uint16_t rtl_read16(realtek_nic_t* nic, uint32_t reg) {
    if (nic->use_io) {
        return inw(nic->io_base + reg);
    } else {
        return mmio_read16(nic->mmio_base + reg);
    }
}

static uint32_t rtl_read32(realtek_nic_t* nic, uint32_t reg) {
    if (nic->use_io) {
        return inl(nic->io_base + reg);
    } else {
        return mmio_read32(nic->mmio_base + reg);
    }
}

static void rtl_write8(realtek_nic_t* nic, uint32_t reg, uint8_t value) {
    if (nic->use_io) {
        outb(nic->io_base + reg, value);
    } else {
        mmio_write8(nic->mmio_base + reg, value);
    }
}

static void rtl_write16(realtek_nic_t* nic, uint32_t reg, uint16_t value) {
    if (nic->use_io) {
        outw(nic->io_base + reg, value);
    } else {
        mmio_write16(nic->mmio_base + reg, value);
    }
}

static void rtl_write32(realtek_nic_t* nic, uint32_t reg, uint32_t value) {
    if (nic->use_io) {
        outl(nic->io_base + reg, value);
    } else {
        mmio_write32(nic->mmio_base + reg, value);
    }
}

// =============================================================================
// PHY Operations
// =============================================================================

static uint16_t rtl_phy_read(realtek_nic_t* nic, uint8_t reg) {
    rtl_write32(nic, RTL_REG_PHYAR, 0x0 | (reg & 0x1F) << 16);
    
    // Wait for completion
    uint64_t timeout = continuum_get_time() + 1000000;
    while (continuum_get_time() < timeout) {
        if (rtl_read32(nic, RTL_REG_PHYAR) & 0x80000000) {
            return rtl_read32(nic, RTL_REG_PHYAR) >> 16;
        }
        io_wait();
    }
    
    return 0xFFFF;
}

static void rtl_phy_write(realtek_nic_t* nic, uint8_t reg, uint16_t value) {
    rtl_write32(nic, RTL_REG_PHYAR, 0x80000000 | (reg & 0x1F) << 16 | value);
    
    // Wait for completion
    uint64_t timeout = continuum_get_time() + 1000000;
    while (continuum_get_time() < timeout) {
        if (!(rtl_read32(nic, RTL_REG_PHYAR) & 0x80000000)) {
            break;
        }
        io_wait();
    }
}

// =============================================================================
// MAC Address
// =============================================================================

static void rtl_read_mac_address(realtek_nic_t* nic) {
    if (nic->chip_type == CHIP_RTL8139) {
        // RTL8139 stores MAC in IDR registers
        uint32_t mac_low = rtl_read32(nic, RTL_REG_IDR0);
        uint16_t mac_high = rtl_read16(nic, RTL_REG_IDR4);
        
        nic->mac_addr[0] = mac_low & 0xFF;
        nic->mac_addr[1] = (mac_low >> 8) & 0xFF;
        nic->mac_addr[2] = (mac_low >> 16) & 0xFF;
        nic->mac_addr[3] = (mac_low >> 24) & 0xFF;
        nic->mac_addr[4] = mac_high & 0xFF;
        nic->mac_addr[5] = (mac_high >> 8) & 0xFF;
    } else {
        // RTL8169/8168 uses MAC registers
        for (int i = 0; i < 6; i++) {
            nic->mac_addr[i] = rtl_read8(nic, RTL_REG_MAC0 + i);
        }
    }
}

// =============================================================================
// RTL8139 Specific Functions
// =============================================================================

static int rtl8139_init(realtek_nic_t* nic) {
    // Power on
    rtl_write8(nic, RTL_REG_CONFIG1, 0x00);
    
    // Software reset
    rtl_write8(nic, RTL_REG_CR, RTL_CR_RST);
    while (rtl_read8(nic, RTL_REG_CR) & RTL_CR_RST) {
        io_wait();
    }
    
    // Allocate receive buffer (8KB + 16 + 1500)
    nic->rx_buffer_dma = resonance_alloc_dma(8192 + 16 + 1500, DMA_FLAG_COHERENT);
    if (!nic->rx_buffer_dma) {
        return -1;
    }
    nic->rx_buffer = nic->rx_buffer_dma->virtual_addr;
    
    // Set receive buffer
    rtl_write32(nic, RTL_REG_RBSTART, nic->rx_buffer_dma->physical_addr);
    
    // Set IMR + ISR
    rtl_write16(nic, RTL_REG_IMR, RTL_INT_ROK | RTL_INT_TOK);
    
    // Configure receive
    rtl_write32(nic, RTL_REG_RCR, RTL_RCR_AAP | RTL_RCR_APM | RTL_RCR_AM |
                                   RTL_RCR_AB | RTL_RCR_WRAP);
    
    // Configure transmit
    rtl_write32(nic, RTL_REG_TCR, RTL_TCR_IFG_NORMAL | RTL_TCR_MXDMA_2048);
    
    // Enable RX/TX
    rtl_write8(nic, RTL_REG_CR, RTL_CR_RE | RTL_CR_TE);
    
    nic->rx_offset = 0;
    
    return 0;
}

static int rtl8139_send(realtek_nic_t* nic, void* data, size_t length) {
    if (length > 1792) {
        return -1;
    }
    
    spinlock_acquire(&nic->tx_lock);
    
    uint8_t tx_slot = nic->tx_cur % 4;
    
    // Check if slot is free
    uint32_t status = rtl_read32(nic, RTL_REG_TSD0 + tx_slot * 4);
    if (!(status & RTL_TSD_OWN)) {
        spinlock_release(&nic->tx_lock);
        return -1;  // Still in use
    }
    
    // Allocate TX buffer if needed
    if (!nic->tx_buffers[tx_slot]) {
        nic->tx_buffers[tx_slot] = resonance_alloc_dma(2048, DMA_FLAG_COHERENT);
        if (!nic->tx_buffers[tx_slot]) {
            spinlock_release(&nic->tx_lock);
            return -1;
        }
    }
    
    // Copy data
    memcpy(nic->tx_buffers[tx_slot]->virtual_addr, data, length);
    
    // Set buffer address
    rtl_write32(nic, RTL_REG_TSAD0 + tx_slot * 4, 
                nic->tx_buffers[tx_slot]->physical_addr);
    
    // Start transmission
    rtl_write32(nic, RTL_REG_TSD0 + tx_slot * 4, length & 0x1FFF);
    
    nic->tx_cur++;
    
    spinlock_release(&nic->tx_lock);
    return 0;
}

static int rtl8139_receive(realtek_nic_t* nic, void* buffer, size_t max_len) {
    uint16_t status = rtl_read16(nic, RTL_REG_ISR);
    if (!(status & RTL_INT_ROK)) {
        return 0;  // No packet
    }
    
    // Clear interrupt
    rtl_write16(nic, RTL_REG_ISR, RTL_INT_ROK);
    
    uint8_t* rx_ptr = nic->rx_buffer + nic->rx_offset;
    uint16_t rx_status = *(uint16_t*)rx_ptr;
    uint16_t rx_size = *(uint16_t*)(rx_ptr + 2);
    
    if (rx_status & RTL_RX_ROK) {
        // Packet is OK
        rx_size = (rx_size & 0xFFF) - 4;  // Remove CRC
        
        if (rx_size <= max_len) {
            memcpy(buffer, rx_ptr + 4, rx_size);
        }
        
        // Update offset
        nic->rx_offset = (nic->rx_offset + rx_size + 4 + 3) & ~3;
        if (nic->rx_offset > 8192) {
            nic->rx_offset = nic->rx_offset % 8192;
        }
        
        // Update CAPR
        rtl_write16(nic, RTL_REG_CAPR, nic->rx_offset - 16);
        
        return rx_size;
    }
    
    return -1;
}

// =============================================================================
// RTL8169/8168 Specific Functions
// =============================================================================

static int rtl8169_init(realtek_nic_t* nic) {
    // Unlock config registers
    rtl_write8(nic, RTL_REG_9346CR, RTL_9346CR_UNLOCK);
    
    // Software reset
    rtl_write8(nic, RTL_REG_CR, RTL_CR_RST);
    while (rtl_read8(nic, RTL_REG_CR) & RTL_CR_RST) {
        io_wait();
    }
    
    // Set MAC address
    for (int i = 0; i < 6; i++) {
        rtl_write8(nic, RTL_REG_MAC0 + i, nic->mac_addr[i]);
    }
    
    // Initialize descriptor rings
    // RX descriptors
    size_t rx_ring_size = RTL8169_RX_DESC_COUNT * sizeof(rtl8169_desc_t);
    nic->rx_ring_dma = resonance_alloc_dma(rx_ring_size, DMA_FLAG_COHERENT);
    if (!nic->rx_ring_dma) {
        return -1;
    }
    nic->rx_ring = (rtl8169_desc_t*)nic->rx_ring_dma->virtual_addr;
    
    for (int i = 0; i < RTL8169_RX_DESC_COUNT; i++) {
        nic->rx_buffers[i] = resonance_alloc_dma(RTL8169_RX_BUFFER_SIZE, 
                                                 DMA_FLAG_COHERENT);
        if (!nic->rx_buffers[i]) {
            return -1;
        }
        
        nic->rx_ring[i].addr = nic->rx_buffers[i]->physical_addr;
        nic->rx_ring[i].opts1 = RTL8169_DESC_OWN | 
                                (RTL8169_RX_BUFFER_SIZE & 0x3FFF);
        if (i == RTL8169_RX_DESC_COUNT - 1) {
            nic->rx_ring[i].opts1 |= RTL8169_DESC_EOR;
        }
    }
    
    // TX descriptors
    size_t tx_ring_size = RTL8169_TX_DESC_COUNT * sizeof(rtl8169_desc_t);
    nic->tx_ring_dma = resonance_alloc_dma(tx_ring_size, DMA_FLAG_COHERENT);
    if (!nic->tx_ring_dma) {
        return -1;
    }
    nic->tx_ring = (rtl8169_desc_t*)nic->tx_ring_dma->virtual_addr;
    
    for (int i = 0; i < RTL8169_TX_DESC_COUNT; i++) {
        nic->tx_ring[i].opts1 = 0;
        if (i == RTL8169_TX_DESC_COUNT - 1) {
            nic->tx_ring[i].opts1 = RTL8169_DESC_EOR;
        }
    }
    
    // Set descriptor addresses
    rtl_write32(nic, RTL_REG_RDSAR_LO, nic->rx_ring_dma->physical_addr & 0xFFFFFFFF);
    rtl_write32(nic, RTL_REG_RDSAR_HI, nic->rx_ring_dma->physical_addr >> 32);
    rtl_write32(nic, RTL_REG_TNPDS_LO, nic->tx_ring_dma->physical_addr & 0xFFFFFFFF);
    rtl_write32(nic, RTL_REG_TNPDS_HI, nic->tx_ring_dma->physical_addr >> 32);
    
    // Set max RX packet size
    rtl_write16(nic, RTL_REG_RMS, RTL8169_RX_BUFFER_SIZE);
    
    // Configure receive
    rtl_write32(nic, RTL_REG_RCR, RTL_RCR_AAP | RTL_RCR_APM | RTL_RCR_AM |
                                   RTL_RCR_AB | RTL_RCR_RXFTH_NONE | 
                                   RTL_RCR_MXDMA_UNLIMITED);
    
    // Configure transmit
    rtl_write32(nic, RTL_REG_TCR, RTL_TCR_IFG_NORMAL | RTL_TCR_MXDMA_UNLIMITED);
    
    // Enable interrupts
    rtl_write16(nic, RTL_REG_IMR, RTL_INT_ROK | RTL_INT_TOK | RTL_INT_RDU | 
                                  RTL_INT_TDU | RTL_INT_LINKCHG);
    
    // Lock config registers
    rtl_write8(nic, RTL_REG_9346CR, RTL_9346CR_LOCK);
    
    // Enable RX/TX
    rtl_write8(nic, RTL_REG_CR, RTL_CR_RE | RTL_CR_TE);
    
    nic->rx_cur = 0;
    nic->tx_cur = 0;
    
    return 0;
}

static int rtl8169_send(realtek_nic_t* nic, void* data, size_t length) {
    if (length > RTL8169_TX_BUFFER_SIZE) {
        return -1;
    }
    
    spinlock_acquire(&nic->tx_lock);
    
    uint32_t tx_idx = nic->tx_cur % RTL8169_TX_DESC_COUNT;
    rtl8169_desc_t* desc = &nic->tx_ring[tx_idx];
    
    // Check if descriptor is available
    if (desc->opts1 & RTL8169_DESC_OWN) {
        spinlock_release(&nic->tx_lock);
        return -1;
    }
    
    // Allocate buffer if needed
    if (!nic->tx_buffers[tx_idx]) {
        nic->tx_buffers[tx_idx] = resonance_alloc_dma(RTL8169_TX_BUFFER_SIZE,
                                                      DMA_FLAG_COHERENT);
        if (!nic->tx_buffers[tx_idx]) {
            spinlock_release(&nic->tx_lock);
            return -1;
        }
    }
    
    // Copy data
    memcpy(nic->tx_buffers[tx_idx]->virtual_addr, data, length);
    
    // Setup descriptor
    desc->addr = nic->tx_buffers[tx_idx]->physical_addr;
    desc->opts2 = 0;
    desc->opts1 = RTL8169_DESC_OWN | RTL8169_DESC_FS | RTL8169_DESC_LS |
                  (length & 0x3FFF);
    
    if (tx_idx == RTL8169_TX_DESC_COUNT - 1) {
        desc->opts1 |= RTL8169_DESC_EOR;
    }
    
    // Notify hardware
    rtl_write8(nic, RTL_REG_TPPOLL, RTL_TPPOLL_NPQ);
    
    nic->tx_cur++;
    
    spinlock_release(&nic->tx_lock);
    return 0;
}

static int rtl8169_receive(realtek_nic_t* nic, void* buffer, size_t max_len) {
    uint32_t rx_idx = nic->rx_cur % RTL8169_RX_DESC_COUNT;
    rtl8169_desc_t* desc = &nic->rx_ring[rx_idx];
    
    // Check if packet available
    if (desc->opts1 & RTL8169_DESC_OWN) {
        return 0;  // No packet
    }
    
    // Check for errors
    if (desc->opts1 & RTL8169_DESC_RES) {
        // Reset descriptor
        desc->opts1 = RTL8169_DESC_OWN | (RTL8169_RX_BUFFER_SIZE & 0x3FFF);
        if (rx_idx == RTL8169_RX_DESC_COUNT - 1) {
            desc->opts1 |= RTL8169_DESC_EOR;
        }
        nic->rx_cur++;
        return -1;
    }
    
    // Get packet length
    uint16_t length = (desc->opts1 >> 16) & 0x3FFF;
    if (length > max_len) {
        length = max_len;
    }
    
    // Copy data
    memcpy(buffer, nic->rx_buffers[rx_idx]->virtual_addr, length);
    
    // Reset descriptor
    desc->opts1 = RTL8169_DESC_OWN | (RTL8169_RX_BUFFER_SIZE & 0x3FFF);
    if (rx_idx == RTL8169_RX_DESC_COUNT - 1) {
        desc->opts1 |= RTL8169_DESC_EOR;
    }
    
    nic->rx_cur++;
    
    return length;
}

// =============================================================================
// Common Functions
// =============================================================================

int realtek_send_packet(realtek_nic_t* nic, void* data, size_t length) {
    if (nic->chip_type == CHIP_RTL8139) {
        return rtl8139_send(nic, data, length);
    } else {
        return rtl8169_send(nic, data, length);
    }
}

int realtek_receive_packet(realtek_nic_t* nic, void* buffer, size_t max_len) {
    if (nic->chip_type == CHIP_RTL8139) {
        return rtl8139_receive(nic, buffer, max_len);
    } else {
        return rtl8169_receive(nic, buffer, max_len);
    }
}

// =============================================================================
// Driver Interface
// =============================================================================

static void* realtek_probe(device_node_t* node) {
    if (node->vendor_id != 0x10EC) {
        return NULL;  // Not Realtek
    }
    
    // Check if device is supported
    realtek_chip_t chip_type = CHIP_UNKNOWN;
    const char* chip_name = NULL;
    
    for (int i = 0; realtek_devices[i].device_id != 0; i++) {
        if (node->device_id == realtek_devices[i].device_id) {
            chip_type = realtek_devices[i].chip_type;
            chip_name = realtek_devices[i].name;
            break;
        }
    }
    
    if (chip_type == CHIP_UNKNOWN) {
        return NULL;
    }
    
    realtek_nic_t* nic = flux_allocate(NULL, sizeof(realtek_nic_t),
                                       FLUX_ALLOC_KERNEL | FLUX_ALLOC_ZERO);
    if (!nic) {
        return NULL;
    }
    
    nic->chip_type = chip_type;
    
    // Get PCI resources
    pci_device_info_t* pci_info = (pci_device_info_t*)node->bus_specific_data;
    
    // Check for I/O or MMIO
    if (pci_info->bars[0] & 0x01) {
        // I/O space
        nic->use_io = true;
        nic->io_base = pci_info->bars[0] & ~0x03;
    } else {
        // Memory space
        nic->use_io = false;
        nic->mmio_base = (void*)(uintptr_t)(pci_info->bars[0] & ~0x0F);
    }
    
    spinlock_init(&nic->rx_lock);
    spinlock_init(&nic->tx_lock);
    
    // Read MAC address
    rtl_read_mac_address(nic);
    
    // Initialize chip
    int result;
    if (chip_type == CHIP_RTL8139) {
        result = rtl8139_init(nic);
    } else {
        result = rtl8169_init(nic);
    }
    
    if (result != 0) {
        flux_free(nic);
        return NULL;
    }
    
    // Add to global list
    spinlock_acquire(&g_realtek_lock);
    g_realtek_nics[g_realtek_nic_count++] = nic;
    spinlock_release(&g_realtek_lock);
    
    return nic;
}

static int realtek_attach(device_handle_t* handle) {
    realtek_nic_t* nic = (realtek_nic_t*)handle->driver_data;
    nic->state = REALTEK_STATE_UP;
    return 0;
}

static void realtek_detach(device_handle_t* handle) {
    realtek_nic_t* nic = (realtek_nic_t*)handle->driver_data;
    
    // Disable RX/TX
    rtl_write8(nic, RTL_REG_CR, 0);
    
    // Disable interrupts
    rtl_write16(nic, RTL_REG_IMR, 0);
    
    nic->state = REALTEK_STATE_DOWN;
}

// Driver registration
static resonance_driver_t realtek_driver = {
    .name = "realtek-ethernet",
    .vendor_ids = {0x10EC, 0},
    .device_ids = {0},  // Check in probe
    .probe = realtek_probe,
    .attach = realtek_attach,
    .detach = realtek_detach
};

void realtek_init(void) {
    resonance_register_driver(&realtek_driver);
}
