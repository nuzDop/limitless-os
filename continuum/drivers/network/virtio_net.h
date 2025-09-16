/*
 * VirtIO Network Driver Header
 * Virtual network device definitions
 */

#ifndef VIRTIO_NET_H
#define VIRTIO_NET_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "../resonance.h"

// =============================================================================
// VirtIO Network Constants
// =============================================================================

#define MAX_VIRTIO_NET_DEVICES  16
#define VIRTIO_NET_QUEUE_SIZE   256
#define VIRTIO_NET_BUFFER_SIZE  2048
#define VIRTIO_NET_MAX_PACKET_SIZE 1514

// VirtIO PCI registers (legacy)
#define VIRTIO_PCI_DEVICE_FEATURES  0x00
#define VIRTIO_PCI_DRIVER_FEATURES  0x04
#define VIRTIO_PCI_QUEUE_PFN        0x08
#define VIRTIO_PCI_QUEUE_SIZE       0x0C
#define VIRTIO_PCI_QUEUE_SEL        0x0E
#define VIRTIO_PCI_QUEUE_NOTIFY     0x10
#define VIRTIO_PCI_STATUS           0x12
#define VIRTIO_PCI_ISR              0x13
#define VIRTIO_PCI_CONFIG           0x14

// VirtIO status bits
#define VIRTIO_STATUS_ACKNOWLEDGE   0x01
#define VIRTIO_STATUS_DRIVER        0x02
#define VIRTIO_STATUS_DRIVER_OK     0x04
#define VIRTIO_STATUS_FEATURES_OK   0x08
#define VIRTIO_STATUS_DEVICE_ERROR  0x40
#define VIRTIO_STATUS_FAILED        0x80

// VirtIO network features
#define VIRTIO_NET_F_CSUM           (1 << 0)   // Host handles partial csum
#define VIRTIO_NET_F_GUEST_CSUM     (1 << 1)   // Guest handles partial csum
#define VIRTIO_NET_F_MAC            (1 << 5)   // Host has given MAC address
#define VIRTIO_NET_F_GSO            (1 << 6)   // Host handles GSO
#define VIRTIO_NET_F_GUEST_TSO4     (1 << 7)   // Guest can handle TSOv4
#define VIRTIO_NET_F_GUEST_TSO6     (1 << 8)   // Guest can handle TSOv6
#define VIRTIO_NET_F_GUEST_ECN      (1 << 9)   // Guest can handle TSO with ECN
#define VIRTIO_NET_F_GUEST_UFO      (1 << 10)  // Guest can handle UFO
#define VIRTIO_NET_F_HOST_TSO4      (1 << 11)  // Host can handle TSOv4
#define VIRTIO_NET_F_HOST_TSO6      (1 << 12)  // Host can handle TSOv6
#define VIRTIO_NET_F_HOST_ECN       (1 << 13)  // Host can handle TSO with ECN
#define VIRTIO_NET_F_HOST_UFO       (1 << 14)  // Host can handle UFO
#define VIRTIO_NET_F_MRG_RXBUF      (1 << 15)  // Host can merge receive buffers
#define VIRTIO_NET_F_STATUS         (1 << 16)  // Config status field available
#define VIRTIO_NET_F_CTRL_VQ        (1 << 17)  // Control channel available
#define VIRTIO_NET_F_CTRL_RX        (1 << 18)  // Control channel RX mode support
#define VIRTIO_NET_F_CTRL_VLAN      (1 << 19)  // Control channel VLAN filtering
#define VIRTIO_NET_F_GUEST_ANNOUNCE (1 << 21)  // Guest can send gratuitous packets
#define VIRTIO_NET_F_MQ             (1 << 22)  // Device supports multiple queues
#define VIRTIO_NET_F_CTRL_MAC_ADDR  (1 << 23)  // Set MAC address through control

// VirtIO network config offsets
#define VIRTIO_NET_CFG_MAC          0x00
#define VIRTIO_NET_CFG_STATUS       0x06
#define VIRTIO_NET_CFG_MAX_VQ_PAIRS 0x08

// VirtIO network status
#define VIRTIO_NET_S_LINK_UP        1
#define VIRTIO_NET_S_ANNOUNCE       2

// VirtQueue descriptor flags
#define VIRTQ_DESC_F_NEXT           1
#define VIRTQ_DESC_F_WRITE          2
#define VIRTQ_DESC_F_INDIRECT       4

// GSO types
#define VIRTIO_NET_HDR_GSO_NONE     0
#define VIRTIO_NET_HDR_GSO_TCPV4    1
#define VIRTIO_NET_HDR_GSO_UDP       3
#define VIRTIO_NET_HDR_GSO_TCPV6    4
#define VIRTIO_NET_HDR_GSO_ECN       0x80

// =============================================================================
// VirtIO Network Structures
// =============================================================================

// VirtQueue descriptor
typedef struct __attribute__((packed)) {
    uint64_t addr;
    uint32_t len;
    uint16_t flags;
    uint16_t next;
} virtq_desc_t;

// VirtQueue available ring
typedef struct __attribute__((packed)) {
    uint16_t flags;
    uint16_t idx;
    uint16_t ring[];
} virtq_avail_t;

// VirtQueue used element
typedef struct __attribute__((packed)) {
    uint32_t id;
    uint32_t len;
} virtq_used_elem_t;

// VirtQueue used ring
typedef struct __attribute__((packed)) {
    uint16_t flags;
    uint16_t idx;
    virtq_used_elem_t ring[];
} virtq_used_t;

// VirtIO network header
typedef struct __attribute__((packed)) {
    uint8_t flags;
    uint8_t gso_type;
    uint16_t hdr_len;
    uint16_t gso_size;
    uint16_t csum_start;
    uint16_t csum_offset;
} virtio_net_hdr_t;

// VirtIO network queue
typedef struct {
    uint16_t queue_idx;
    uint16_t queue_size;
    uint16_t last_used_idx;
    uint16_t free_head;
    
    // Queue components
    virtq_desc_t* desc;
    virtq_avail_t* avail;
    virtq_used_t* used;
    
    // DMA regions
    dma_region_t* queue_dma;
    dma_region_t* buffers[VIRTIO_NET_QUEUE_SIZE];
    
    // Device reference
    struct virtio_net_device* device;
    
    spinlock_t lock;
} virtio_net_queue_t;

// Network statistics
typedef struct {
    uint64_t rx_packets;
    uint64_t rx_bytes;
    uint64_t rx_errors;
    uint64_t rx_dropped;
    uint64_t tx_packets;
    uint64_t tx_bytes;
    uint64_t tx_errors;
    uint64_t tx_dropped;
} virtio_net_stats_t;

// Device state
typedef enum {
    VIRTIO_NET_STATE_DISABLED = 0,
    VIRTIO_NET_STATE_INITIALIZING,
    VIRTIO_NET_STATE_READY,
    VIRTIO_NET_STATE_ERROR
} virtio_net_state_t;

// VirtIO network device
typedef struct virtio_net_device {
    // Device access
    uint16_t io_base;        // Legacy I/O base
    void* common_cfg;        // Modern MMIO common config
    
    // Device state
    virtio_net_state_t state;
    uint32_t device_features;
    uint32_t driver_features;
    
    // Device properties
    uint8_t mac_addr[6];
    uint16_t status;
    uint16_t max_queue_pairs;
    bool link_up;
    bool has_csum;
    
    // Virtqueues
    virtio_net_queue_t* rx_queue;
    virtio_net_queue_t* tx_queue;
    virtio_net_queue_t* ctrl_queue;  // Optional control queue
    
    // Statistics
    virtio_net_stats_t stats;
} virtio_net_device_t;

// =============================================================================
// Function Prototypes
// =============================================================================

void virtio_net_init(void);
int virtio_net_send_packet(virtio_net_device_t* dev, void* data, size_t length);
int virtio_net_receive_packet(virtio_net_device_t* dev, void* buffer, size_t max_len);
void virtio_net_get_mac_address(virtio_net_device_t* dev, uint8_t* mac);
bool virtio_net_is_link_up(virtio_net_device_t* dev);
void virtio_net_get_stats(virtio_net_device_t* dev, virtio_net_stats_t* stats);

// Control queue operations (if supported)
int virtio_net_set_rx_mode(virtio_net_device_t* dev, bool promisc, bool allmulti);
int virtio_net_set_mac_address(virtio_net_device_t* dev, uint8_t* mac);
int virtio_net_add_vlan(virtio_net_device_t* dev, uint16_t vlan_id);
int virtio_net_del_vlan(virtio_net_device_t* dev, uint16_t vlan_id);

#endif /* VIRTIO_NET_H */
