/*
 * PS/2 Keyboard Driver Header
 * Standard keyboard input definitions
 */

#ifndef PS2_KEYBOARD_H
#define PS2_KEYBOARD_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "../resonance.h"

// =============================================================================
// PS/2 Controller Ports
// =============================================================================

#define PS2_DATA_PORT       0x60
#define PS2_STATUS_PORT     0x64
#define PS2_CMD_PORT        0x64

// PS/2 Status Register Bits
#define PS2_STATUS_OUTPUT_FULL      0x01
#define PS2_STATUS_INPUT_FULL       0x02
#define PS2_STATUS_SYSTEM_FLAG      0x04
#define PS2_STATUS_CMD_DATA         0x08
#define PS2_STATUS_TIMEOUT_ERROR    0x40
#define PS2_STATUS_PARITY_ERROR     0x80

// PS/2 Controller Commands
#define PS2_CMD_READ_CONFIG         0x20
#define PS2_CMD_WRITE_CONFIG        0x60
#define PS2_CMD_DISABLE_PORT2       0xA7
#define PS2_CMD_ENABLE_PORT2        0xA8
#define PS2_CMD_TEST_PORT2          0xA9
#define PS2_CMD_TEST_CONTROLLER     0xAA
#define PS2_CMD_TEST_PORT1          0xAB
#define PS2_CMD_DIAGNOSTIC_DUMP     0xAC
#define PS2_CMD_DISABLE_PORT1       0xAD
#define PS2_CMD_ENABLE_PORT1        0xAE
#define PS2_CMD_READ_INPUT_PORT     0xC0
#define PS2_CMD_READ_OUTPUT_PORT    0xD0
#define PS2_CMD_WRITE_OUTPUT_PORT   0xD1
#define PS2_CMD_WRITE_PORT1_OUTPUT  0xD2
#define PS2_CMD_WRITE_PORT2_OUTPUT  0xD3
#define PS2_CMD_WRITE_PORT2_INPUT   0xD4

// PS/2 Configuration Byte Bits
#define PS2_CONFIG_PORT1_INT        0x01
#define PS2_CONFIG_PORT2_INT        0x02
#define PS2_CONFIG_SYSTEM_FLAG      0x04
#define PS2_CONFIG_PORT1_CLOCK      0x10
#define PS2_CONFIG_PORT2_CLOCK      0x20
#define PS2_CONFIG_PORT1_TRANSLATE  0x40

// Test Results
#define PS2_TEST_PASSED             0x55
#define PS2_TEST_FAILED             0xFC

// =============================================================================
// Keyboard Commands
// =============================================================================

#define KBD_CMD_SET_LEDS            0xED
#define KBD_CMD_ECHO                0xEE
#define KBD_CMD_SET_SCANCODE        0xF0
#define KBD_CMD_IDENTIFY            0xF2
#define KBD_CMD_SET_TYPEMATIC       0xF3
#define KBD_CMD_ENABLE              0xF4
#define KBD_CMD_DISABLE             0xF5
#define KBD_CMD_SET_DEFAULTS        0xF6
#define KBD_CMD_SET_ALL_TYPEMATIC   0xF7
#define KBD_CMD_SET_ALL_MAKE_BREAK  0xF8
#define KBD_CMD_SET_ALL_MAKE        0xF9
#define KBD_CMD_SET_ALL_AUTO        0xFA
#define KBD_CMD_SET_KEY_TYPEMATIC   0xFB
#define KBD_CMD_SET_KEY_MAKE_BREAK  0xFC
#define KBD_CMD_SET_KEY_BREAK       0xFD
#define KBD_CMD_RESEND              0xFE
#define KBD_CMD_RESET               0xFF

// Keyboard Responses
#define KBD_RESPONSE_ACK            0xFA
#define KBD_RESPONSE_RESEND         0xFE
#define KBD_RESPONSE_TEST_PASSED    0xAA
#define KBD_RESPONSE_ECHO           0xEE
#define KBD_RESPONSE_ERROR          0x00

// LED Flags
#define KBD_LED_SCROLL_LOCK         0x01
#define KBD_LED_NUM_LOCK            0x02
#define KBD_LED_CAPS_LOCK           0x04

// =============================================================================
// Special Keys
// =============================================================================

#define KEY_ESCAPE      0x1B
#define KEY_BACKSPACE   0x08
#define KEY_TAB         0x09
#define KEY_ENTER       0x0A
#define KEY_CTRL        0x80
#define KEY_SHIFT       0x81
#define KEY_ALT         0x82
#define KEY_CAPSLOCK    0x83
#define KEY_NUMLOCK     0x84
#define KEY_SCROLLLOCK  0x85
#define KEY_F1          0x86
#define KEY_F2          0x87
#define KEY_F3          0x88
#define KEY_F4          0x89
#define KEY_F5          0x8A
#define KEY_F6          0x8B
#define KEY_F7          0x8C
#define KEY_F8          0x8D
#define KEY_F9          0x8E
#define KEY_F10         0x8F
#define KEY_F11         0x90
#define KEY_F12         0x91
#define KEY_HOME        0x92
#define KEY_END         0x93
#define KEY_INSERT      0x94
#define KEY_DELETE      0x95
#define KEY_PAGEUP      0x96
#define KEY_PAGEDOWN    0x97
#define KEY_LEFT        0x98
#define KEY_RIGHT       0x99
#define KEY_UP          0x9A
#define KEY_DOWN        0x9B
#define KEY_PAUSE       0x9C
#define KEY_PRINT       0x9D
#define KEY_SYSRQ       0x9E
#define KEY_BREAK       0x9F

// =============================================================================
// Data Structures
// =============================================================================

#define KBD_BUFFER_SIZE 256
#define IRQ_KEYBOARD    1

// Keyboard State
typedef struct {
    // Modifier keys
    bool shift_pressed;
    bool ctrl_pressed;
    bool alt_pressed;
    bool caps_lock;
    bool num_lock;
    bool scroll_lock;
    
    // Extended scan code flag
    bool extended;
    
    // Keyboard buffer
    uint8_t buffer[KBD_BUFFER_SIZE];
    uint32_t buffer_read;
    uint32_t buffer_write;
    
    // IPC client waiting for input
    uint32_t waiting_client;
    
    // Statistics
    uint64_t keys_pressed;
    uint64_t interrupts;
} ps2_keyboard_t;

// Interrupt frame
typedef struct {
    uint64_t rip;
    uint64_t cs;
    uint64_t rflags;
    uint64_t rsp;
    uint64_t ss;
} interrupt_frame_t;

// =============================================================================
// Function Prototypes
// =============================================================================

int ps2_keyboard_init(void);
void ps2_keyboard_register(void);

int ps2_keyboard_read_key(void);
bool ps2_keyboard_has_data(void);
void ps2_keyboard_flush_buffer(void);

void ps2_keyboard_set_client(uint32_t client_id);
bool ps2_keyboard_is_modifier_pressed(uint8_t modifier);

// Helper functions
void pic_send_eoi(uint8_t irq);
void interrupt_register(uint8_t vector, void (*handler)(interrupt_frame_t*));
uint64_t continuum_get_time(void);

#endif /* PS2_KEYBOARD_H */
