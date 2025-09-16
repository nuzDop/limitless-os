/*
 * Broadcom Network Driver Header
 * BCM57xx Gigabit Ethernet controller definitions
 */

#ifndef BROADCOM_H
#define BROADCOM_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "../resonance.h"

// =============================================================================
// Constants
// =============================================================================

#define MAX_BROADCOM_NICS       8
#define BCM_RX_RING_SIZE        512
#define BCM_TX_RING_SIZE        512
#define BCM_RX_BUFFER_SIZE      2048
#define BCM_TX_BUFFER_SIZE      2048

// Major Register Groups
#define BCM_REG_MISC_CFG        0x6804
#define BCM_REG_MISC_HOST_CTRL  0x6890
#define BCM_REG_FASTBOOT_PC     0x6894
#define BCM_REG_DMA_RW_CTRL     0x6C00
#define BCM_REG_MEM_ARBITER_MODE 0x4000

// MAC Registers
#define BCM_REG_MAC_MODE        0x0400
#define BCM_REG_MAC_STATUS      0x0404
#define BCM_REG_MAC_ADDR_0_HIGH 0x0410
#define BCM_REG_MAC_ADDR_0_LOW  0x0414
#define BCM_REG_TX_MAC_MODE     0x045C
#define BCM_REG_RX_MAC_MODE     0x0468
#define BCM_REG_MAC_HASH_0      0x0470
#define BCM_REG_MAC_HASH_1      0x0474
#define BCM_REG_MAC_HASH_2      0x0478
#define BCM_REG_MAC_HASH_3      0x047C

// Statistics Registers
#define BCM_REG_MAC_TX_STATS_CLEAR 0x0800
#define BCM_REG_MAC_RX_STATS_CLEAR 0x0900

// Buffer Manager
#define BCM_REG_BUFFER_MGR_MODE 0x4400
#define BCM_REG_BUFFER_MGR_STATUS 0x4404

// Receive List Placement
#define BCM_REG_RCV_LIST_PLACEMENT_MODE 0x2000
#define BCM_REG_RCV_LIST_PLACEMENT_STATUS 0x2004

// Host Coalescing
#define BCM_REG_HOST_COAL_MODE  0x3C00
#define BCM_REG_HOST_COAL_STATUS 0x3C04
#define BCM_REG_HOST_COAL_RX_TICK 0x3C08
#define BCM_REG_HOST_COAL_TX_TICK 0x3C0C
#define BCM_REG_HOST_COAL_RX_MAX_COAL 0x3C10
#define BCM_REG_HOST_COAL_TX_MAX_COAL 0x3C14

// DMA Registers
#define BCM_REG_RD_DMA_MODE     0x4800
#define BCM_REG_WR_DMA_MODE     0x4C00

// TX/RX Mode
#define BCM_REG_TX_MODE         0x0454
#define BCM_REG_RX_MODE         0x0500

// Standard Receive Producer Ring
#define BCM_REG_RX_STD_RCB_HADDR_HI 0x2450
#define BCM_REG_RX_STD_RCB_HADDR_LO 0x2454
#define BCM_REG_RX_STD_RCB_LEN_FLAGS 0x2458
#define BCM_REG_RX_STD_RCB_NICADDR 0x245C

// Transmit Ring
#define BCM_REG_TX_RCB_HADDR_HI 0x3800
#define BCM_REG_TX_RCB_HADDR_LO 0x3804
#define BCM_REG_TX_RCB_LEN_FLAGS 0x3808
#define BCM_REG_TX_RCB_NICADDR  0x380C

// Mailbox Registers
#define BCM_REG_RX_STD_PROD_IDX 0x0268
#define BCM_REG_RX_STD_CONS_IDX 0x0270
#define BCM_REG_TX_HOST_PROD_IDX 0x0304
#define BCM_REG_TX_NIC_CONS_IDX 0x3CC4

// NVRAM/EEPROM Registers
#define BCM_REG_NVRAM_CMD       0x7000
#define BCM_REG_NVRAM_ADDR      0x7014
#define BCM_REG_NVRAM_RDDATA    0x7010
#define BCM_REG_NVRAM_WRDATA    0x7008

// NVRAM Commands
#define BCM_NVRAM_CMD_START     (1 << 0)
#define BCM_NVRAM_CMD_DONE      (1 << 3)
#define BCM_NVRAM_CMD_WR        (1 << 5)
#define BCM_NVRAM_CMD_RD        (0 << 5)

// NVRAM Offsets
#define BCM_NVRAM_MAC_ADDR_HIGH 0x7C
#define BCM_NVRAM_MAC_ADDR_LOW  0x80

// Misc Host Control bits
#define BCM_MISC_HOST_CTRL_CLEAR_INT    (1 << 0)
#define BCM_MISC_HOST_CTRL_MASK_PCI_INT (1 << 1)
#define BCM_MISC_HOST_CTRL_INDIRECT_ACCESS (1 << 2)
#define BCM_MISC_HOST_CTRL_ENABLE_PCI_STATE (1 << 4)
#define BCM_MISC_HOST_CTRL_CLOCK_CTRL  (1 << 5)

// Misc Config bits
#define BCM_MISC_CFG_RESET      (1 << 0)

// Buffer Manager Mode
#define BCM_BUFFER_MGR_ENABLE   (1 << 1)
#define BCM_BUFFER_MGR_ATTN_ENABLE (1 << 2)

// Memory Arbiter
#define BCM_MEM_ARBITER_ENABLE  (1 << 1)

// TX/RX MAC Mode
#define BCM_TX_MAC_ENABLE       (1 << 0)
#define BCM_RX_MAC_ENABLE       (1 << 0)
#define BCM_RX_MAC_PROMISC      (1 << 8)

// TX/RX Mode
#define BCM_TX_MODE_ENABLE      (1 << 1)
#define BCM_RX_MODE_ENABLE      (1 << 1)

// DMA Mode
#define BCM_DMA_MODE_ENABLE     (1 << 1)

// Host Coalescing Mode
#define BCM_HOST_COAL_ENABLE    (1 << 1)
#define BCM_HOST_COAL_ATTN_ENABLE (1 << 2)

// Receive List Placement
#define BCM_RCV_LIST_PLACEMENT_ENABLE (1 << 1)

// RCB Flags
#define BCM_RCB_FLAG_USE_EXT_RCV_BD (1 << 0)
#define BCM_RCB_FLAG_RING_DISABLED (1 << 1)

// TX Descriptor Flags
#define BCM_TX_FLAG_PACKET_END  (1 << 0)
#define BCM_TX_FLAG_IP_CSUM     (1 << 1)
#define BCM_TX_FLAG_TCP_CSUM    (1 << 2)
#define BCM_TX_FLAG_VLAN        (1 << 6)

// =============================================================================
// Data Structures
// =============================================================================

// Receive Descriptor
typedef struct __attribute__((packed)) {
    uint32_t addr_hi;
    uint32_t addr_lo;
    uint32_t idx_vlan;      // Index and VLAN tag
    uint32_t len_flags;     // Length and flags
    uint32_t type_gen;      // Type and generation
    uint32_t reserved;
    uint32_t opaque;
    uint32_t error_flags;
} bcm_rx_desc_t;

// Transmit Descriptor
typedef struct __attribute__((packed)) {
    uint32_t addr_hi;
    uint32_t addr_lo;
    uint32_t len_flags;
    uint32_t vlan_tag;
} bcm_tx_desc_t;

// Device State
typedef enum {
    BROADCOM_STATE_DOWN = 0,
    BROADCOM_STATE_INITIALIZING,
    BROADCOM_STATE_UP,
    BROADCOM_STATE_ERROR
} broadcom_state_t;

// Broadcom NIC Structure
typedef struct {
    void* regs;
    broadcom_state_t state;
    
    // MAC address
    uint8_t mac_addr[6];
    
    // RX ring
    bcm_rx_desc_t* rx_std_ring;
    dma_region_t* rx_std_ring_dma;
    dma_region_t* rx_buffers[BCM_RX_RING_SIZE];
    uint32_t rx_std_prod;
    uint32_t rx_std_cons;
    spinlock_t rx_lock;
    
    // TX ring
    bcm_tx_desc_t* tx_ring;
    dma_region_t* tx_ring_dma;
    dma_region_t* tx_buffers[BCM_TX_RING_SIZE];
    uint32_t tx_prod;
    uint32_t tx_cons;
    spinlock_t tx_lock;
    
    // Link status
    bool link_up;
    uint32_t link_speed;
    bool full_duplex;
} broadcom_nic_t;

// =============================================================================
// Function Prototypes
// =============================================================================

void broadcom_init(void);
int broadcom_send_packet(broadcom_nic_t* nic, void* data, size_t length);
int broadcom_receive_packet(broadcom_nic_t* nic, void* buffer, size_t max_len);
void broadcom_get_mac_address(broadcom_nic_t* nic, uint8_t* mac);
bool broadcom_is_link_up(broadcom_nic_t* nic);

#endif /* BROADCOM_H */
