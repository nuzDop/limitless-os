/*
 * NVMe Driver Header
 * NVM Express storage driver definitions
 */

#ifndef NVME_H
#define NVME_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "../resonance.h"

// =============================================================================
// NVMe Constants
// =============================================================================

#define MAX_NVME_CONTROLLERS    16
#define MAX_NVME_NAMESPACES     128
#define MAX_NVME_QUEUES         64
#define NVME_QUEUE_SIZE         256

// NVMe Registers
#define NVME_REG_CAP            0x00    // Controller Capabilities
#define NVME_REG_VS             0x08    // Version
#define NVME_REG_INTMS          0x0C    // Interrupt Mask Set
#define NVME_REG_INTMC          0x10    // Interrupt Mask Clear
#define NVME_REG_CC             0x14    // Controller Configuration
#define NVME_REG_CSTS           0x1C    // Controller Status
#define NVME_REG_NSSR           0x20    // NVM Subsystem Reset
#define NVME_REG_AQA            0x24    // Admin Queue Attributes
#define NVME_REG_ASQ            0x28    // Admin Submission Queue
#define NVME_REG_ACQ            0x30    // Admin Completion Queue
#define NVME_REG_ASQ_TAIL       0x1000  // Admin SQ Tail Doorbell
#define NVME_REG_ACQ_HEAD       0x1004  // Admin CQ Head Doorbell

// Controller Configuration bits
#define NVME_CC_ENABLE          (1 << 0)
#define NVME_CC_CSS_NVM         (0 << 4)
#define NVME_CC_MPS_SHIFT       7
#define NVME_CC_AMS_RR          (0 << 11)
#define NVME_CC_SHN_NONE        (0 << 14)
#define NVME_CC_IOSQES_SHIFT    16
#define NVME_CC_IOCQES_SHIFT    20

// Controller Status bits
#define NVME_CSTS_RDY           (1 << 0)
#define NVME_CSTS_CFS           (1 << 1)
#define NVME_CSTS_SHST_MASK     (3 << 2)
#define NVME_CSTS_NSSRO         (1 << 4)

// Admin Commands
#define NVME_ADMIN_DELETE_SQ    0x00
#define NVME_ADMIN_CREATE_SQ    0x01
#define NVME_ADMIN_GET_LOG      0x02
#define NVME_ADMIN_DELETE_CQ    0x04
#define NVME_ADMIN_CREATE_CQ    0x05
#define NVME_ADMIN_IDENTIFY     0x06
#define NVME_ADMIN_ABORT        0x08
#define NVME_ADMIN_SET_FEATURES 0x09
#define NVME_ADMIN_GET_FEATURES 0x0A
#define NVME_ADMIN_ASYNC_EVENT  0x0C
#define NVME_ADMIN_FW_COMMIT    0x10
#define NVME_ADMIN_FW_DOWNLOAD  0x11

// I/O Commands
#define NVME_IO_FLUSH           0x00
#define NVME_IO_WRITE           0x01
#define NVME_IO_READ            0x02
#define NVME_IO_WRITE_UNCOR     0x04
#define NVME_IO_COMPARE         0x05
#define NVME_IO_WRITE_ZEROS     0x08
#define NVME_IO_DSM             0x09
#define NVME_IO_RESERVATION     0x0D

// DMA Flags
#define DMA_FLAG_COHERENT       (1 << 0)
#define DMA_FLAG_STREAMING      (1 << 1)

// =============================================================================
// NVMe Data Structures
// =============================================================================

// NVMe Command
typedef struct __attribute__((packed)) {
    uint8_t opcode;
    uint8_t flags;
    uint16_t command_id;
    uint32_t nsid;
    uint64_t reserved;
    uint64_t metadata;
    uint64_t prp1;
    uint64_t prp2;
    uint32_t cdw10;
    uint32_t cdw11;
    uint32_t cdw12;
    uint32_t cdw13;
    uint32_t cdw14;
    uint32_t cdw15;
} nvme_command_t;

// NVMe Completion
typedef struct __attribute__((packed)) {
    uint32_t result;
    uint32_t reserved;
    uint16_t sq_head;
    uint16_t sq_id;
    uint16_t command_id;
    uint16_t status;
} nvme_completion_t;

// Identify Controller Data
typedef struct __attribute__((packed)) {
    uint16_t vid;           // Vendor ID
    uint16_t ssvid;         // Subsystem Vendor ID
    char sn[20];           // Serial Number
    char mn[40];           // Model Number
    char fr[8];            // Firmware Revision
    uint8_t rab;           // Recommended Arbitration Burst
    uint8_t ieee[3];       // IEEE OUI Identifier
    uint8_t cmic;          // Controller Multi-Path I/O
    uint8_t mdts;          // Maximum Data Transfer Size
    uint16_t cntlid;       // Controller ID
    uint32_t ver;          // Version
    uint32_t rtd3r;        // RTD3 Resume Latency
    uint32_t rtd3e;        // RTD3 Entry Latency
    uint32_t oaes;         // Optional Async Events Supported
    uint32_t ctratt;       // Controller Attributes
    uint8_t reserved1[156];
    uint16_t oacs;         // Optional Admin Command Support
    uint8_t acl;           // Abort Command Limit
    uint8_t aerl;          // Async Event Request Limit
    uint8_t frmw;          // Firmware Updates
    uint8_t lpa;           // Log Page Attributes
    uint8_t elpe;          // Error Log Page Entries
    uint8_t npss;          // Number of Power States Support
    uint8_t avscc;         // Admin Vendor Specific Command Config
    uint8_t apsta;         // Autonomous Power State Transition
    uint16_t wctemp;       // Warning Composite Temperature Threshold
    uint16_t cctemp;       // Critical Composite Temperature Threshold
    uint16_t mtfa;         // Maximum Time for Firmware Activation
    uint32_t hmpre;        // Host Memory Buffer Preferred Size
    uint32_t hmmin;        // Host Memory Buffer Minimum Size
    uint8_t reserved2[230];
    uint8_t sqes;          // Submission Queue Entry Size
    uint8_t cqes;          // Completion Queue Entry Size
    uint16_t maxcmd;       // Maximum Outstanding Commands
    uint32_t nn;           // Number of Namespaces
    uint16_t oncs;         // Optional NVM Command Support
    uint16_t fuses;        // Fused Operation Support
    uint8_t fna;           // Format NVM Attributes
    uint8_t vwc;           // Volatile Write Cache
    uint16_t awun;         // Atomic Write Unit Normal
    uint16_t awupf;        // Atomic Write Unit Power Fail
    uint8_t nvscc;         // NVM Vendor Specific Command Config
    uint8_t reserved3[1];
    uint16_t acwu;         // Atomic Compare & Write Unit
    uint8_t reserved4[2];
    uint32_t sgls;         // SGL Support
    uint8_t reserved5[228];
    char subnqn[256];      // NVM Subsystem NVMe Qualified Name
    uint8_t reserved6[768];
    uint8_t reserved7[256];
    uint8_t psd[32][32];   // Power State Descriptors
    uint8_t vendor_specific[1024];
} nvme_identify_controller_t;

// Identify Namespace Data
typedef struct __attribute__((packed)) {
    uint64_t nsze;         // Namespace Size
    uint64_t ncap;         // Namespace Capacity
    uint64_t nuse;         // Namespace Utilization
    uint8_t nsfeat;        // Namespace Features
    uint8_t nlbaf;         // Number of LBA Formats
    uint8_t flbas;         // Formatted LBA Size
    uint8_t mc;            // Metadata Capabilities
    uint8_t dpc;           // Data Protection Capabilities
    uint8_t dps;           // Data Protection Settings
    uint8_t nmic;          // Namespace Multi-path I/O
    uint8_t rescap;        // Reservation Capabilities
    uint8_t fpi;           // Format Progress Indicator
    uint8_t reserved1[2];
    uint16_t nawun;        // Namespace Atomic Write Unit Normal
    uint16_t nawupf;       // Namespace Atomic Write Unit Power Fail
    uint16_t nacwu;        // Namespace Atomic Compare & Write Unit
    uint16_t nabsn;        // Namespace Atomic Boundary Size Normal
    uint16_t nabo;         // Namespace Atomic Boundary Offset
    uint16_t nabspf;       // Namespace Atomic Boundary Size Power Fail
    uint8_t reserved2[2];
    uint8_t nvmcap[16];    // NVM Capacity
    uint8_t reserved3[104];
    uint8_t nguid[16];     // Namespace Globally Unique Identifier
    uint8_t eui64[8];      // IEEE Extended Unique Identifier
    struct {
        uint16_t ms;       // Metadata Size
        uint8_t ds;        // Data Size
        uint8_t rp;        // Relative Performance
    } lbaf[16];            // LBA Format Support
    uint8_t reserved4[192];
    uint8_t vendor_specific[3712];
} nvme_identify_namespace_t;

// Command tracking info
typedef struct {
    bool submitted;
    void* completion_context;
    uint64_t submit_time;
} nvme_cmd_info_t;

// NVMe Queue
typedef struct nvme_queue {
    uint16_t qid;
    uint16_t size;
    uint16_t sq_tail;
    uint16_t cq_head;
    uint8_t cq_phase;
    
    nvme_command_t* sq;
    nvme_completion_t* cq;
    dma_region_t* sq_dma;
    dma_region_t* cq_dma;
    
    nvme_cmd_info_t* commands;
    struct nvme_controller* controller;
    spinlock_t lock;
} nvme_queue_t;

// NVMe Namespace
typedef struct nvme_namespace {
    struct nvme_controller* controller;
    uint32_t nsid;
    uint64_t size;          // Size in blocks
    uint64_t capacity;      // Capacity in blocks
    uint64_t utilization;   // Utilization in blocks
    uint32_t block_size;    // Block size in bytes
    uint8_t features;
} nvme_namespace_t;

// NVMe Controller State
typedef enum {
    NVME_STATE_DISABLED = 0,
    NVME_STATE_INITIALIZING,
    NVME_STATE_READY,
    NVME_STATE_ERROR
} nvme_state_t;

// NVMe Controller
typedef struct nvme_controller {
    void* bar0;             // Memory-mapped registers
    nvme_state_t state;
    
    // Controller properties
    uint16_t vid;
    uint16_t ssvid;
    char serial_number[20];
    char model_number[40];
    char firmware_rev[8];
    
    // Capabilities
    uint32_t max_queue_entries;
    uint32_t doorbell_stride;
    uint32_t num_namespaces;
    
    // Queues
    nvme_queue_t* admin_queue;
    nvme_queue_t* io_queues[MAX_NVME_QUEUES];
    uint16_t num_io_queues;
    uint16_t queue_size;
    
    // Namespaces
    nvme_namespace_t* namespaces[MAX_NVME_NAMESPACES];
    
    // Statistics
    uint64_t commands_submitted;
    uint64_t commands_completed;
    uint64_t bytes_read;
    uint64_t bytes_written;
} nvme_controller_t;

// Completion handler type
typedef void (*nvme_completion_handler_t)(nvme_completion_t* cqe);

// =============================================================================
// Function Prototypes
// =============================================================================

// Initialization
void nvme_init(void);

// I/O operations
int nvme_read(nvme_namespace_t* ns, uint64_t lba, uint32_t count, void* buffer);
int nvme_write(nvme_namespace_t* ns, uint64_t lba, uint32_t count, void* buffer);
int nvme_flush(nvme_namespace_t* ns);
int nvme_trim(nvme_namespace_t* ns, uint64_t lba, uint32_t count);

// Controller management
nvme_controller_t* nvme_get_controller(uint32_t index);
uint32_t nvme_get_controller_count(void);

// Namespace management
nvme_namespace_t* nvme_get_namespace(nvme_controller_t* ctrl, uint32_t nsid);
uint64_t nvme_get_namespace_size(nvme_namespace_t* ns);
uint32_t nvme_get_block_size(nvme_namespace_t* ns);

// Admin commands
int nvme_format_namespace(nvme_namespace_t* ns, uint8_t lbaf);
int nvme_get_log_page(nvme_controller_t* ctrl, uint8_t log_id, void* buffer, size_t size);
int nvme_set_features(nvme_controller_t* ctrl, uint8_t fid, uint32_t value);

// Helper functions
void* memcpy(void* dest, const void* src, size_t n);
void* memset(void* s, int c, size_t n);

#endif /* NVME_H */
