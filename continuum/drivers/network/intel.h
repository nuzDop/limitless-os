/*
 * Intel Network Driver Header
 * Intel Ethernet controller definitions
 */

#ifndef INTEL_NIC_H
#define INTEL_NIC_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "../resonance.h"

// =============================================================================
// Constants
// =============================================================================

#define MAX_INTEL_NICS          8
#define INTEL_RX_DESC_COUNT     256
#define INTEL_TX_DESC_COUNT     256
#define INTEL_RX_BUFFER_SIZE    2048
#define INTEL_TX_BUFFER_SIZE    2048

// Intel Registers
#define INTEL_REG_CTRL          0x0000  // Device Control
#define INTEL_REG_STATUS        0x0008  // Device Status
#define INTEL_REG_EERD          0x0014  // EEPROM Read
#define INTEL_REG_CTRL_EXT      0x0018  // Extended Device Control
#define INTEL_REG_ICR           0x00C0  // Interrupt Cause Read
#define INTEL_REG_ITR           0x00C4  // Interrupt Throttling
#define INTEL_REG_ICS           0x00C8  // Interrupt Cause Set
#define INTEL_REG_IMS           0x00D0  // Interrupt Mask Set
#define INTEL_REG_IMC           0x00D8  // Interrupt Mask Clear

// Receive Registers
#define INTEL_REG_RCTL          0x0100  // Receive Control
#define INTEL_REG_RDBAL         0x2800  // Receive Descriptor Base Low
#define INTEL_REG_RDBAH         0x2804  // Receive Descriptor Base High
#define INTEL_REG_RDLEN         0x2808  // Receive Descriptor Length
#define INTEL_REG_RDH           0x2810  // Receive Descriptor Head
#define INTEL_REG_RDT           0x2818  // Receive Descriptor Tail
#define INTEL_REG_RDTR          0x2820  // Receive Delay Timer
#define INTEL_REG_RADV          0x282C  // Receive Absolute Delay Timer

// Transmit Registers
#define INTEL_REG_TCTL          0x0400  // Transmit Control
#define INTEL_REG_TIPG          0x0410  // Transmit IPG
#define INTEL_REG_TDBAL         0x3800  // Transmit Descriptor Base Low
#define INTEL_REG_TDBAH         0x3804  // Transmit Descriptor Base High
#define INTEL_REG_TDLEN         0x3808  // Transmit Descriptor Length
#define INTEL_REG_TDH           0x3810  // Transmit Descriptor Head
#define INTEL_REG_TDT           0x3818  // Transmit Descriptor Tail
#define INTEL_REG_TIDV          0x3820  // Transmit Interrupt Delay

// MAC Address Registers
#define INTEL_REG_RAL(n)        (0x5400 + (n) * 8)  // Receive Address Low
#define INTEL_REG_RAH(n)        (0x5404 + (n) * 8)  // Receive Address High

// Control Register Bits
#define INTEL_CTRL_FD           (1 << 0)   // Full Duplex
#define INTEL_CTRL_LRST         (1 << 3)   // Link Reset
#define INTEL_CTRL_ASDE         (1 << 5)   // Auto-Speed Detection Enable
#define INTEL_CTRL_SLU          (1 << 6)   // Set Link Up
#define INTEL_CTRL_ILOS         (1 << 7)   // Invert Loss-of-Signal
#define INTEL_CTRL_SPEED_MASK   (3 << 8)   // Speed Selection Mask
#define INTEL_CTRL_FRCSPD       (1 << 11)  // Force Speed
#define INTEL_CTRL_FRCDPLX      (1 << 12)  // Force Duplex
#define INTEL_CTRL_RST          (1 << 26)  // Device Reset
#define INTEL_CTRL_RFCE         (1 << 27)  // Receive Flow Control Enable
#define INTEL_CTRL_TFCE         (1 << 28)  // Transmit Flow Control Enable
#define INTEL_CTRL_VME          (1 << 30)  // VLAN Mode Enable

// Status Register Bits
#define INTEL_STATUS_FD         (1 << 0)   // Full Duplex
#define INTEL_STATUS_LU         (1 << 1)   // Link Up
#define INTEL_STATUS_SPEED_MASK (3 << 6)   // Speed Mask

// EEPROM Read Register
#define INTEL_EERD_START        (1 << 0)   // Start Read
#define INTEL_EERD_DONE         (1 << 4)   // Read Done

// Receive Control Register
#define INTEL_RCTL_EN           (1 << 1)   // Receiver Enable
#define INTEL_RCTL_SBP          (1 << 2)   // Store Bad Packets
#define INTEL_RCTL_UPE          (1 << 3)   // Unicast Promiscuous Enable
#define INTEL_RCTL_MPE          (1 << 4)   // Multicast Promiscuous Enable
#define INTEL_RCTL_LPE          (1 << 5)   // Long Packet Enable
#define INTEL_RCTL_LBM_NO       (0 << 6)   // No Loopback
#define INTEL_RCTL_RDMTS_HALF   (0 << 8)   // Rx Desc Min Threshold Size
#define INTEL_RCTL_BAM          (1 << 15)  // Broadcast Accept Mode
#define INTEL_RCTL_BSIZE_2048   (0 << 16)  // Buffer Size 2048

// Transmit Control Register
#define INTEL_TCTL_EN           (1 << 1)   // Transmitter Enable
#define INTEL_TCTL_PSP          (1 << 3)   // Pad Short Packets
#define INTEL_TCTL_CT_SHIFT     4           // Collision Threshold
#define INTEL_TCTL_COLD_SHIFT   12          // Collision Distance

// RAH Register
#define INTEL_RAH_AV            (1 << 31)  // Address Valid

// Interrupt Bits
#define INTEL_INT_TXDW          (1 << 0)   // Transmit Descriptor Written Back
#define INTEL_INT_TXQE          (1 << 1)   // Transmit Queue Empty
#define INTEL_INT_LSC           (1 << 2)   // Link Status Change
#define INTEL_INT_RXSEQ         (1 << 3)   // Receive Sequence Error
#define INTEL_INT_RXDMT0        (1 << 4)   // Receive Descriptor Min Threshold
#define INTEL_INT_RXO           (1 << 6)   // Receiver Overrun
#define INTEL_INT_RXT0          (1 << 7)   // Receiver Timer Interrupt

// Descriptor Status
#define INTEL_RX_STATUS_DD      (1 << 0)   // Descriptor Done
#define INTEL_RX_STATUS_EOP     (1 << 1)   // End of Packet
#define INTEL_TX_STATUS_DD      (1 << 0)   // Descriptor Done

// Transmit Command
#define INTEL_TX_CMD_EOP        (1 << 0)   // End of Packet
#define INTEL_TX_CMD_IFCS       (1 << 1)   // Insert FCS
#define INTEL_TX_CMD_RS         (1 << 3)   // Report Status

// =============================================================================
// Data Structures
// =============================================================================

// Receive Descriptor
typedef struct __attribute__((packed)) {
    uint64_t addr;          // Buffer address
    uint16_t length;        // Length
    uint16_t checksum;      // Packet checksum
    uint8_t status;         // Descriptor status
    uint8_t errors;         // Errors
    uint16_t special;       // Special
} intel_rx_desc_t;

// Transmit Descriptor
typedef struct __attribute__((packed)) {
    uint64_t addr;          // Buffer address
    uint16_t length;        // Length
    uint8_t cso;           // Checksum offset
    uint8_t cmd;           // Command
    uint8_t status;        // Status
    uint8_t css;           // Checksum start
    uint16_t special;      // Special
} intel_tx_desc_t;

// Device State
typedef enum {
    INTEL_STATE_DOWN = 0,
    INTEL_STATE_INITIALIZING,
    INTEL_STATE_UP,
    INTEL_STATE_ERROR
} intel_state_t;

// Network Statistics
typedef struct {
    uint64_t rx_packets;
    uint64_t rx_bytes;
    uint64_t rx_errors;
    uint64_t rx_dropped;
    uint64_t tx_packets;
    uint64_t tx_bytes;
    uint64_t tx_errors;
    uint64_t tx_dropped;
} net_stats_t;

// Intel NIC Structure
typedef struct {
    void* mmio_base;
    intel_state_t state;
    
    // MAC address
    uint8_t mac_addr[6];
    
    // Receive ring
    intel_rx_desc_t* rx_ring;
    dma_region_t* rx_ring_dma;
    dma_region_t* rx_buffers[INTEL_RX_DESC_COUNT];
    uint32_t rx_cur;
    spinlock_t rx_lock;
    
    // Transmit ring
    intel_tx_desc_t* tx_ring;
    dma_region_t* tx_ring_dma;
    dma_region_t* tx_buffers[INTEL_TX_DESC_COUNT];
    uint32_t tx_cur;
    spinlock_t tx_lock;
    
    // Link status
    bool link_up;
    uint32_t link_speed;  // Mbps
    bool full_duplex;
    
    // Statistics
    net_stats_t stats;
} intel_nic_t;

// =============================================================================
// Function Prototypes
// =============================================================================

void intel_nic_init(void);
int intel_send_packet(intel_nic_t* nic, void* data, size_t length);
int intel_receive_packet(intel_nic_t* nic, void* buffer, size_t max_len);
void intel_get_mac_address(intel_nic_t* nic, uint8_t* mac);
bool intel_is_link_up(intel_nic_t* nic);
void intel_get_stats(intel_nic_t* nic, net_stats_t* stats);

#endif /* INTEL_NIC_H */
