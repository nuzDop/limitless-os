/*
 * USB HID Driver for Continuum Kernel
 * Human Interface Device driver for USB keyboards and mice
 */

#include "usb_hid.h"
#include "../resonance.h"
#include "../../flux_memory.h"
#include "../../conduit_ipc.h"

// =============================================================================
// Global HID State
// =============================================================================

static usb_hid_device_t* g_hid_devices[MAX_HID_DEVICES];
static uint32_t g_hid_device_count = 0;
static spinlock_t g_hid_lock = SPINLOCK_INIT;

// USB HID keyboard scan code to ASCII mapping
static const uint8_t hid_to_ascii[256] = {
    0, 0, 0, 0, 'a', 'b', 'c', 'd', 'e', 'f', 'g', 'h', 'i', 'j', 'k', 'l',
    'm', 'n', 'o', 'p', 'q', 'r', 's', 't', 'u', 'v', 'w', 'x', 'y', 'z',
    '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '\n', 0x1B, '\b', '\t',
    ' ', '-', '=', '[', ']', '\\', '#', ';', '\'', '`', ',', '.', '/',
    // ... more mappings
};

// =============================================================================
// HID Report Descriptor Parsing
// =============================================================================

static int hid_parse_report_descriptor(usb_hid_device_t* hid, uint8_t* descriptor,
                                       size_t length) {
    uint32_t i = 0;
    uint8_t report_id = 0;
    uint32_t usage_page = 0;
    uint32_t usage = 0;
    
    while (i < length) {
        uint8_t item = descriptor[i];
        uint8_t type = (item >> 2) & 0x03;
        uint8_t tag = (item >> 4) & 0x0F;
        uint8_t size = item & 0x03;
        
        if (size == 3) size = 4;  // Long item
        
        i++;
        
        uint32_t data = 0;
        if (size > 0 && i + size <= length) {
            memcpy(&data, &descriptor[i], size);
            i += size;
        }
        
        switch (type) {
            case 0: // Main item
                switch (tag) {
                    case 0x08: // Input
                        if (usage_page == HID_USAGE_PAGE_GENERIC_DESKTOP) {
                            if (usage == HID_USAGE_KEYBOARD) {
                                hid->type = HID_TYPE_KEYBOARD;
                                hid->keyboard.report_size = 8;
                            } else if (usage == HID_USAGE_MOUSE) {
                                hid->type = HID_TYPE_MOUSE;
                                hid->mouse.report_size = (data & 0x01) ? 8 : 3;
                            }
                        }
                        break;
                    case 0x09: // Output
                        break;
                    case 0x0B: // Feature
                        break;
                    case 0x0A: // Collection
                        break;
                    case 0x0C: // End Collection
                        break;
                }
                break;
                
            case 1: // Global item
                switch (tag) {
                    case 0x00: // Usage Page
                        usage_page = data;
                        break;
                    case 0x01: // Logical Minimum
                        break;
                    case 0x02: // Logical Maximum
                        break;
                    case 0x07: // Report Size
                        break;
                    case 0x08: // Report ID
                        report_id = data;
                        hid->uses_report_ids = true;
                        break;
                    case 0x09: // Report Count
                        break;
                }
                break;
                
            case 2: // Local item
                switch (tag) {
                    case 0x00: // Usage
                        usage = data;
                        break;
                    case 0x01: // Usage Minimum
                        break;
                    case 0x02: // Usage Maximum
                        break;
                }
                break;
        }
    }
    
    return 0;
}

// =============================================================================
// HID Keyboard Processing
// =============================================================================

static void hid_process_keyboard_report(usb_hid_device_t* hid, uint8_t* report) {
    hid_keyboard_t* kbd = &hid->keyboard;
    
    // Check modifier keys
    uint8_t modifiers = report[0];
    kbd->ctrl_pressed = (modifiers & 0x11) != 0;  // Left or right ctrl
    kbd->shift_pressed = (modifiers & 0x22) != 0; // Left or right shift
    kbd->alt_pressed = (modifiers & 0x44) != 0;   // Left or right alt
    kbd->gui_pressed = (modifiers & 0x88) != 0;   // Left or right GUI
    
    // Process key codes (up to 6 simultaneous keys)
    for (int i = 2; i < 8; i++) {
        uint8_t key = report[i];
        
        if (key == 0) {
            continue;  // No key
        }
        
        if (key == 0x01) {
            // Keyboard error - too many keys pressed
            continue;
        }
        
        // Check if key was already pressed
        bool was_pressed = false;
        for (int j = 0; j < 6; j++) {
            if (kbd->prev_keys[j] == key) {
                was_pressed = true;
                kbd->prev_keys[j] = 0;  // Mark as handled
                break;
            }
        }
        
        if (!was_pressed) {
            // New key press
            uint8_t ascii = 0;
            
            if (key < 256) {
                ascii = hid_to_ascii[key];
                
                // Apply shift
                if (kbd->shift_pressed && ascii >= 'a' && ascii <= 'z') {
                    ascii = ascii - 'a' + 'A';
                }
                
                // Apply caps lock
                if (kbd->caps_lock && ascii >= 'a' && ascii <= 'z') {
                    ascii = ascii - 'a' + 'A';
                } else if (kbd->caps_lock && ascii >= 'A' && ascii <= 'Z') {
                    ascii = ascii - 'A' + 'a';
                }
            }
            
            if (ascii != 0) {
                hid_add_key_to_buffer(hid, ascii);
            }
        }
    }
    
    // Check for released keys
    for (int i = 0; i < 6; i++) {
        if (kbd->prev_keys[i] != 0) {
            // Key was released
            // Could generate key-up event here
        }
    }
    
    // Save current keys as previous
    memcpy(kbd->prev_keys, &report[2], 6);
}

// =============================================================================
// HID Mouse Processing
// =============================================================================

static void hid_process_mouse_report(usb_hid_device_t* hid, uint8_t* report) {
    hid_mouse_t* mouse = &hid->mouse;
    
    uint8_t buttons = report[0];
    int8_t x_movement = (int8_t)report[1];
    int8_t y_movement = (int8_t)report[2];
    int8_t wheel = 0;
    
    if (mouse->report_size >= 4) {
        wheel = (int8_t)report[3];
    }
    
    // Update position
    mouse->x += x_movement;
    mouse->y += y_movement;
    
    // Clamp to screen bounds
    if (mouse->x < 0) mouse->x = 0;
    if (mouse->x >= mouse->screen_width) mouse->x = mouse->screen_width - 1;
    if (mouse->y < 0) mouse->y = 0;
    if (mouse->y >= mouse->screen_height) mouse->y = mouse->screen_height - 1;
    
    // Update buttons
    bool left_prev = mouse->left_button;
    bool right_prev = mouse->right_button;
    bool middle_prev = mouse->middle_button;
    
    mouse->left_button = (buttons & 0x01) != 0;
    mouse->right_button = (buttons & 0x02) != 0;
    mouse->middle_button = (buttons & 0x04) != 0;
    
    // Generate events
    mouse_event_t event = {0};
    event.x = mouse->x;
    event.y = mouse->y;
    event.dx = x_movement;
    event.dy = y_movement;
    event.dz = wheel;
    event.buttons = buttons & 0x07;
    
    if (x_movement != 0 || y_movement != 0) {
        event.type = MOUSE_EVENT_MOVE;
        hid_add_mouse_event(hid, &event);
    }
    
    if (mouse->left_button != left_prev) {
        event.type = mouse->left_button ? MOUSE_EVENT_BUTTON_DOWN : MOUSE_EVENT_BUTTON_UP;
        event.button = MOUSE_BUTTON_LEFT;
        hid_add_mouse_event(hid, &event);
    }
    
    if (mouse->right_button != right_prev) {
        event.type = mouse->right_button ? MOUSE_EVENT_BUTTON_DOWN : MOUSE_EVENT_BUTTON_UP;
        event.button = MOUSE_BUTTON_RIGHT;
        hid_add_mouse_event(hid, &event);
    }
    
    if (mouse->middle_button != middle_prev) {
        event.type = mouse->middle_button ? MOUSE_EVENT_BUTTON_DOWN : MOUSE_EVENT_BUTTON_UP;
        event.button = MOUSE_BUTTON_MIDDLE;
        hid_add_mouse_event(hid, &event);
    }
    
    if (wheel != 0) {
        event.type = MOUSE_EVENT_SCROLL;
        hid_add_mouse_event(hid, &event);
    }
}

// =============================================================================
// USB Transfer Handling
// =============================================================================

static void hid_interrupt_callback(usb_transfer_t* transfer) {
    usb_hid_device_t* hid = (usb_hid_device_t*)transfer->context;
    
    if (transfer->status == USB_TRANSFER_COMPLETED) {
        spinlock_acquire(&hid->lock);
        
        uint8_t* report = transfer->data;
        size_t length = transfer->actual_length;
        
        // Skip report ID if present
        if (hid->uses_report_ids && length > 0) {
            report++;
            length--;
        }
        
        // Process report based on device type
        switch (hid->type) {
            case HID_TYPE_KEYBOARD:
                hid_process_keyboard_report(hid, report);
                break;
                
            case HID_TYPE_MOUSE:
                hid_process_mouse_report(hid, report);
                break;
                
            case HID_TYPE_GAMEPAD:
                // Process gamepad report
                break;
        }
        
        spinlock_release(&hid->lock);
    }
    
    // Resubmit transfer for continuous polling
    usb_submit_transfer(transfer);
}

// =============================================================================
// HID Initialization
// =============================================================================

static int hid_init_device(usb_hid_device_t* hid) {
    // Get HID descriptor
    usb_hid_descriptor_t hid_desc;
    if (usb_get_descriptor(hid->usb_device, USB_DESC_HID, 0, &hid_desc,
                          sizeof(hid_desc)) != 0) {
        return -1;
    }
    
    // Get report descriptor
    uint8_t* report_desc = flux_allocate(NULL, hid_desc.report_desc_length,
                                        FLUX_ALLOC_KERNEL);
    if (!report_desc) {
        return -1;
    }
    
    if (usb_get_descriptor(hid->usb_device, USB_DESC_HID_REPORT, 0,
                          report_desc, hid_desc.report_desc_length) != 0) {
        flux_free(report_desc);
        return -1;
    }
    
    // Parse report descriptor
    hid_parse_report_descriptor(hid, report_desc, hid_desc.report_desc_length);
    flux_free(report_desc);
    
    // Set idle rate (0 = infinite)
    usb_control_transfer(hid->usb_device,
                        USB_REQ_TYPE_CLASS | USB_REQ_TYPE_INTERFACE,
                        HID_REQ_SET_IDLE,
                        0,  // No idle
                        hid->interface_num,
                        NULL, 0);
    
    // Set protocol (1 = Report Protocol)
    usb_control_transfer(hid->usb_device,
                        USB_REQ_TYPE_CLASS | USB_REQ_TYPE_INTERFACE,
                        HID_REQ_SET_PROTOCOL,
                        1,  // Report protocol
                        hid->interface_num,
                        NULL, 0);
    
    // Allocate interrupt transfer
    size_t report_size = (hid->type == HID_TYPE_KEYBOARD) ? 8 : 
                        (hid->type == HID_TYPE_MOUSE) ? hid->mouse.report_size : 64;
    
    hid->interrupt_transfer = usb_alloc_transfer(report_size);
    if (!hid->interrupt_transfer) {
        return -1;
    }
    
    hid->interrupt_transfer->device = hid->usb_device;
    hid->interrupt_transfer->endpoint = hid->interrupt_endpoint;
    hid->interrupt_transfer->type = USB_TRANSFER_INTERRUPT;
    hid->interrupt_transfer->callback = hid_interrupt_callback;
    hid->interrupt_transfer->context = hid;
    
    // Submit initial transfer
    usb_submit_transfer(hid->interrupt_transfer);
    
    return 0;
}

// =============================================================================
// Driver Interface
// =============================================================================

static void* usb_hid_probe(device_node_t* node) {
    usb_device_info_t* usb_info = (usb_device_info_t*)node->bus_specific_data;
    
    // Check for HID class
    if (usb_info->interface_class != USB_CLASS_HID) {
        return NULL;
    }
    
    usb_hid_device_t* hid = flux_allocate(NULL, sizeof(usb_hid_device_t),
                                         FLUX_ALLOC_KERNEL | FLUX_ALLOC_ZERO);
    if (!hid) {
        return NULL;
    }
    
    hid->usb_device = node;
    hid->interface_num = usb_info->interface_num;
    hid->interrupt_endpoint = usb_info->interrupt_endpoint;
    spinlock_init(&hid->lock);
    
    // Determine device type based on interface protocol
    switch (usb_info->interface_protocol) {
        case HID_PROTOCOL_KEYBOARD:
            hid->type = HID_TYPE_KEYBOARD;
            break;
        case HID_PROTOCOL_MOUSE:
            hid->type = HID_TYPE_MOUSE;
            hid->mouse.screen_width = 1024;
            hid->mouse.screen_height = 768;
            hid->mouse.x = 512;
            hid->mouse.y = 384;
            break;
        default:
            hid->type = HID_TYPE_GENERIC;
            break;
    }
    
    // Initialize device
    if (hid_init_device(hid) != 0) {
        flux_free(hid);
        return NULL;
    }
    
    // Add to global list
    spinlock_acquire(&g_hid_lock);
    g_hid_devices[g_hid_device_count++] = hid;
    spinlock_release(&g_hid_lock);
    
    return hid;
}

static int usb_hid_attach(device_handle_t* handle) {
    usb_hid_device_t* hid = (usb_hid_device_t*)handle->driver_data;
    hid->state = HID_STATE_ACTIVE;
    return 0;
}

static void usb_hid_detach(device_handle_t* handle) {
    usb_hid_device_t* hid = (usb_hid_device_t*)handle->driver_data;
    
    // Cancel interrupt transfer
    if (hid->interrupt_transfer) {
        usb_cancel_transfer(hid->interrupt_transfer);
        usb_free_transfer(hid->interrupt_transfer);
    }
    
    hid->state = HID_STATE_DISCONNECTED;
}

// Driver registration
static resonance_driver_t usb_hid_driver = {
    .name = "usb-hid",
    .class_code = USB_CLASS_HID,
    .probe = usb_hid_probe,
    .attach = usb_hid_attach,
    .detach = usb_hid_detach
};

void usb_hid_init(void) {
    resonance_register_driver(&usb_hid_driver);
}
