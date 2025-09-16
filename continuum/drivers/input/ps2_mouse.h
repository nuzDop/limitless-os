/*
 * PS/2 Mouse Driver Header
 * Standard mouse input definitions
 */

#ifndef PS2_MOUSE_H
#define PS2_MOUSE_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "../resonance.h"

// =============================================================================
// PS/2 Controller (shared with keyboard)
// =============================================================================

#define PS2_DATA_PORT               0x60
#define PS2_STATUS_PORT             0x64
#define PS2_CMD_PORT                0x64

#define PS2_STATUS_OUTPUT_FULL      0x01
#define PS2_STATUS_INPUT_FULL       0x02

#define PS2_CMD_READ_CONFIG         0x20
#define PS2_CMD_WRITE_CONFIG        0x60
#define PS2_CMD_DISABLE_PORT2       0xA7
#define PS2_CMD_ENABLE_PORT2        0xA8
#define PS2_CMD_TEST_PORT2          0xA9
#define PS2_CMD_WRITE_PORT2_INPUT   0xD4

#define PS2_CONFIG_PORT2_INT        0x02
#define PS2_CONFIG_PORT2_CLOCK      0x20

// =============================================================================
// Mouse Commands
// =============================================================================

#define MOUSE_CMD_RESET             0xFF
#define MOUSE_CMD_RESEND            0xFE
#define MOUSE_CMD_SET_DEFAULTS      0xF6
#define MOUSE_CMD_DISABLE           0xF5
#define MOUSE_CMD_ENABLE            0xF4
#define MOUSE_CMD_SET_SAMPLE_RATE   0xF3
#define MOUSE_CMD_GET_ID            0xF2
#define MOUSE_CMD_SET_REMOTE_MODE   0xF0
#define MOUSE_CMD_SET_WRAP_MODE     0xEE
#define MOUSE_CMD_RESET_WRAP_MODE   0xEC
#define MOUSE_CMD_READ_DATA         0xEB
#define MOUSE_CMD_SET_STREAM_MODE   0xEA
#define MOUSE_CMD_STATUS_REQUEST    0xE9
#define MOUSE_CMD_SET_RESOLUTION    0xE8
#define MOUSE_CMD_SET_SCALING_2_1   0xE7
#define MOUSE_CMD_SET_SCALING_1_1   0xE6

// Mouse Responses
#define MOUSE_RESPONSE_ACK          0xFA
#define MOUSE_RESPONSE_RESEND       0xFE

// =============================================================================
// Mouse Packet Bits
// =============================================================================

#define MOUSE_PACKET_LEFT_BTN       0x01
#define MOUSE_PACKET_RIGHT_BTN      0x02
#define MOUSE_PACKET_MIDDLE_BTN     0x04
#define MOUSE_PACKET_VALID          0x08
#define MOUSE_PACKET_X_SIGN         0x10
#define MOUSE_PACKET_Y_SIGN         0x20
#define MOUSE_PACKET_X_OVERFLOW     0x40
#define MOUSE_PACKET_Y_OVERFLOW     0x80

// =============================================================================
// Mouse Events
// =============================================================================

#define MOUSE_EVENT_MOVE            0x01
#define MOUSE_EVENT_BUTTON_DOWN     0x02
#define MOUSE_EVENT_BUTTON_UP       0x03
#define MOUSE_EVENT_SCROLL          0x04

#define MOUSE_BUTTON_LEFT           0x01
#define MOUSE_BUTTON_RIGHT          0x02
#define MOUSE_BUTTON_MIDDLE         0x04
#define MOUSE_BUTTON_EXTRA1         0x08
#define MOUSE_BUTTON_EXTRA2         0x10

#define MOUSE_EVENT_QUEUE_SIZE      256
#define IRQ_MOUSE                   12

// =============================================================================
// Data Structures
// =============================================================================

// Mouse Event
typedef struct {
    uint8_t type;
    uint8_t button;
    uint8_t buttons;        // Current button state
    int32_t x;             // Absolute position
    int32_t y;
    int16_t dx;            // Relative movement
    int16_t dy;
    int8_t dz;             // Scroll wheel
    uint64_t timestamp;
} mouse_event_t;

// Mouse State
typedef struct {
    // Current position
    int32_t x;
    int32_t y;
    
    // Button state
    bool left_button;
    bool right_button;
    bool middle_button;
    bool extra_button1;
    bool extra_button2;
    
    // Screen bounds
    uint32_t screen_width;
    uint32_t screen_height;
    
    // Packet buffer
    uint8_t packet[4];
    uint8_t packet_index;
    uint8_t packet_size;
    
    // Features
    bool has_scroll_wheel;
    bool has_extra_buttons;
    
    // Event queue
    mouse_event_t event_queue[MOUSE_EVENT_QUEUE_SIZE];
    uint32_t event_read;
    uint32_t event_write;
    
    // IPC client waiting for events
    uint32_t waiting_client;
    
    // Statistics
    uint64_t packets_received;
    uint64_t interrupts;
} ps2_mouse_t;

// =============================================================================
// Function Prototypes
// =============================================================================

int ps2_mouse_init(void);
void ps2_mouse_register(void);

bool ps2_mouse_get_event(mouse_event_t* event);
void ps2_mouse_flush_events(void);

void ps2_mouse_get_position(int32_t* x, int32_t* y);
void ps2_mouse_set_position(int32_t x, int32_t y);
uint8_t ps2_mouse_get_buttons(void);

void ps2_mouse_set_screen_size(uint32_t width, uint32_t height);
void ps2_mouse_set_sensitivity(uint8_t sensitivity);
void ps2_mouse_set_acceleration(bool enable);

void ps2_mouse_set_client(uint32_t client_id);

#endif /* PS2_MOUSE_H */
