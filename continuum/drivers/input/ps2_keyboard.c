/*
 * PS/2 Keyboard Driver for Continuum Kernel
 * Standard keyboard input driver
 */

#include "ps2_keyboard.h"
#include "../resonance.h"
#include "../../flux_memory.h"
#include "../../conduit_ipc.h"

// =============================================================================
// Global Keyboard State
// =============================================================================

static ps2_keyboard_t g_keyboard;
static spinlock_t g_kbd_lock = SPINLOCK_INIT;

// Keyboard scan code to ASCII mapping (US layout)
static const uint8_t scancode_to_ascii[128] = {
    0,  27, '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=', '\b',
    '\t', 'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', '\n',
    0, 'a', 's', 'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'', '`',
    0, '\\', 'z', 'x', 'c', 'v', 'b', 'n', 'm', ',', '.', '/', 0,
    '*', 0, ' ', 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    '-', 0, 0, 0, '+', 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
};

static const uint8_t scancode_to_ascii_shift[128] = {
    0,  27, '!', '@', '#', '$', '%', '^', '&', '*', '(', ')', '_', '+', '\b',
    '\t', 'Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I', 'O', 'P', '{', '}', '\n',
    0, 'A', 'S', 'D', 'F', 'G', 'H', 'J', 'K', 'L', ':', '"', '~',
    0, '|', 'Z', 'X', 'C', 'V', 'B', 'N', 'M', '<', '>', '?', 0,
    '*', 0, ' ', 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
    '-', 0, 0, 0, '+', 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
};

// =============================================================================
// PS/2 Controller Communication
// =============================================================================

static void ps2_wait_input(void) {
    uint64_t timeout = continuum_get_time() + 100000;  // 100ms timeout
    while (continuum_get_time() < timeout) {
        if (!(inb(PS2_STATUS_PORT) & PS2_STATUS_INPUT_FULL)) {
            return;
        }
        io_wait();
    }
}

static void ps2_wait_output(void) {
    uint64_t timeout = continuum_get_time() + 100000;
    while (continuum_get_time() < timeout) {
        if (inb(PS2_STATUS_PORT) & PS2_STATUS_OUTPUT_FULL) {
            return;
        }
        io_wait();
    }
}

static uint8_t ps2_read_data(void) {
    ps2_wait_output();
    return inb(PS2_DATA_PORT);
}

static void ps2_write_command(uint8_t cmd) {
    ps2_wait_input();
    outb(PS2_CMD_PORT, cmd);
}

static void ps2_write_data(uint8_t data) {
    ps2_wait_input();
    outb(PS2_DATA_PORT, data);
}

// =============================================================================
// Keyboard Commands
// =============================================================================

static int ps2_keyboard_send_command(uint8_t cmd) {
    // Send command to keyboard
    ps2_write_data(cmd);
    
    // Wait for ACK
    uint8_t response = ps2_read_data();
    if (response == KBD_RESPONSE_ACK) {
        return 0;
    } else if (response == KBD_RESPONSE_RESEND) {
        // Retry once
        ps2_write_data(cmd);
        response = ps2_read_data();
        return (response == KBD_RESPONSE_ACK) ? 0 : -1;
    }
    
    return -1;
}

static void ps2_keyboard_set_leds(uint8_t leds) {
    ps2_keyboard_send_command(KBD_CMD_SET_LEDS);
    ps2_keyboard_send_command(leds);
}

static void ps2_keyboard_set_typematic(uint8_t rate) {
    ps2_keyboard_send_command(KBD_CMD_SET_TYPEMATIC);
    ps2_keyboard_send_command(rate);
}

// =============================================================================
// Scan Code Processing
// =============================================================================

static void ps2_keyboard_process_scancode(uint8_t scancode) {
    spinlock_acquire(&g_kbd_lock);
    
    // Handle extended scan codes
    if (scancode == 0xE0) {
        g_keyboard.extended = true;
        spinlock_release(&g_kbd_lock);
        return;
    }
    
    bool key_release = (scancode & 0x80) != 0;
    scancode &= 0x7F;
    
    if (g_keyboard.extended) {
        // Handle extended keys
        g_keyboard.extended = false;
        
        switch (scancode) {
            case 0x48: // Up arrow
                if (!key_release) {
                    ps2_keyboard_add_to_buffer(KEY_UP);
                }
                break;
            case 0x50: // Down arrow
                if (!key_release) {
                    ps2_keyboard_add_to_buffer(KEY_DOWN);
                }
                break;
            case 0x4B: // Left arrow
                if (!key_release) {
                    ps2_keyboard_add_to_buffer(KEY_LEFT);
                }
                break;
            case 0x4D: // Right arrow
                if (!key_release) {
                    ps2_keyboard_add_to_buffer(KEY_RIGHT);
                }
                break;
            case 0x47: // Home
                if (!key_release) {
                    ps2_keyboard_add_to_buffer(KEY_HOME);
                }
                break;
            case 0x4F: // End
                if (!key_release) {
                    ps2_keyboard_add_to_buffer(KEY_END);
                }
                break;
            case 0x49: // Page Up
                if (!key_release) {
                    ps2_keyboard_add_to_buffer(KEY_PAGEUP);
                }
                break;
            case 0x51: // Page Down
                if (!key_release) {
                    ps2_keyboard_add_to_buffer(KEY_PAGEDOWN);
                }
                break;
            case 0x53: // Delete
                if (!key_release) {
                    ps2_keyboard_add_to_buffer(KEY_DELETE);
                }
                break;
            case 0x52: // Insert
                if (!key_release) {
                    ps2_keyboard_add_to_buffer(KEY_INSERT);
                }
                break;
        }
    } else {
        // Handle regular keys
        switch (scancode) {
            case 0x2A: // Left Shift
            case 0x36: // Right Shift
                g_keyboard.shift_pressed = !key_release;
                break;
                
            case 0x1D: // Left Ctrl
                g_keyboard.ctrl_pressed = !key_release;
                break;
                
            case 0x38: // Left Alt
                g_keyboard.alt_pressed = !key_release;
                break;
                
            case 0x3A: // Caps Lock
                if (!key_release) {
                    g_keyboard.caps_lock = !g_keyboard.caps_lock;
                    ps2_keyboard_update_leds();
                }
                break;
                
            case 0x45: // Num Lock
                if (!key_release) {
                    g_keyboard.num_lock = !g_keyboard.num_lock;
                    ps2_keyboard_update_leds();
                }
                break;
                
            case 0x46: // Scroll Lock
                if (!key_release) {
                    g_keyboard.scroll_lock = !g_keyboard.scroll_lock;
                    ps2_keyboard_update_leds();
                }
                break;
                
            default:
                if (!key_release && scancode < 128) {
                    // Regular key press
                    uint8_t ascii;
                    
                    if (g_keyboard.shift_pressed) {
                        ascii = scancode_to_ascii_shift[scancode];
                    } else {
                        ascii = scancode_to_ascii[scancode];
                    }
                    
                    // Apply Caps Lock for letters
                    if (g_keyboard.caps_lock && ascii >= 'a' && ascii <= 'z') {
                        ascii = ascii - 'a' + 'A';
                    } else if (g_keyboard.caps_lock && ascii >= 'A' && ascii <= 'Z') {
                        ascii = ascii - 'A' + 'a';
                    }
                    
                    // Handle Ctrl combinations
                    if (g_keyboard.ctrl_pressed && ascii >= 'a' && ascii <= 'z') {
                        ascii = ascii - 'a' + 1;  // Ctrl+A = 1, etc.
                    }
                    
                    if (ascii != 0) {
                        ps2_keyboard_add_to_buffer(ascii);
                    }
                }
                break;
        }
    }
    
    spinlock_release(&g_kbd_lock);
}

// =============================================================================
// Keyboard Buffer Management
// =============================================================================

static void ps2_keyboard_add_to_buffer(uint8_t key) {
    uint32_t next_write = (g_keyboard.buffer_write + 1) % KBD_BUFFER_SIZE;
    
    if (next_write != g_keyboard.buffer_read) {
        g_keyboard.buffer[g_keyboard.buffer_write] = key;
        g_keyboard.buffer_write = next_write;
        
        // Send IPC message if client is waiting
        if (g_keyboard.waiting_client) {
            conduit_message_t msg = {
                .type = MSG_KEYBOARD_INPUT,
                .data = key,
                .size = 1
            };
            conduit_send(g_keyboard.waiting_client, &msg);
            g_keyboard.waiting_client = 0;
        }
    }
}

int ps2_keyboard_read_key(void) {
    spinlock_acquire(&g_kbd_lock);
    
    if (g_keyboard.buffer_read == g_keyboard.buffer_write) {
        spinlock_release(&g_kbd_lock);
        return -1;  // Buffer empty
    }
    
    uint8_t key = g_keyboard.buffer[g_keyboard.buffer_read];
    g_keyboard.buffer_read = (g_keyboard.buffer_read + 1) % KBD_BUFFER_SIZE;
    
    spinlock_release(&g_kbd_lock);
    return key;
}

bool ps2_keyboard_has_data(void) {
    spinlock_acquire(&g_kbd_lock);
    bool has_data = (g_keyboard.buffer_read != g_keyboard.buffer_write);
    spinlock_release(&g_kbd_lock);
    return has_data;
}

// =============================================================================
// LED Control
// =============================================================================

static void ps2_keyboard_update_leds(void) {
    uint8_t leds = 0;
    
    if (g_keyboard.scroll_lock) leds |= KBD_LED_SCROLL_LOCK;
    if (g_keyboard.num_lock) leds |= KBD_LED_NUM_LOCK;
    if (g_keyboard.caps_lock) leds |= KBD_LED_CAPS_LOCK;
    
    ps2_keyboard_set_leds(leds);
}

// =============================================================================
// Interrupt Handler
// =============================================================================

static void ps2_keyboard_interrupt(interrupt_frame_t* frame) {
    uint8_t status = inb(PS2_STATUS_PORT);
    
    if (status & PS2_STATUS_OUTPUT_FULL) {
        uint8_t scancode = inb(PS2_DATA_PORT);
        ps2_keyboard_process_scancode(scancode);
    }
    
    // Send EOI to interrupt controller
    pic_send_eoi(1);
}

// =============================================================================
// Initialization
// =============================================================================

int ps2_keyboard_init(void) {
    // Initialize keyboard state
    memset(&g_keyboard, 0, sizeof(ps2_keyboard_t));
    g_keyboard.num_lock = true;  // Num Lock on by default
    
    // Disable PS/2 ports
    ps2_write_command(PS2_CMD_DISABLE_PORT1);
    ps2_write_command(PS2_CMD_DISABLE_PORT2);
    
    // Flush output buffer
    while (inb(PS2_STATUS_PORT) & PS2_STATUS_OUTPUT_FULL) {
        inb(PS2_DATA_PORT);
        io_wait();
    }
    
    // Set controller configuration
    ps2_write_command(PS2_CMD_READ_CONFIG);
    uint8_t config = ps2_read_data();
    config &= ~(PS2_CONFIG_PORT1_INT | PS2_CONFIG_PORT2_INT | PS2_CONFIG_PORT1_TRANSLATE);
    config |= PS2_CONFIG_PORT1_INT;  // Enable keyboard interrupts
    ps2_write_command(PS2_CMD_WRITE_CONFIG);
    ps2_write_data(config);
    
    // Perform controller self-test
    ps2_write_command(PS2_CMD_TEST_CONTROLLER);
    if (ps2_read_data() != PS2_TEST_PASSED) {
        return -1;
    }
    
    // Enable keyboard port
    ps2_write_command(PS2_CMD_ENABLE_PORT1);
    
    // Reset keyboard
    if (ps2_keyboard_send_command(KBD_CMD_RESET) != 0) {
        return -1;
    }
    
    // Wait for self-test result
    uint8_t result = ps2_read_data();
    if (result != 0xAA) {
        return -1;
    }
    
    // Set scan code set 2
    ps2_keyboard_send_command(KBD_CMD_SET_SCANCODE);
    ps2_keyboard_send_command(2);
    
    // Set typematic rate (fastest)
    ps2_keyboard_set_typematic(0x00);
    
    // Enable keyboard
    ps2_keyboard_send_command(KBD_CMD_ENABLE);
    
    // Update LEDs
    ps2_keyboard_update_leds();
    
    // Register interrupt handler
    interrupt_register(IRQ_KEYBOARD, ps2_keyboard_interrupt);
    
    return 0;
}

// =============================================================================
// Driver Interface
// =============================================================================

static void* ps2_keyboard_probe(device_node_t* node) {
    // PS/2 keyboard is typically always present on PC systems
    return &g_keyboard;
}

static int ps2_keyboard_attach(device_handle_t* handle) {
    return ps2_keyboard_init();
}

static void ps2_keyboard_detach(device_handle_t* handle) {
    // Disable keyboard interrupts
    ps2_write_command(PS2_CMD_DISABLE_PORT1);
}

// Driver registration
static resonance_driver_t ps2_keyboard_driver = {
    .name = "ps2-keyboard",
    .probe = ps2_keyboard_probe,
    .attach = ps2_keyboard_attach,
    .detach = ps2_keyboard_detach
};

void ps2_keyboard_register(void) {
    resonance_register_driver(&ps2_keyboard_driver);
}
