/*
 * Realtek Network Driver Header
 * RTL8139/8169/8168/8111 series definitions
 */

#ifndef REALTEK_H
#define REALTEK_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "../resonance.h"

// =============================================================================
// Constants
// =============================================================================

#define MAX_REALTEK_NICS        8
#define RTL8139_RX_BUFFER_SIZE  (8192 + 16 + 1500)
#define RTL8139_TX_BUFFERS      4
#define RTL8169_RX_DESC_COUNT   256
#define RTL8169_TX_DESC_COUNT   256
#define RTL8169_RX_BUFFER_SIZE  2048
#define RTL8169_TX_BUFFER_SIZE  2048

// Chip types
typedef enum {
    CHIP_UNKNOWN = 0,
    CHIP_RTL8139,
    CHIP_RTL8169,
    CHIP_RTL8168,
    CHIP_RTL8101
} realtek_chip_t;

// Common Registers
#define RTL_REG_IDR0        0x00    // MAC address
#define RTL_REG_IDR4        0x04    // MAC address
#define RTL_REG_MAR0        0x08    // Multicast filter
#define RTL_REG_MAR4        0x0C    // Multicast filter
#define RTL_REG_TSD0        0x10    // Transmit status (8139)
#define RTL_REG_TSAD0       0x20    // Transmit start address (8139)
#define RTL_REG_RBSTART     0x30    // Receive buffer start
#define RTL_REG_CR          0x37    // Command register
#define RTL_REG_CAPR        0x38    // Current address of packet read
#define RTL_REG_IMR         0x3C    // Interrupt mask
#define RTL_REG_ISR         0x3E    // Interrupt status
#define RTL_REG_TCR         0x40    // Transmit configuration
#define RTL_REG_RCR         0x44    // Receive configuration
#define RTL_REG_CONFIG1     0x52    // Configuration 1
#define RTL_REG_9346CR      0x50    // 93C46 command

// RTL8169/8168 specific registers
#define RTL_REG_TNPDS_LO    0x20    // TX normal priority descriptors
#define RTL_REG_TNPDS_HI    0x24
#define RTL_REG_THPDS_LO    0x28    // TX high priority descriptors
#define RTL_REG_THPDS_HI    0x2C
#define RTL_REG_RDSAR_LO    0xE4    // RX descriptor start address
#define RTL_REG_RDSAR_HI    0xE8
#define RTL_REG_MAC0        0x00    // MAC address byte 0
#define RTL_REG_TPPOLL      0x38    // Transmit priority polling
#define RTL_REG_RMS         0xDA    // Receive max size
#define RTL_REG_PHYAR       0x60    // PHY access

// Command Register bits
#define RTL_CR_RST          (1 << 4)   // Reset
#define RTL_CR_RE           (1 << 3)   // Receiver enable
#define RTL_CR_TE           (1 << 2)   // Transmitter enable

// Transmit Configuration Register
#define RTL_TCR_IFG_NORMAL  (3 << 24)  // Normal interframe gap
#define RTL_TCR_MXDMA_2048  (6 << 8)   // Max DMA burst 2048
#define RTL_TCR_MXDMA_UNLIMITED (7 << 8)

// Receive Configuration Register  
#define RTL_RCR_AAP         (1 << 0)   // Accept all packets
#define RTL_RCR_APM         (1 << 1)   // Accept physical match
#define RTL_RCR_AM          (1 << 2)   // Accept multicast
#define RTL_RCR_AB          (1 << 3)   // Accept broadcast
#define RTL_RCR_WRAP        (1 << 7)   // Wrap
#define RTL_RCR_RXFTH_NONE  (7 << 13)  // No RX threshold
#define RTL_RCR_MXDMA_UNLIMITED (7 << 8)

// Interrupt bits
#define RTL_INT_ROK         (1 << 0)   // Receive OK
#define RTL_INT_RER         (1 << 1)   // Receive error
#define RTL_INT_TOK         (1 << 2)   // Transmit OK
#define RTL_INT_TER         (1 << 3)   // Transmit error
#define RTL_INT_RDU         (1 << 4)   // RX descriptor unavailable
#define RTL_INT_TDU         (1 << 5)   // TX descriptor unavailable
#define RTL_INT_LINKCHG     (1 << 5)   // Link change
#define RTL_INT_FOVW        (1 << 6)   // RX FIFO overflow
#define RTL_INT_LENCHG      (1 << 13)  // Cable length change

// Transmit Status Descriptor
#define RTL_TSD_OWN         (1 << 13)  // Own
#define RTL_TSD_TUN         (1 << 14)  // Transmit FIFO underrun
#define RTL_TSD_TOK         (1 << 15)  // Transmit OK
#define RTL_TSD_CDH         (1 << 28)  // CD heart beat
#define RTL_TSD_OWC         (1 << 29)  // Out of window collision
#define RTL_TSD_TABT        (1 << 30)  // Transmit abort
#define RTL_TSD_CRS         (1 << 31)  // Carrier sense lost

// Receive Status
#define RTL_RX_ROK          (1 << 0)   // Receive OK
#define RTL_RX_FAE          (1 << 1)   // Frame alignment error
#define RTL_RX_CRC          (1 << 2)   // CRC error
#define RTL_RX_LONG         (1 << 3)   // Long packet
#define RTL_RX_RUNT         (1 << 4)   // Runt packet
#define RTL_RX_ISE          (1 << 5)   // Invalid symbol error

// RTL8169 descriptor flags
#define RTL8169_DESC_OWN    (1 << 31)  // Ownership
#define RTL8169_DESC_EOR    (1 << 30)  // End of ring
#define RTL8169_DESC_FS     (1 << 29)  // First segment
#define RTL8169_DESC_LS     (1 << 28)  // Last segment
#define RTL8169_DESC_RES    (1 << 20)  // Receive error summary

// 93C46 Command Register
#define RTL_9346CR_LOCK     0x00
#define RTL_9346CR_UNLOCK   0xC0

// TX Priority Polling
#define RTL_TPPOLL_NPQ      (1 << 6)   // Normal priority queue

// =============================================================================
// Data Structures
// =============================================================================

// RTL8169 Descriptor
typedef struct __attribute__((packed)) {
    uint32_t opts1;     // Command/status
    uint32_t opts2;     // VLAN
    uint64_t addr;      // Buffer address
} rtl8169_desc_t;

// Device state
typedef enum {
    REALTEK_STATE_DOWN = 0,
    REALTEK_STATE_INITIALIZING,
    REALTEK_STATE_UP,
    REALTEK_STATE_ERROR
} realtek_state_t;

// Realtek NIC structure
typedef struct {
    realtek_chip_t chip_type;
    realtek_state_t state;
    
    // Access method
    bool use_io;
    uint16_t io_base;
    void* mmio_base;
    
    // MAC address
    uint8_t mac_addr[6];
    
    // RTL8139 specific
    uint8_t* rx_buffer;
    dma_region_t* rx_buffer_dma;
    uint16_t rx_offset;
    dma_region_t* tx_buffers[RTL8139_TX_BUFFERS];
    
    // RTL8169 specific
    rtl8169_desc_t* rx_ring;
    rtl8169_desc_t* tx_ring;
    dma_region_t* rx_ring_dma;
    dma_region_t* tx_ring_dma;
    dma_region_t* rx_buffers[RTL8169_RX_DESC_COUNT];
    dma_region_t* tx_buffers[RTL8169_TX_DESC_COUNT];
    uint32_t rx_cur;
    uint32_t tx_cur;
    
    // Synchronization
    spinlock_t rx_lock;
    spinlock_t tx_lock;
    
    // Link status
    bool link_up;
    uint32_t link_speed;
    bool full_duplex;
} realtek_nic_t;

// =============================================================================
// Function Prototypes
// =============================================================================

void realtek_init(void);
int realtek_send_packet(realtek_nic_t* nic, void* data, size_t length);
int realtek_receive_packet(realtek_nic_t* nic, void* buffer, size_t max_len);
void realtek_get_mac_address(realtek_nic_t* nic, uint8_t* mac);
bool realtek_is_link_up(realtek_nic_t* nic);

// MMIO helper functions
static inline uint8_t mmio_read8(void* addr) {
    return *(volatile uint8_t*)addr;
}

static inline uint16_t mmio_read16(void* addr) {
    return *(volatile uint16_t*)addr;
}

static inline void mmio_write8(void* addr, uint8_t value) {
    *(volatile uint8_t*)addr = value;
}

static inline void mmio_write16(void* addr, uint16_t value) {
    *(volatile uint16_t*)addr = value;
}

#endif /* REALTEK_H */
