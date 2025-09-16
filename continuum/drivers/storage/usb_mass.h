/*
 * USB Mass Storage Driver Header
 * Bulk-Only Transport definitions
 */

#ifndef USB_MASS_H
#define USB_MASS_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "../resonance.h"

// =============================================================================
// USB Constants
// =============================================================================

#define MAX_USB_MASS_DEVICES    32

// USB Classes
#define USB_CLASS_MASS_STORAGE  0x08

// Mass Storage Subclasses
#define USB_MASS_SUBCLASS_SCSI  0x06

// Mass Storage Protocols
#define USB_MASS_PROTOCOL_CBI   0x00  // Control/Bulk/Interrupt
#define USB_MASS_PROTOCOL_CB    0x01  // Control/Bulk
#define USB_MASS_PROTOCOL_BBB   0x50  // Bulk-Only Transport

// USB Requests
#define USB_REQUEST_CLEAR_FEATURE   0x01
#define USB_FEATURE_ENDPOINT_HALT   0x00

// Mass Storage Requests
#define USB_MASS_REQUEST_RESET      0xFF
#define USB_MASS_REQUEST_GET_MAX_LUN 0xFE

// CBW/CSW Signatures
#define CBW_SIGNATURE   0x43425355  // "USBC"
#define CSW_SIGNATURE   0x53425355  // "USBS"

// SCSI Commands
#define SCSI_CMD_TEST_UNIT_READY    0x00
#define SCSI_CMD_REQUEST_SENSE       0x03
#define SCSI_CMD_FORMAT_UNIT         0x04
#define SCSI_CMD_INQUIRY             0x12
#define SCSI_CMD_MODE_SENSE_6        0x1A
#define SCSI_CMD_START_STOP_UNIT     0x1B
#define SCSI_CMD_PREVENT_ALLOW_MEDIUM_REMOVAL 0x1E
#define SCSI_CMD_READ_CAPACITY_10    0x25
#define SCSI_CMD_READ_10             0x28
#define SCSI_CMD_WRITE_10            0x2A
#define SCSI_CMD_VERIFY_10           0x2F
#define SCSI_CMD_SYNCHRONIZE_CACHE   0x35
#define SCSI_CMD_MODE_SENSE_10       0x5A
#define SCSI_CMD_READ_12             0xA8
#define SCSI_CMD_WRITE_12            0xAA
#define SCSI_CMD_READ_16             0x88
#define SCSI_CMD_WRITE_16            0x8A

// SCSI Status
#define SCSI_STATUS_GOOD             0x00
#define SCSI_STATUS_CHECK_CONDITION  0x02
#define SCSI_STATUS_CONDITION_MET    0x04
#define SCSI_STATUS_BUSY             0x08
#define SCSI_STATUS_RESERVATION_CONFLICT 0x18

// =============================================================================
// USB Structures
// =============================================================================

// USB Setup packet
typedef struct __attribute__((packed)) {
    uint8_t request_type;
    uint8_t request;
    uint16_t value;
    uint16_t index;
    uint16_t length;
} usb_setup_packet_t;

// Command Block Wrapper (CBW)
typedef struct __attribute__((packed)) {
    uint32_t signature;          // "USBC"
    uint32_t tag;               // Command tag
    uint32_t data_transfer_length;
    uint8_t flags;              // Direction: 0x80 = IN, 0x00 = OUT
    uint8_t lun;                // Logical Unit Number
    uint8_t cb_length;          // Command block length (1-16)
    uint8_t cb[16];             // Command block (SCSI command)
} cbw_t;

// Command Status Wrapper (CSW)
typedef struct __attribute__((packed)) {
    uint32_t signature;         // "USBS"
    uint32_t tag;              // Must match CBW tag
    uint32_t data_residue;     // Amount of data not transferred
    uint8_t status;            // Command status
} csw_t;

// =============================================================================
// SCSI Structures
// =============================================================================

// SCSI INQUIRY data
typedef struct __attribute__((packed)) {
    uint8_t peripheral_device_type : 5;
    uint8_t peripheral_qualifier : 3;
    uint8_t reserved1 : 7;
    uint8_t rmb : 1;            // Removable media bit
    uint8_t version;
    uint8_t response_data_format : 4;
    uint8_t hi_sup : 1;
    uint8_t norm_aca : 1;
    uint8_t reserved2 : 2;
    uint8_t additional_length;
    uint8_t reserved3[3];
    uint8_t vendor_id[8];
    uint8_t product_id[16];
    uint8_t product_rev[4];
} scsi_inquiry_data_t;

// SCSI READ CAPACITY data
typedef struct __attribute__((packed)) {
    uint32_t last_lba;          // Big-endian
    uint32_t block_size;        // Big-endian
} scsi_read_capacity_data_t;

// SCSI REQUEST SENSE data
typedef struct __attribute__((packed)) {
    uint8_t error_code : 7;
    uint8_t valid : 1;
    uint8_t segment_number;
    uint8_t sense_key : 4;
    uint8_t reserved : 1;
    uint8_t ili : 1;
    uint8_t eom : 1;
    uint8_t filemark : 1;
    uint8_t information[4];
    uint8_t additional_sense_length;
    uint8_t command_specific_info[4];
    uint8_t asc;                // Additional Sense Code
    uint8_t ascq;               // Additional Sense Code Qualifier
    uint8_t fruc;               // Field Replaceable Unit Code
    uint8_t sense_key_specific[3];
} scsi_sense_data_t;

// =============================================================================
// Driver Structures
// =============================================================================

// USB device info (from USB enumeration)
typedef struct {
    uint8_t address;
    uint8_t configuration;
    uint8_t interface;
    uint16_t vendor_id;
    uint16_t product_id;
    uint8_t device_class;
    uint8_t device_subclass;
    uint8_t device_protocol;
} usb_device_info_t;

// Device state
typedef enum {
    USB_MASS_STATE_DISCONNECTED = 0,
    USB_MASS_STATE_INITIALIZING,
    USB_MASS_STATE_READY,
    USB_MASS_STATE_ERROR,
    USB_MASS_STATE_SUSPENDED
} usb_mass_state_t;

// USB Mass Storage device
typedef struct {
    device_node_t* usb_device;
    usb_mass_state_t state;
    
    // USB endpoints
    uint8_t bulk_in_ep;
    uint8_t bulk_out_ep;
    uint8_t interface_num;
    
    // Device properties
    uint8_t lun;                // Current LUN
    uint8_t max_lun;            // Maximum LUN
    uint32_t tag_counter;       // CBW tag counter
    
    // SCSI device info
    char vendor_id[9];
    char product_id[17];
    char product_rev[5];
    uint8_t device_type;
    bool removable;
    bool write_protected;
    
    // Storage capacity
    uint32_t last_lba;
    uint32_t block_size;
    uint64_t capacity;
    
    // Statistics
    uint64_t commands_sent;
    uint64_t bytes_read;
    uint64_t bytes_written;
    uint64_t errors;
    
    spinlock_t lock;
} usb_mass_device_t;

// =============================================================================
// Function Prototypes
// =============================================================================

// Initialization
void usb_mass_init(void);

// Device operations
int usb_mass_read(usb_mass_device_t* dev, uint64_t lba, uint32_t count, void* buffer);
int usb_mass_write(usb_mass_device_t* dev, uint64_t lba, uint32_t count, void* buffer);
int usb_mass_flush(usb_mass_device_t* dev);

// Device management
usb_mass_device_t* usb_mass_get_device(uint32_t index);
uint32_t usb_mass_get_device_count(void);
uint64_t usb_mass_get_capacity(usb_mass_device_t* dev);
uint32_t usb_mass_get_block_size(usb_mass_device_t* dev);
const char* usb_mass_get_vendor(usb_mass_device_t* dev);
const char* usb_mass_get_product(usb_mass_device_t* dev);

// SCSI commands
int usb_mass_test_unit_ready(usb_mass_device_t* dev);
int usb_mass_request_sense(usb_mass_device_t* dev);
int usb_mass_start_stop_unit(usb_mass_device_t* dev, bool start, bool eject);

#endif /* USB_MASS_H */
