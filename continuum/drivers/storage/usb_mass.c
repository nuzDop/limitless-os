/*
 * USB Mass Storage Driver for Continuum Kernel
 * Bulk-Only Transport (BOT) implementation for USB storage devices
 */

#include "usb_mass.h"
#include "../resonance.h"
#include "../../flux_memory.h"

// =============================================================================
// Global USB Mass Storage State
// =============================================================================

static usb_mass_device_t* g_usb_mass_devices[MAX_USB_MASS_DEVICES];
static uint32_t g_usb_mass_count = 0;
static spinlock_t g_usb_mass_lock = SPINLOCK_INIT;

// =============================================================================
// USB Control Transfer
// =============================================================================

static int usb_control_transfer(usb_mass_device_t* dev, usb_setup_packet_t* setup,
                               void* data, uint16_t length) {
    // This would interface with the USB host controller driver
    // Simplified implementation
    return 0;
}

static int usb_bulk_transfer(usb_mass_device_t* dev, uint8_t endpoint,
                            void* data, uint32_t length, uint32_t* transferred) {
    // This would interface with the USB host controller driver
    // Simplified implementation
    *transferred = length;
    return 0;
}

// =============================================================================
// SCSI Command Execution
// =============================================================================

static int usb_mass_send_cbw(usb_mass_device_t* dev, cbw_t* cbw) {
    uint32_t transferred;
    return usb_bulk_transfer(dev, dev->bulk_out_ep, cbw, sizeof(cbw_t), &transferred);
}

static int usb_mass_recv_csw(usb_mass_device_t* dev, csw_t* csw) {
    uint32_t transferred;
    int result = usb_bulk_transfer(dev, dev->bulk_in_ep, csw, sizeof(csw_t), &transferred);
    
    if (result != 0 || transferred != sizeof(csw_t)) {
        return -1;
    }
    
    // Validate CSW
    if (csw->signature != CSW_SIGNATURE) {
        return -1;
    }
    
    return csw->status;
}

static int usb_mass_execute_scsi(usb_mass_device_t* dev, uint8_t* cdb,
                                uint8_t cdb_len, void* data, uint32_t data_len,
                                bool is_write) {
    spinlock_acquire(&dev->lock);
    
    // Build CBW
    cbw_t cbw = {0};
    cbw.signature = CBW_SIGNATURE;
    cbw.tag = dev->tag_counter++;
    cbw.data_transfer_length = data_len;
    cbw.flags = is_write ? 0x00 : 0x80;  // Direction: 0x80 = IN, 0x00 = OUT
    cbw.lun = dev->lun;
    cbw.cb_length = cdb_len;
    memcpy(cbw.cb, cdb, cdb_len);
    
    // Send CBW
    if (usb_mass_send_cbw(dev, &cbw) != 0) {
        spinlock_release(&dev->lock);
        return -1;
    }
    
    // Transfer data if needed
    if (data_len > 0) {
        uint32_t transferred;
        uint8_t endpoint = is_write ? dev->bulk_out_ep : dev->bulk_in_ep;
        
        if (usb_bulk_transfer(dev, endpoint, data, data_len, &transferred) != 0) {
            spinlock_release(&dev->lock);
            return -1;
        }
        
        if (transferred != data_len) {
            // Handle short transfer
            // May need to recover with Reset Recovery
        }
    }
    
    // Receive CSW
    csw_t csw;
    int status = usb_mass_recv_csw(dev, &csw);
    
    if (csw.tag != cbw.tag) {
        // Tag mismatch - protocol error
        spinlock_release(&dev->lock);
        return -1;
    }
    
    if (csw.data_residue > 0) {
        // Not all data transferred
        // Handle residue
    }
    
    spinlock_release(&dev->lock);
    
    return status;
}

// =============================================================================
// SCSI Commands
// =============================================================================

static int usb_mass_inquiry(usb_mass_device_t* dev) {
    uint8_t cdb[6] = {
        SCSI_CMD_INQUIRY,
        0,  // EVPD = 0
        0,  // Page code
        0,  // Reserved
        36, // Allocation length
        0   // Control
    };
    
    scsi_inquiry_data_t inquiry_data;
    int result = usb_mass_execute_scsi(dev, cdb, 6, &inquiry_data, 36, false);
    
    if (result == 0) {
        // Parse inquiry data
        memcpy(dev->vendor_id, inquiry_data.vendor_id, 8);
        memcpy(dev->product_id, inquiry_data.product_id, 16);
        memcpy(dev->product_rev, inquiry_data.product_rev, 4);
        
        dev->vendor_id[8] = '\0';
        dev->product_id[16] = '\0';
        dev->product_rev[4] = '\0';
        
        dev->device_type = inquiry_data.peripheral_device_type;
        dev->removable = inquiry_data.rmb ? true : false;
    }
    
    return result;
}

static int usb_mass_test_unit_ready(usb_mass_device_t* dev) {
    uint8_t cdb[6] = {
        SCSI_CMD_TEST_UNIT_READY,
        0, 0, 0, 0, 0
    };
    
    return usb_mass_execute_scsi(dev, cdb, 6, NULL, 0, false);
}

static int usb_mass_request_sense(usb_mass_device_t* dev) {
    uint8_t cdb[6] = {
        SCSI_CMD_REQUEST_SENSE,
        0,  // Descriptor format
        0,  // Reserved
        0,  // Reserved
        18, // Allocation length
        0   // Control
    };
    
    scsi_sense_data_t sense_data;
    return usb_mass_execute_scsi(dev, cdb, 6, &sense_data, 18, false);
}

static int usb_mass_read_capacity(usb_mass_device_t* dev) {
    uint8_t cdb[10] = {
        SCSI_CMD_READ_CAPACITY_10,
        0, 0, 0, 0, 0, 0, 0, 0, 0
    };
    
    scsi_read_capacity_data_t capacity_data;
    int result = usb_mass_execute_scsi(dev, cdb, 10, &capacity_data, 8, false);
    
    if (result == 0) {
        dev->last_lba = __builtin_bswap32(capacity_data.last_lba);
        dev->block_size = __builtin_bswap32(capacity_data.block_size);
        dev->capacity = ((uint64_t)dev->last_lba + 1) * dev->block_size;
    }
    
    return result;
}

// =============================================================================
// Read/Write Operations
// =============================================================================

int usb_mass_read(usb_mass_device_t* dev, uint64_t lba, uint32_t count, void* buffer) {
    if (!dev || !buffer || count == 0) {
        return -1;
    }
    
    // Use READ(10) command for simplicity
    uint8_t cdb[10] = {
        SCSI_CMD_READ_10,
        0,  // Flags
        (lba >> 24) & 0xFF,
        (lba >> 16) & 0xFF,
        (lba >> 8) & 0xFF,
        lba & 0xFF,
        0,  // Group number
        (count >> 8) & 0xFF,
        count & 0xFF,
        0   // Control
    };
    
    uint32_t data_len = count * dev->block_size;
    return usb_mass_execute_scsi(dev, cdb, 10, buffer, data_len, false);
}

int usb_mass_write(usb_mass_device_t* dev, uint64_t lba, uint32_t count, void* buffer) {
    if (!dev || !buffer || count == 0) {
        return -1;
    }
    
    if (dev->write_protected) {
        return -1;
    }
    
    // Use WRITE(10) command
    uint8_t cdb[10] = {
        SCSI_CMD_WRITE_10,
        0,  // Flags
        (lba >> 24) & 0xFF,
        (lba >> 16) & 0xFF,
        (lba >> 8) & 0xFF,
        lba & 0xFF,
        0,  // Group number
        (count >> 8) & 0xFF,
        count & 0xFF,
        0   // Control
    };
    
    uint32_t data_len = count * dev->block_size;
    return usb_mass_execute_scsi(dev, cdb, 10, buffer, data_len, true);
}

// =============================================================================
// USB Mass Storage Reset Recovery
// =============================================================================

static int usb_mass_reset(usb_mass_device_t* dev) {
    // Bulk-Only Mass Storage Reset
    usb_setup_packet_t setup = {
        .request_type = 0x21,  // Class, Interface, Host to Device
        .request = USB_MASS_REQUEST_RESET,
        .value = 0,
        .index = dev->interface_num,
        .length = 0
    };
    
    return usb_control_transfer(dev, &setup, NULL, 0);
}

static int usb_mass_clear_halt(usb_mass_device_t* dev, uint8_t endpoint) {
    // Clear Feature HALT
    usb_setup_packet_t setup = {
        .request_type = 0x02,  // Standard, Endpoint, Host to Device
        .request = USB_REQUEST_CLEAR_FEATURE,
        .value = USB_FEATURE_ENDPOINT_HALT,
        .index = endpoint,
        .length = 0
    };
    
    return usb_control_transfer(dev, &setup, NULL, 0);
}

// =============================================================================
// Device Initialization
// =============================================================================

static int usb_mass_init_device(usb_mass_device_t* dev) {
    // Get max LUN
    usb_setup_packet_t setup = {
        .request_type = 0xA1,  // Class, Interface, Device to Host
        .request = USB_MASS_REQUEST_GET_MAX_LUN,
        .value = 0,
        .index = dev->interface_num,
        .length = 1
    };
    
    uint8_t max_lun = 0;
    if (usb_control_transfer(dev, &setup, &max_lun, 1) == 0) {
        dev->max_lun = max_lun;
    } else {
        dev->max_lun = 0;  // Assume single LUN
    }
    
    // Reset device
    if (usb_mass_reset(dev) != 0) {
        return -1;
    }
    
    // Clear any stalls
    usb_mass_clear_halt(dev, dev->bulk_in_ep);
    usb_mass_clear_halt(dev, dev->bulk_out_ep);
    
    // Test unit ready (may fail initially)
    for (int i = 0; i < 5; i++) {
        if (usb_mass_test_unit_ready(dev) == 0) {
            break;
        }
        // Request sense to clear error
        usb_mass_request_sense(dev);
    }
    
    // Get device information
    if (usb_mass_inquiry(dev) != 0) {
        return -1;
    }
    
    // Get capacity
    if (usb_mass_read_capacity(dev) != 0) {
        return -1;
    }
    
    return 0;
}

// =============================================================================
// Driver Interface
// =============================================================================

static void* usb_mass_probe(device_node_t* node) {
    // Check for USB Mass Storage class
    usb_device_info_t* usb_info = (usb_device_info_t*)node->bus_specific_data;
    if (!usb_info || usb_info->device_class != USB_CLASS_MASS_STORAGE) {
        return NULL;
    }
    
    // Check for Bulk-Only Transport
    if (usb_info->device_protocol != USB_MASS_PROTOCOL_BBB) {
        return NULL;
    }
    
    usb_mass_device_t* dev = flux_allocate(NULL, sizeof(usb_mass_device_t),
                                          FLUX_ALLOC_KERNEL | FLUX_ALLOC_ZERO);
    if (!dev) {
        return NULL;
    }
    
    dev->usb_device = node;
    dev->interface_num = usb_info->interface;
    dev->bulk_in_ep = 0x81;   // Typical IN endpoint
    dev->bulk_out_ep = 0x02;  // Typical OUT endpoint
    spinlock_init(&dev->lock);
    
    // Initialize device
    if (usb_mass_init_device(dev) != 0) {
        flux_free(dev);
        return NULL;
    }
    
    // Add to global list
    spinlock_acquire(&g_usb_mass_lock);
    g_usb_mass_devices[g_usb_mass_count++] = dev;
    spinlock_release(&g_usb_mass_lock);
    
    return dev;
}

static int usb_mass_attach(device_handle_t* handle) {
    usb_mass_device_t* dev = (usb_mass_device_t*)handle->driver_data;
    dev->state = USB_MASS_STATE_READY;
    return 0;
}

static void usb_mass_detach(device_handle_t* handle) {
    usb_mass_device_t* dev = (usb_mass_device_t*)handle->driver_data;
    dev->state = USB_MASS_STATE_DISCONNECTED;
}

// Driver registration
static resonance_driver_t usb_mass_driver = {
    .name = "usb-storage",
    .class_code = USB_CLASS_MASS_STORAGE,
    .subclass_code = 0xFF,  // Any subclass
    .probe = usb_mass_probe,
    .attach = usb_mass_attach,
    .detach = usb_mass_detach
};

void usb_mass_init(void) {
    resonance_register_driver(&usb_mass_driver);
}
