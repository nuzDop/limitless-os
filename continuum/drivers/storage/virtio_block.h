/*
 * VirtIO Block Driver Header
 * Virtual I/O block device definitions
 */

#ifndef VIRTIO_BLOCK_H
#define VIRTIO_BLOCK_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "../resonance.h"

// =============================================================================
// VirtIO Constants
// =============================================================================

#define MAX_VIRTIO_BLK_DEVICES  16
#define VIRTIO_BLK_QUEUE_SIZE   128

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

// VirtIO block features
#define VIRTIO_BLK_F_SIZE_MAX       (1 << 1)
#define VIRTIO_BLK_F_SEG_MAX        (1 << 2)
#define VIRTIO_BLK_F_GEOMETRY       (1 << 4)
#define VIRTIO_BLK_F_RO             (1 << 5)
#define VIRTIO_BLK_F_BLK_SIZE       (1 << 6)
#define VIRTIO_BLK_F_FLUSH          (1 << 9)
#define VIRTIO_BLK_F_TOPOLOGY       (1 << 10)
#define VIRTIO_BLK_F_CONFIG_WCE     (1 << 11)
#define VIRTIO_BLK_F_DISCARD        (1 << 13)
#define VIRTIO_BLK_F_WRITE_ZEROES   (1 << 14)

// VirtIO block config offsets
#define VIRTIO_BLK_CFG_CAPACITY     0x00
#define VIRTIO_BLK_CFG_SIZE_MAX     0x08
#define VIRTIO_BLK_CFG_SEG_MAX      0x0C
#define VIRTIO_BLK_CFG_GEOMETRY     0x10
#define VIRTIO_BLK_CFG_BLK_SIZE     0x14

// VirtIO block request types
#define VIRTIO_BLK_T_IN             0
#define VIRTIO_BLK_T_OUT            1
#define VIRTIO_BLK_T_FLUSH          4
#define VIRTIO_BLK_T_DISCARD        11
#define VIRTIO_BLK_T_WRITE_ZEROES   13

// VirtIO block status codes
#define VIRTIO_BLK_S_OK             0
#define VIRTIO_BLK_S_IOERR          1
#define VIRTIO_BLK_S_UNSUPP         2

// VirtQueue descriptor flags
#define VIRTQ_DESC_F_NEXT           1
#define VIRTQ_DESC_F_WRITE          2
#define VIRTQ_DESC_F_INDIRECT       4

// =============================================================================
// VirtQueue Structures
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

// =============================================================================
// VirtIO Block Structures
// =============================================================================

// Block request header
typedef struct __attribute__((packed)) {
    uint32_t type;
    uint32_t reserved;
    uint64_t sector;
} virtio_blk_req_header_t;

// Block device geometry
typedef struct {
    uint16_t cylinders;
    uint8_t heads;
    uint8_t sectors;
} virtio_blk_geometry_t;

// Block request structure
typedef struct virtio_blk_request {
    // Request components
    virtio_blk_req_header_t* header;
    void* data;
    uint8_t* status;
    
    // Physical addresses
    uint64_t header_phys;
    uint64_t data_phys;
    uint64_t status_phys;
    
    // Data length
    size_t data_len;
    
    // DMA regions
    dma_region_t* header_dma;
    dma_region_t* data_dma;
    dma_region_t* status_dma;
    
    // Completion
    bool completed;
    void* context;
} virtio_blk_request_t;

// VirtQueue structure
typedef struct virtqueue {
    uint16_t queue_idx;
    uint16_t queue_size;
    uint16_t last_used_idx;
    uint16_t last_avail_idx;
    uint16_t free_head;
    
    // Queue components
    virtq_desc_t* desc;
    virtq_avail_t* avail;
    virtq_used_t* used;
    
    // DMA region
    dma_region_t* queue_dma;
    
    // Request tracking
    virtio_blk_request_t* requests[VIRTIO_BLK_QUEUE_SIZE];
    
    // Device reference
    struct virtio_blk_device* device;
    
    spinlock_t lock;
} virtqueue_t;

// Device state
typedef enum {
    VIRTIO_BLK_STATE_DISABLED = 0,
    VIRTIO_BLK_STATE_INITIALIZING,
    VIRTIO_BLK_STATE_READY,
    VIRTIO_BLK_STATE_ERROR
} virtio_blk_state_t;

// VirtIO block device
typedef struct virtio_blk_device {
    // Device access
    uint16_t io_base;        // Legacy I/O base
    void* common_cfg;        // Modern MMIO common config
    void* device_cfg;        // Device-specific config
    void* notify_base;       // Notification area
    
    // Device state
    virtio_blk_state_t state;
    uint32_t device_features;
    uint32_t driver_features;
    
    // Device properties
    uint64_t capacity;       // Capacity in 512-byte sectors
    uint32_t block_size;     // Block size in bytes
    virtio_blk_geometry_t geometry;
    bool readonly;
    
    // Virtqueue
    virtqueue_t* vq;
    
    // Statistics
    uint64_t reads;
    uint64_t writes;
    uint64_t bytes_read;
    uint64_t bytes_written;
} virtio_blk_device_t;

// =============================================================================
// Function Prototypes
// =============================================================================

// Initialization
void virtio_blk_init(void);

// Device operations
int virtio_blk_read(virtio_blk_device_t* dev, uint64_t sector,
                   uint32_t count, void* buffer);
int virtio_blk_write(virtio_blk_device_t* dev, uint64_t sector,
                    uint32_t count, void* buffer);
int virtio_blk_flush(virtio_blk_device_t* dev);

// Device management
virtio_blk_device_t* virtio_blk_get_device(uint32_t index);
uint32_t virtio_blk_get_device_count(void);
uint64_t virtio_blk_get_capacity(virtio_blk_device_t* dev);
uint32_t virtio_blk_get_block_size(virtio_blk_device_t* dev);

// Helper functions for MMIO access
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

#endif /* VIRTIO_BLOCK_H */
