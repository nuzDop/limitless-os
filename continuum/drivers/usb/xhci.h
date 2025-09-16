/*
 * xHCI USB 3.0 Host Controller Driver Header
 * Extensible Host Controller Interface definitions
 */

#ifndef XHCI_H
#define XHCI_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "../resonance.h"

// =============================================================================
// xHCI Constants
// =============================================================================

#define MAX_XHCI_CONTROLLERS    4
#define XHCI_MAX_SLOTS         256
#define XHCI_MAX_ENDPOINTS     32
#define XHCI_MAX_PORTS         127
#define XHCI_EVENT_RING_SIZE   256
#define XHCI_CMD_RING_SIZE     64
#define XHCI_TRANSFER_RING_SIZE 256

// Capability Registers
#define XHCI_CAP_CAPLENGTH     0x00
#define XHCI_CAP_HCIVERSION    0x02
#define XHCI_CAP_HCSPARAMS1    0x04
#define XHCI_CAP_HCSPARAMS2    0x08
#define XHCI_CAP_HCSPARAMS3    0x0C
#define XHCI_CAP_HCCPARAMS1    0x10
#define XHCI_CAP_DBOFF         0x14
#define XHCI_CAP_RTSOFF        0x18
#define XHCI_CAP_HCCPARAMS2    0x1C

// HCSPARAMS1 fields
#define XHCI_HCS1_MAX_SLOTS(x)  ((x) & 0xFF)
#define XHCI_HCS1_MAX_INTRS(x)  (((x) >> 8) & 0x7FF)
#define XHCI_HCS1_MAX_PORTS(x)  (((x) >> 24) & 0xFF)

// Operational Registers
#define XHCI_OP_USBCMD         0x00
#define XHCI_OP_USBSTS         0x04
#define XHCI_OP_PAGESIZE       0x08
#define XHCI_OP_DNCTRL         0x14
#define XHCI_OP_CRCR           0x18
#define XHCI_OP_DCBAAP         0x30
#define XHCI_OP_CONFIG         0x38

// USBCMD bits
#define XHCI_CMD_RUN           (1 << 0)
#define XHCI_CMD_RESET         (1 << 1)
#define XHCI_CMD_INTE          (1 << 2)
#define XHCI_CMD_HSEE          (1 << 3)
#define XHCI_CMD_LHCRST        (1 << 7)
#define XHCI_CMD_CSS           (1 << 8)
#define XHCI_CMD_CRS           (1 << 9)
#define XHCI_CMD_EWE           (1 << 10)
#define XHCI_CMD_EU3S          (1 << 11)

// USBSTS bits
#define XHCI_STS_HCH           (1 << 0)
#define XHCI_STS_HSE           (1 << 2)
#define XHCI_STS_EINT          (1 << 3)
#define XHCI_STS_PCD           (1 << 4)
#define XHCI_STS_SSS           (1 << 8)
#define XHCI_STS_RSS           (1 << 9)
#define XHCI_STS_SRE           (1 << 10)
#define XHCI_STS_CNR           (1 << 11)
#define XHCI_STS_HCE           (1 << 12)

// CRCR bits
#define XHCI_CRCR_RCS          (1 << 0)
#define XHCI_CRCR_CS           (1 << 1)
#define XHCI_CRCR_CA           (1 << 2)
#define XHCI_CRCR_CRR          (1 << 3)

// Port Status and Control Register
#define XHCI_PORTSC_CCS        (1 << 0)   // Current Connect Status
#define XHCI_PORTSC_PED        (1 << 1)   // Port Enabled/Disabled
#define XHCI_PORTSC_OCA        (1 << 3)   // Over-current Active
#define XHCI_PORTSC_PR         (1 << 4)   // Port Reset
#define XHCI_PORTSC_PLS_MASK   (0xF << 5) // Port Link State
#define XHCI_PORTSC_PLS_SHIFT  5
#define XHCI_PORTSC_PP         (1 << 9)   // Port Power
#define XHCI_PORTSC_SPEED_MASK (0xF << 10)
#define XHCI_PORTSC_SPEED_SHIFT 10
#define XHCI_PORTSC_PIC_MASK   (0x3 << 14)
#define XHCI_PORTSC_LWS        (1 << 16)  // Port Link State Write Strobe
#define XHCI_PORTSC_CSC        (1 << 17)  // Connect Status Change
#define XHCI_PORTSC_PEC        (1 << 18)  // Port Enabled/Disabled Change
#define XHCI_PORTSC_WRC        (1 << 19)  // Warm Port Reset Change
#define XHCI_PORTSC_OCC        (1 << 20)  // Over-current Change
#define XHCI_PORTSC_PRC        (1 << 21)  // Port Reset Change
#define XHCI_PORTSC_PLC        (1 << 22)  // Port Link State Change
#define XHCI_PORTSC_CEC        (1 << 23)  // Port Config Error Change
#define XHCI_PORTSC_CAS        (1 << 24)  // Cold Attach Status

// TRB Types
#define TRB_NORMAL             1
#define TRB_SETUP              2
#define TRB_DATA               3
#define TRB_STATUS             4
#define TRB_ISOCH              5
#define TRB_LINK               6
#define TRB_EVENT_DATA         7
#define TRB_NOOP               8
#define TRB_ENABLE_SLOT        9
#define TRB_DISABLE_SLOT       10
#define TRB_ADDRESS_DEVICE     11
#define TRB_CONFIG_EP          12
#define TRB_EVALUATE_CONTEXT   13
#define TRB_RESET_EP           14
#define TRB_STOP_EP            15
#define TRB_SET_TR_DEQUEUE     16
#define TRB_RESET_DEVICE       17
#define TRB_FORCE_EVENT        18
#define TRB_NEGOTIATE_BW       19
#define TRB_SET_LATENCY_TOL    20
#define TRB_GET_PORT_BW        21
#define TRB_FORCE_HEADER       22
#define TRB_NOOP_CMD           23

// Event TRB Types
#define TRB_TRANSFER           32
#define TRB_COMMAND_COMPLETE   33
#define TRB_PORT_STATUS_CHANGE 34
#define TRB_BANDWIDTH_REQUEST  35
#define TRB_DOORBELL           36
#define TRB_HOST_CONTROLLER    37
#define TRB_DEVICE_NOTIFY      38
#define TRB_MFINDEX_WRAP       39

// TRB Control bits
#define TRB_C                  (1 << 0)   // Cycle bit
#define TRB_TC                 (1 << 1)   // Toggle cycle
#define TRB_ENT                (1 << 1)   // Evaluate Next TRB
#define TRB_ISP                (1 << 2)   // Interrupt on Short Packet
#define TRB_NS                 (1 << 3)   // No Snoop
#define TRB_CH                 (1 << 4)   // Chain bit
#define TRB_IOC                (1 << 5)   // Interrupt On Completion
#define TRB_IDT                (1 << 6)   // Immediate Data
#define TRB_BSR                (1 << 9)   // Block Set Address Request

// TRB Macros
#define TRB_TYPE(x)            ((x) << 10)
#define TRB_TYPE_GET(x)        (((x) >> 10) & 0x3F)
#define TRB_SLOT(x)            ((x) << 24)
#define TRB_SLOT_GET(x)        (((x) >> 24) & 0xFF)
#define TRB_EP(x)              ((x) << 16)
#define TRB_EP_GET(x)          (((x) >> 16) & 0x1F)

// Completion Codes
#define COMP_SUCCESS           1
#define COMP_DATA_BUFFER_ERROR 2
#define COMP_BABBLE            3
#define COMP_USB_TRANS_ERROR   4
#define COMP_TRB_ERROR         5
#define COMP_STALL             6
#define COMP_RESOURCE_ERROR    7
#define COMP_BANDWIDTH_ERROR   8
#define COMP_NO_SLOTS          9
#define COMP_INVALID_STREAM    10
#define COMP_SLOT_NOT_ENABLED  11
#define COMP_EP_NOT_ENABLED    12
#define COMP_SHORT_PACKET      13
#define COMP_RING_UNDERRUN     14
#define COMP_RING_OVERRUN      15

// USB Speeds
#define USB_SPEED_FULL         1
#define USB_SPEED_LOW          2
#define USB_SPEED_HIGH         3
#define USB_SPEED_SUPER        4
#define USB_SPEED_SUPER_PLUS   5

// =============================================================================
// xHCI Data Structures
// =============================================================================

// Transfer Request Block (TRB)
typedef struct __attribute__((packed)) {
    uint64_t parameter;
    uint32_t status;
    uint32_t control;
} xhci_trb_t;

// Event Ring Segment Table Entry
typedef struct __attribute__((packed)) {
    uint64_t base;
    uint16_t size;
    uint16_t reserved;
    uint32_t reserved2;
} xhci_erst_entry_t;

// Slot Context
typedef struct __attribute__((packed)) {
    uint32_t route_string   : 20;
    uint32_t speed          : 4;
    uint32_t reserved1      : 1;
    uint32_t mtt            : 1;
    uint32_t hub            : 1;
    uint32_t context_entries: 5;
    
    uint32_t max_exit_latency : 16;
    uint32_t root_hub_port    : 8;
    uint32_t num_ports        : 8;
    
    uint32_t parent_hub_slot  : 8;
    uint32_t parent_port      : 8;
    uint32_t tt_think_time    : 2;
    uint32_t reserved2        : 4;
    uint32_t interrupter      : 10;
    
    uint32_t device_address   : 8;
    uint32_t reserved3        : 19;
    uint32_t slot_state       : 5;
    
    uint32_t reserved4[4];
} xhci_slot_context_t;

// Endpoint Context
typedef struct __attribute__((packed)) {
    uint32_t ep_state       : 3;
    uint32_t reserved1      : 5;
    uint32_t mult           : 2;
    uint32_t max_pstreams   : 5;
    uint32_t lsa            : 1;
    uint32_t interval       : 8;
    uint32_t max_esit_hi    : 8;
    
    uint32_t reserved2      : 1;
    uint32_t cerr           : 2;
    uint32_t ep_type        : 3;
    uint32_t reserved3      : 1;
    uint32_t hid            : 1;
    uint32_t max_burst_size : 8;
    uint32_t max_packet_size: 16;
    
    uint64_t tr_dequeue_ptr;
    
    uint32_t average_trb_len: 16;
    uint32_t max_esit_lo    : 16;
    
    uint32_t reserved4[3];
} xhci_ep_context_t;

// Device Context
typedef struct __attribute__((packed)) {
    xhci_slot_context_t slot;
    xhci_ep_context_t endpoints[31];
} xhci_device_context_t;

// Input Control Context
typedef struct __attribute__((packed)) {
    uint32_t drop_context_flags;
    uint32_t add_context_flags;
    uint32_t reserved[5];
    uint32_t config_value;
} xhci_input_control_context_t;

// Input Context
typedef struct __attribute__((packed)) {
    xhci_input_control_context_t control;
    xhci_device_context_t device;
} xhci_input_context_t;

// Port Register Set
typedef struct __attribute__((packed)) {
    uint32_t portsc;
    uint32_t portpmsc;
    uint32_t portli;
    uint32_t porthlpmc;
} xhci_port_regs_t;

// Operational Registers
typedef struct __attribute__((packed)) {
    uint32_t usbcmd;
    uint32_t usbsts;
    uint32_t pagesize;
    uint32_t reserved1[2];
    uint32_t dnctrl;
    uint64_t crcr;
    uint32_t reserved2[4];
    uint64_t dcbaap;
    uint32_t config;
    uint32_t reserved3[241];
    xhci_port_regs_t ports[];
} xhci_op_regs_t;

// Interrupter Register Set
typedef struct __attribute__((packed)) {
    uint32_t iman;
    uint32_t imod;
    uint32_t erstsz;
    uint32_t reserved;
    uint64_t erstba;
    uint64_t erdp;
} xhci_interrupter_t;

// Runtime Registers
typedef struct __attribute__((packed)) {
    uint32_t mfindex;
    uint32_t reserved[7];
    xhci_interrupter_t interrupters[1024];
} xhci_rt_regs_t;

// Event Ring
typedef struct {
    xhci_trb_t* ring;
    xhci_erst_entry_t* erst;
    dma_region_t* ring_dma;
    dma_region_t* erst_dma;
    xhci_trb_t* dequeue;
    uint32_t cycle_state;
} xhci_event_ring_t;

// Transfer Ring
typedef struct {
    xhci_trb_t* ring;
    dma_region_t* ring_dma;
    uint32_t size;
    xhci_trb_t* enqueue;
    xhci_trb_t* dequeue;
    uint32_t cycle_state;
} xhci_transfer_ring_t;

// Port State
typedef struct {
    bool connected;
    bool enabled;
    uint32_t speed;
    uint8_t slot_id;
} xhci_port_t;

// Controller State
typedef enum {
    XHCI_STATE_HALTED = 0,
    XHCI_STATE_RUNNING,
    XHCI_STATE_ERROR
} xhci_state_t;

// xHCI Controller
typedef struct {
    uint8_t* cap_regs;
    xhci_op_regs_t* op_regs;
    xhci_rt_regs_t* rt_regs;
    uint32_t* db_regs;
    
    xhci_state_t state;
    
    // Controller properties
    uint32_t max_slots;
    uint32_t max_intrs;
    uint32_t num_ports;
    
    // Device Context Base Address Array
    uint64_t* dcbaa;
    dma_region_t* dcbaa_dma;
    
    // Command ring
    xhci_trb_t* cmd_ring;
    dma_region_t* cmd_ring_dma;
    xhci_trb_t* cmd_enqueue;
    uint32_t cmd_cycle;
    spinlock_t cmd_lock;
    
    // Event rings
    xhci_event_ring_t event_rings[1024];
    spinlock_t event_lock;
    
    // Port status
    xhci_port_t ports[XHCI_MAX_PORTS];
    
    // Device contexts
    xhci_device_context_t* device_contexts[XHCI_MAX_SLOTS];
    xhci_transfer_ring_t* transfer_rings[XHCI_MAX_SLOTS][XHCI_MAX_ENDPOINTS];
} xhci_controller_t;

// =============================================================================
// Function Prototypes
// =============================================================================

void xhci_init(void);
int xhci_reset_port(xhci_controller_t* xhci, uint32_t port);
int xhci_enumerate_device(xhci_controller_t* xhci, uint32_t port);
int xhci_configure_endpoint(xhci_controller_t* xhci, uint8_t slot_id, uint8_t ep_num);
int xhci_transfer(xhci_controller_t* xhci, uint8_t slot_id, uint8_t ep_num,
                  void* buffer, size_t length, bool is_input);

#endif /* XHCI_H */
