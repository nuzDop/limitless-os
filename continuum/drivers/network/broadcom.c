/*
 * Broadcom Network Driver for Continuum Kernel
 * Supports BCM5701, BCM5703, BCM5750, BCM5751, BCM5752, BCM5754, BCM5755
 */

#include "broadcom.h"
#include "../resonance.h"
#include "../../flux_memory.h"

// =============================================================================
// Global Broadcom NIC State
// =============================================================================

static broadcom_nic_t* g_broadcom_nics[MAX_BROADCOM_NICS];
static uint32_t g_broadcom_nic_count = 0;
static spinlock_t g_broadcom_lock = SPINLOCK_INIT;

// Supported devices
static const struct {
    uint16_t device_id;
    const char* name;
} broadcom_devices[] = {
    {0x1644, "BCM5700"},
    {0x1645, "BCM5701"},
    {0x1646, "BCM5702"},
    {0x1647, "BCM5703"},
    {0x1648, "BCM5704"},
    {0x164D, "BCM5702FE"},
    {0x1653, "BCM5705"},
    {0x1654, "BCM5705_2"},
    {0x165D, "BCM5705M"},
    {0x165E, "BCM5705M_2"},
    {0x1676, "BCM5750"},
    {0x1677, "BCM5751"},
    {0x167C, "BCM5750M"},
    {0x167D, "BCM5751M"},
    {0x167E, "BCM5751F"},
    {0x1693, "BCM5787"},
    {0x1694, "BCM5787M"},
    {0x169C, "BCM5788"},
    {0x16A6, "BCM5702X"},
    {0x16A7, "BCM5703X"},
    {0x16A8, "BCM5704S"},
    {0x16C6, "BCM5702A3"},
    {0x16C7, "BCM5703A3"},
    {0x1712, "BCM5714"},
    {0x1713, "BCM5715"},
    {0}
};

// =============================================================================
// Register Access
// =============================================================================

static uint32_t bcm_read32(broadcom_nic_t* nic, uint32_t reg) {
    return mmio_read32(nic->regs + reg);
}

static void bcm_write32(broadcom_nic_t* nic, uint32_t reg, uint32_t value) {
    mmio_write32(nic->regs + reg, value);
}

static void bcm_write_mailbox(broadcom_nic_t* nic, uint32_t reg, uint32_t value) {
    bcm_write32(nic, reg, value);
    bcm_read32(nic, reg);  // Flush
}

static void bcm_set_bits(broadcom_nic_t* nic, uint32_t reg, uint32_t bits) {
    bcm_write32(nic, reg, bcm_read32(nic, reg) | bits);
}

static void bcm_clear_bits(broadcom_nic_t* nic, uint32_t reg, uint32_t bits) {
    bcm_write32(nic, reg, bcm_read32(nic, reg) & ~bits);
}

// =============================================================================
// NVRAM/EEPROM Access
// =============================================================================

static int bcm_nvram_read32(broadcom_nic_t* nic, uint32_t offset, uint32_t* value) {
    // Set address
    bcm_write32(nic, BCM_REG_NVRAM_ADDR, offset);
    
    // Start read
    bcm_write32(nic, BCM_REG_NVRAM_CMD, BCM_NVRAM_CMD_RD | BCM_NVRAM_CMD_START);
    
    // Wait for completion
    uint64_t timeout = continuum_get_time() + 1000000;
    while (continuum_get_time() < timeout) {
        uint32_t cmd = bcm_read32(nic, BCM_REG_NVRAM_CMD);
        if (cmd & BCM_NVRAM_CMD_DONE) {
            *value = bcm_read32(nic, BCM_REG_NVRAM_RDDATA);
            return 0;
        }
        io_wait();
    }
    
    return -1;
}

static void bcm_read_mac_address(broadcom_nic_t* nic) {
    uint32_t mac_high, mac_low;
    
    // Try to read from NVRAM
    if (bcm_nvram_read32(nic, BCM_NVRAM_MAC_ADDR_HIGH, &mac_high) == 0 &&
        bcm_nvram_read32(nic, BCM_NVRAM_MAC_ADDR_LOW, &mac_low) == 0) {
        nic->mac_addr[0] = (mac_high >> 8) & 0xFF;
        nic->mac_addr[1] = mac_high & 0xFF;
        nic->mac_addr[2] = (mac_low >> 24) & 0xFF;
        nic->mac_addr[3] = (mac_low >> 16) & 0xFF;
        nic->mac_addr[4] = (mac_low >> 8) & 0xFF;
        nic->mac_addr[5] = mac_low & 0xFF;
    } else {
        // Read from MAC address registers
        mac_high = bcm_read32(nic, BCM_REG_MAC_ADDR_0_HIGH);
        mac_low = bcm_read32(nic, BCM_REG_MAC_ADDR_0_LOW);
        
        nic->mac_addr[5] = (mac_high >> 8) & 0xFF;
        nic->mac_addr[4] = mac_high & 0xFF;
        nic->mac_addr[3] = (mac_low >> 24) & 0xFF;
        nic->mac_addr[2] = (mac_low >> 16) & 0xFF;
        nic->mac_addr[1] = (mac_low >> 8) & 0xFF;
        nic->mac_addr[0] = mac_low & 0xFF;
    }
}

// =============================================================================
// Ring Buffer Management
// =============================================================================

static int bcm_init_rx_ring(broadcom_nic_t* nic) {
    // Allocate standard RX ring
    size_t ring_size = BCM_RX_RING_SIZE * sizeof(bcm_rx_desc_t);
    nic->rx_std_ring_dma = resonance_alloc_dma(ring_size, DMA_FLAG_COHERENT);
    if (!nic->rx_std_ring_dma) {
        return -1;
    }
    nic->rx_std_ring = (bcm_rx_desc_t*)nic->rx_std_ring_dma->virtual_addr;
    
    // Allocate RX buffers
    for (int i = 0; i < BCM_RX_RING_SIZE; i++) {
        nic->rx_buffers[i] = resonance_alloc_dma(BCM_RX_BUFFER_SIZE, 
                                                 DMA_FLAG_COHERENT);
        if (!nic->rx_buffers[i]) {
            return -1;
        }
        
        nic->rx_std_ring[i].addr_hi = nic->rx_buffers[i]->physical_addr >> 32;
        nic->rx_std_ring[i].addr_lo = nic->rx_buffers[i]->physical_addr & 0xFFFFFFFF;
        nic->rx_std_ring[i].len_flags = BCM_RX_BUFFER_SIZE << 16;
        nic->rx_std_ring[i].type_gen = 0;
        nic->rx_std_ring[i].idx_vlan = i;
        nic->rx_std_ring[i].reserved = 0;
        nic->rx_std_ring[i].opaque = 0;
    }
    
    // Setup RX standard ring control block
    bcm_write32(nic, BCM_REG_RX_STD_RCB_HADDR_HI, 
                nic->rx_std_ring_dma->physical_addr >> 32);
    bcm_write32(nic, BCM_REG_RX_STD_RCB_HADDR_LO,
                nic->rx_std_ring_dma->physical_addr & 0xFFFFFFFF);
    bcm_write32(nic, BCM_REG_RX_STD_RCB_LEN_FLAGS,
                BCM_RX_RING_SIZE << 16 | BCM_RCB_FLAG_USE_EXT_RCV_BD);
    bcm_write32(nic, BCM_REG_RX_STD_RCB_NICADDR, 0x6000);
    
    // Initialize producer index
    bcm_write_mailbox(nic, BCM_REG_RX_STD_PROD_IDX, BCM_RX_RING_SIZE - 1);
    
    nic->rx_std_prod = BCM_RX_RING_SIZE - 1;
    nic->rx_std_cons = 0;
    
    return 0;
}

static int bcm_init_tx_ring(broadcom_nic_t* nic) {
    // Allocate TX ring
    size_t ring_size = BCM_TX_RING_SIZE * sizeof(bcm_tx_desc_t);
    nic->tx_ring_dma = resonance_alloc_dma(ring_size, DMA_FLAG_COHERENT);
    if (!nic->tx_ring_dma) {
        return -1;
    }
    nic->tx_ring = (bcm_tx_desc_t*)nic->tx_ring_dma->virtual_addr;
    memset(nic->tx_ring, 0, ring_size);
    
    // Allocate TX buffers
    for (int i = 0; i < BCM_TX_RING_SIZE; i++) {
        nic->tx_buffers[i] = resonance_alloc_dma(BCM_TX_BUFFER_SIZE,
                                                 DMA_FLAG_COHERENT);
        if (!nic->tx_buffers[i]) {
            return -1;
        }
    }
    
    // Setup TX ring control block
    bcm_write32(nic, BCM_REG_TX_RCB_HADDR_HI,
                nic->tx_ring_dma->physical_addr >> 32);
    bcm_write32(nic, BCM_REG_TX_RCB_HADDR_LO,
                nic->tx_ring_dma->physical_addr & 0xFFFFFFFF);
    bcm_write32(nic, BCM_REG_TX_RCB_LEN_FLAGS,
                BCM_TX_RING_SIZE << 16);
    bcm_write32(nic, BCM_REG_TX_RCB_NICADDR, 0x4000);
    
    nic->tx_prod = 0;
    nic->tx_cons = 0;
    
    return 0;
}

// =============================================================================
// Packet Transmission
// =============================================================================

int broadcom_send_packet(broadcom_nic_t* nic, void* data, size_t length) {
    if (length > BCM_TX_BUFFER_SIZE) {
        return -1;
    }
    
    spinlock_acquire(&nic->tx_lock);
    
    uint32_t prod = nic->tx_prod;
    uint32_t next_prod = (prod + 1) % BCM_TX_RING_SIZE;
    
    if (next_prod == nic->tx_cons) {
        spinlock_release(&nic->tx_lock);
        return -1;  // Ring full
    }
    
    // Copy data to DMA buffer
    memcpy(nic->tx_buffers[prod]->virtual_addr, data, length);
    
    // Setup descriptor
    bcm_tx_desc_t* desc = &nic->tx_ring[prod];
    desc->addr_hi = nic->tx_buffers[prod]->physical_addr >> 32;
    desc->addr_lo = nic->tx_buffers[prod]->physical_addr & 0xFFFFFFFF;
    desc->len_flags = length | BCM_TX_FLAG_PACKET_END;
    desc->vlan_tag = 0;
    
    // Update producer index
    nic->tx_prod = next_prod;
    bcm_write_mailbox(nic, BCM_REG_TX_HOST_PROD_IDX, next_prod);
    
    spinlock_release(&nic->tx_lock);
    return 0;
}

// =============================================================================
// Packet Reception
// =============================================================================

int broadcom_receive_packet(broadcom_nic_t* nic, void* buffer, size_t max_len) {
    uint32_t cons = nic->rx_std_cons;
    uint32_t prod = bcm_read32(nic, BCM_REG_RX_STD_CONS_IDX);
    
    if (cons == prod) {
        return 0;  // No packets
    }
    
    bcm_rx_desc_t* desc = &nic->rx_std_ring[cons];
    uint16_t len = desc->len_flags >> 16;
    
    if (len > max_len) {
        len = max_len;
    }
    
    // Copy packet data
    memcpy(buffer, nic->rx_buffers[cons]->virtual_addr, len);
    
    // Reset descriptor
    desc->len_flags = BCM_RX_BUFFER_SIZE << 16;
    desc->type_gen = 0;
    
    // Update consumer index
    cons = (cons + 1) % BCM_RX_RING_SIZE;
    nic->rx_std_cons = cons;
    
    // Update producer index
    uint32_t new_prod = (nic->rx_std_prod + 1) % BCM_RX_RING_SIZE;
    nic->rx_std_prod = new_prod;
    bcm_write_mailbox(nic, BCM_REG_RX_STD_PROD_IDX, new_prod);
    
    return len;
}

// =============================================================================
// Device Initialization
// =============================================================================

static int bcm_reset_device(broadcom_nic_t* nic) {
    // Set software reset bit
    bcm_write32(nic, BCM_REG_MISC_CFG, BCM_MISC_CFG_RESET);
    
    // Wait for reset to complete
    uint64_t timeout = continuum_get_time() + 1000000;
    while (continuum_get_time() < timeout) {
        if (!(bcm_read32(nic, BCM_REG_MISC_CFG) & BCM_MISC_CFG_RESET)) {
            break;
        }
        io_wait();
    }
    
    // Clear fastboot bit
    bcm_clear_bits(nic, BCM_REG_FASTBOOT_PC, 0xFFFFFFFF);
    
    return 0;
}

static int bcm_init_device(broadcom_nic_t* nic) {
    // Reset device
    if (bcm_reset_device(nic) != 0) {
        return -1;
    }
    
    // Enable memory arbiter
    bcm_set_bits(nic, BCM_REG_MEM_ARBITER_MODE, BCM_MEM_ARBITER_ENABLE);
    
    // Setup buffer manager
    bcm_write32(nic, BCM_REG_BUFFER_MGR_MODE,
                BCM_BUFFER_MGR_ENABLE | BCM_BUFFER_MGR_ATTN_ENABLE);
    
    // Read MAC address
    bcm_read_mac_address(nic);
    
    // Set MAC address
    bcm_write32(nic, BCM_REG_MAC_ADDR_0_HIGH,
                nic->mac_addr[0] << 8 | nic->mac_addr[1]);
    bcm_write32(nic, BCM_REG_MAC_ADDR_0_LOW,
                nic->mac_addr[2] << 24 | nic->mac_addr[3] << 16 |
                nic->mac_addr[4] << 8 | nic->mac_addr[5]);
    
    // Initialize rings
    if (bcm_init_rx_ring(nic) != 0) {
        return -1;
    }
    
    if (bcm_init_tx_ring(nic) != 0) {
        return -1;
    }
    
    // Setup receive list placement
    bcm_write32(nic, BCM_REG_RCV_LIST_PLACEMENT_MODE,
                BCM_RCV_LIST_PLACEMENT_ENABLE);
    
    // Enable MAC
    bcm_write32(nic, BCM_REG_TX_MAC_MODE, BCM_TX_MAC_ENABLE);
    bcm_write32(nic, BCM_REG_RX_MAC_MODE, BCM_RX_MAC_ENABLE);
    
    // Enable transmit/receive
    bcm_write32(nic, BCM_REG_TX_MODE, BCM_TX_MODE_ENABLE);
    bcm_write32(nic, BCM_REG_RX_MODE, BCM_RX_MODE_ENABLE);
    
    // Enable host coalescing
    bcm_write32(nic, BCM_REG_HOST_COAL_MODE, BCM_HOST_COAL_ENABLE);
    
    // Enable DMA engines
    bcm_write32(nic, BCM_REG_RD_DMA_MODE, BCM_DMA_MODE_ENABLE);
    bcm_write32(nic, BCM_REG_WR_DMA_MODE, BCM_DMA_MODE_ENABLE);
    
    // Clear statistics
    bcm_write32(nic, BCM_REG_MAC_TX_STATS_CLEAR, 0xFFFFFFFF);
    bcm_write32(nic, BCM_REG_MAC_RX_STATS_CLEAR, 0xFFFFFFFF);
    
    // Enable interrupts
    bcm_write32(nic, BCM_REG_MISC_HOST_CTRL,
                BCM_MISC_HOST_CTRL_CLEAR_INT | BCM_MISC_HOST_CTRL_MASK_PCI_INT |
                BCM_MISC_HOST_CTRL_INDIRECT_ACCESS | BCM_MISC_HOST_CTRL_ENABLE_PCI_STATE);
    
    return 0;
}

// =============================================================================
// Driver Interface
// =============================================================================

static void* broadcom_probe(device_node_t* node) {
    if (node->vendor_id != 0x14E4) {
        return NULL;  // Not Broadcom
    }
    
    // Check if device is supported
    bool supported = false;
    for (int i = 0; broadcom_devices[i].device_id != 0; i++) {
        if (node->device_id == broadcom_devices[i].device_id) {
            supported = true;
            break;
        }
    }
    
    if (!supported) {
        return NULL;
    }
    
    broadcom_nic_t* nic = flux_allocate(NULL, sizeof(broadcom_nic_t),
                                        FLUX_ALLOC_KERNEL | FLUX_ALLOC_ZERO);
    if (!nic) {
        return NULL;
    }
    
    // Get PCI resources
    pci_device_info_t* pci_info = (pci_device_info_t*)node->bus_specific_data;
    
    // Map registers (BAR0)
    nic->regs = (void*)(uintptr_t)(pci_info->bars[0] & ~0x0F);
    
    spinlock_init(&nic->rx_lock);
    spinlock_init(&nic->tx_lock);
    
    // Initialize device
    if (bcm_init_device(nic) != 0) {
        flux_free(nic);
        return NULL;
    }
    
    // Add to global list
    spinlock_acquire(&g_broadcom_lock);
    g_broadcom_nics[g_broadcom_nic_count++] = nic;
    spinlock_release(&g_broadcom_lock);
    
    return nic;
}

static int broadcom_attach(device_handle_t* handle) {
    broadcom_nic_t* nic = (broadcom_nic_t*)handle->driver_data;
    nic->state = BROADCOM_STATE_UP;
    return 0;
}

static void broadcom_detach(device_handle_t* handle) {
    broadcom_nic_t* nic = (broadcom_nic_t*)handle->driver_data;
    
    // Disable interrupts
    bcm_write32(nic, BCM_REG_MISC_HOST_CTRL, 0);
    
    // Disable MAC
    bcm_write32(nic, BCM_REG_TX_MAC_MODE, 0);
    bcm_write32(nic, BCM_REG_RX_MAC_MODE, 0);
    
    // Reset device
    bcm_reset_device(nic);
    
    nic->state = BROADCOM_STATE_DOWN;
}

// Driver registration
static resonance_driver_t broadcom_driver = {
    .name = "broadcom-ethernet",
    .vendor_ids = {0x14E4, 0},
    .device_ids = {0},  // Check in probe
    .probe = broadcom_probe,
    .attach = broadcom_attach,
    .detach = broadcom_detach
};

void broadcom_init(void) {
    resonance_register_driver(&broadcom_driver);
}
