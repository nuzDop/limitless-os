/*
 * USB HID Driver Header
 * Human Interface Device definitions for USB
 */

#ifndef USB_HID_H
#define USB_HID_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "../resonance.h"

// =============================================================================
// USB HID Constants
// =============================================================================

#define MAX_HID_DEVICES         32
#define HID_KEYBOARD_BUFFER_SIZE 256
#define HID_EVENT_QUEUE_SIZE    256

// USB Class Codes
#define USB_CLASS_HID           0x03

// HID Interface Protocols
#define HID_PROTOCOL_NONE       0x00
#define HID_PROTOCOL_KEYBOARD   0x01
#define HID_PROTOCOL_MOUSE      0x02

// HID Descriptor Types
#define USB_DESC_HID            0x21
#define USB_DESC_HID_REPORT     0x22
#define USB_DESC_HID_PHYSICAL   0x23

// HID Class Requests
#define HID_REQ_GET_REPORT      0x01
#define HID_REQ_GET_IDLE        0x02
#define HID_REQ_GET_PROTOCOL    0x03
#define HID_REQ_SET_REPORT      0x09
#define HID_REQ_SET_IDLE        0x0A
#define HID_REQ_SET_PROTOCOL    0x0B

// HID Report Types
#define HID_REPORT_INPUT        0x01
#define HID_REPORT_OUTPUT       0x02
#define HID_REPORT_FEATURE      0x03

// HID Usage Pages
#define HID_USAGE_PAGE_GENERIC_DESKTOP 0x01
#define HID_USAGE_PAGE_SIMULATION      0x02
#define HID_USAGE_PAGE_VR              0x03
#define HID_USAGE_PAGE_SPORT           0x04
#define HID_USAGE_PAGE_GAME            0x05
#define HID_USAGE_PAGE_KEYBOARD        0x07
#define HID_USAGE_PAGE_LED             0x08
#define HID_USAGE_PAGE_BUTTON          0x09
#define HID_USAGE_PAGE_CONSUMER        0x0C

// HID Generic Desktop Usages
#define HID_USAGE_POINTER       0x01
#define HID_USAGE_MOUSE         0x02
#define HID_USAGE_JOYSTICK      0x04
#define HID_USAGE_GAMEPAD       0x05
#define HID_USAGE_KEYBOARD      0x06
#define HID_USAGE_KEYPAD        0x07
#define HID_USAGE_X             0x30
#define HID_USAGE_Y             0x31
#define HID_USAGE_Z             0x32
#define HID_USAGE_RX            0x33
#define HID_USAGE_RY            0x34
#define HID_USAGE_RZ            0x35
#define HID_USAGE_WHEEL         0x38

// USB Transfer Types
#define USB_TRANSFER_CONTROL    0x00
#define USB_TRANSFER_ISOCHRONOUS 0x01
#define USB_TRANSFER_BULK       0x02
#define USB_TRANSFER_INTERRUPT  0x03

// USB Request Types
#define USB_REQ_TYPE_STANDARD   0x00
#define USB_REQ_TYPE_CLASS      0x20
#define USB_REQ_TYPE_VENDOR     0x40
#define USB_REQ_TYPE_INTERFACE  0x01

// USB Transfer Status
#define USB_TRANSFER_COMPLETED  0x00
#define USB_TRANSFER_ERROR      0x01
#define USB_TRANSFER_TIMEOUT    0x02
#define USB_TRANSFER_CANCELLED  0x03
#define USB_TRANSFER_STALL      0x04
#define USB_TRANSFER_NO_DEVICE  0x05
#define USB_TRANSFER_OVERFLOW   0x06

// Mouse event types (shared with ps2_mouse.h)
#define MOUSE_EVENT_MOVE        0x01
#define MOUSE_EVENT_BUTTON_DOWN 0x02
#define MOUSE_EVENT_BUTTON_UP   0x03
#define MOUSE_EVENT_SCROLL      0x04

#define MOUSE_BUTTON_LEFT       0x01
#define MOUSE_BUTTON_RIGHT      0x02
#define MOUSE_BUTTON_MIDDLE     0x04

// =============================================================================
// USB HID Data Structures
// =============================================================================

// USB HID Descriptor
typedef struct __attribute__((packed)) {
    uint8_t  bLength;
    uint8_t  bDescriptorType;
    uint16_t bcdHID;
    uint8_t  bCountryCode;
    uint8_t  bNumDescriptors;
    uint8_t  bReportDescriptorType;
    uint16_t report_desc_length;
} usb_hid_descriptor_t;

// USB Device Info
typedef struct {
    uint8_t address;
    uint8_t configuration;
    uint8_t interface_num;
    uint8_t interface_class;
    uint8_t interface_subclass;
    uint8_t interface_protocol;
    uint8_t interrupt_endpoint;
    uint16_t max_packet_size;
} usb_device_info_t;

// USB Transfer
typedef struct usb_transfer {
    void* device;
    uint8_t endpoint;
    uint8_t type;
    uint8_t* data;
    size_t length;
    size_t actual_length;
    uint8_t status;
    void (*callback)(struct usb_transfer* transfer);
    void* context;
} usb_transfer_t;

// Mouse Event (shared with ps2_mouse.h)
typedef struct {
    uint8_t type;
    uint8_t button;
    uint8_t buttons;
    int32_t x;
    int32_t y;
    int16_t dx;
    int16_t dy;
    int8_t dz;
    uint64_t timestamp;
} mouse_event_t;

// HID Device Types
typedef enum {
    HID_TYPE_GENERIC = 0,
    HID_TYPE_KEYBOARD,
    HID_TYPE_MOUSE,
    HID_TYPE_GAMEPAD,
    HID_TYPE_JOYSTICK,
    HID_TYPE_TABLET,
    HID_TYPE_TOUCHPAD
} hid_device_type_t;

// HID Device State
typedef enum {
    HID_STATE_DISCONNECTED = 0,
    HID_STATE_INITIALIZING,
    HID_STATE_ACTIVE,
    HID_STATE_SUSPENDED,
    HID_STATE_ERROR
} hid_device_state_t;

// HID Keyboard State
typedef struct {
    bool ctrl_pressed;
    bool shift_pressed;
    bool alt_pressed;
    bool gui_pressed;
    bool caps_lock;
    bool num_lock;
    bool scroll_lock;
    
    uint8_t prev_keys[6];
    uint8_t buffer[HID_KEYBOARD_BUFFER_SIZE];
    uint32_t buffer_read;
    uint32_t buffer_write;
    
    uint8_t report_size;
    uint64_t keys_pressed;
} hid_keyboard_t;

// HID Mouse State
typedef struct {
    int32_t x;
    int32_t y;
    bool left_button;
    bool right_button;
    bool middle_button;
    
    uint32_t screen_width;
    uint32_t screen_height;
    
    mouse_event_t event_queue[HID_EVENT_QUEUE_SIZE];
    uint32_t event_read;
    uint32_t event_write;
    
    uint8_t report_size;
    uint64_t packets_received;
} hid_mouse_t;

// HID Gamepad State
typedef struct {
    int16_t left_stick_x;
    int16_t left_stick_y;
    int16_t right_stick_x;
    int16_t right_stick_y;
    uint8_t left_trigger;
    uint8_t right_trigger;
    uint32_t buttons;
    uint8_t dpad;
} hid_gamepad_t;

// USB HID Device
typedef struct {
    void* usb_device;
    hid_device_type_t type;
    hid_device_state_t state;
    
    uint8_t interface_num;
    uint8_t interrupt_endpoint;
    bool uses_report_ids;
    
    union {
        hid_keyboard_t keyboard;
        hid_mouse_t mouse;
        hid_gamepad_t gamepad;
    };
    
    usb_transfer_t* interrupt_transfer;
    uint32_t waiting_client;
    
    spinlock_t lock;
} usb_hid_device_t;

// =============================================================================
// Function Prototypes
// =============================================================================

void usb_hid_init(void);

// Device enumeration
uint32_t usb_hid_get_device_count(void);
usb_hid_device_t* usb_hid_get_device(uint32_t index);
usb_hid_device_t* usb_hid_get_keyboard(uint32_t index);
usb_hid_device_t* usb_hid_get_mouse(uint32_t index);

// Keyboard operations
int usb_hid_keyboard_read_key(usb_hid_device_t* hid);
bool usb_hid_keyboard_has_data(usb_hid_device_t* hid);

// Mouse operations
bool usb_hid_mouse_get_event(usb_hid_device_t* hid, mouse_event_t* event);
void usb_hid_mouse_get_position(usb_hid_device_t* hid, int32_t* x, int32_t* y);
void usb_hid_mouse_set_screen_size(usb_hid_device_t* hid, uint32_t width, uint32_t height);

// Gamepad operations
void usb_hid_gamepad_get_state(usb_hid_device_t* hid, hid_gamepad_t* state);

// USB operations (provided by USB subsystem)
int usb_get_descriptor(void* device, uint8_t type, uint8_t index,
                       void* buffer, size_t length);
int usb_control_transfer(void* device, uint8_t request_type, uint8_t request,
                        uint16_t value, uint16_t index, void* data, size_t length);
usb_transfer_t* usb_alloc_transfer(size_t data_size);
void usb_free_transfer(usb_transfer_t* transfer);
int usb_submit_transfer(usb_transfer_t* transfer);
void usb_cancel_transfer(usb_transfer_t* transfer);

// Helper functions
static inline void hid_add_key_to_buffer(usb_hid_device_t* hid, uint8_t key) {
    hid_keyboard_t* kbd = &hid->keyboard;
    uint32_t next_write = (kbd->buffer_write + 1) % HID_KEYBOARD_BUFFER_SIZE;
    if (next_write != kbd->buffer_read) {
        kbd->buffer[kbd->buffer_write] = key;
        kbd->buffer_write = next_write;
    }
}

static inline void hid_add_mouse_event(usb_hid_device_t* hid, mouse_event_t* event) {
    hid_mouse_t* mouse = &hid->mouse;
    uint32_t next_write = (mouse->event_write + 1) % HID_EVENT_QUEUE_SIZE;
    if (next_write != mouse->event_read) {
        mouse->event_queue[mouse->event_write] = *event;
        mouse->event_write = next_write;
    }
}

#endif /* USB_HID_H */
