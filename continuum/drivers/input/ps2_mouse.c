/*
 * PS/2 Mouse Driver for Continuum Kernel
 * Standard mouse input driver
 */

#include "ps2_mouse.h"
#include "../resonance.h"
#include "../../flux_memory.h"
#include "../../conduit_ipc.h"

// =============================================================================
// Global Mouse State
// =============================================================================

static ps2_mouse_t g_mouse;
static spinlock_t g_mouse_lock = SPINLOCK_INIT;

// =============================================================================
// PS/2 Mouse Communication
// =============================================================================

static void ps2_mouse_wait_input(void) {
    uint64_t timeout = continuum_get_time() + 100000;
    while (continuum_get_time() < timeout) {
        if (!(inb(PS2_STATUS_PORT) & PS2_STATUS_INPUT_FULL)) {
            return;
        }
        io_wait();
    }
}

static void ps2_mouse_wait_output(void) {
    uint64_t timeout = continuum_get_time() + 100000;
    while (continuum_get_time() < timeout) {
        if (inb(PS2_STATUS_PORT) & PS2_STATUS_OUTPUT_FULL) {
            return;
        }
        io_wait();
    }
}

static uint8_t ps2_mouse_read(void) {
    ps2_mouse_wait_output();
    return inb(PS2_DATA_PORT);
}

static void ps2_mouse_write(uint8_t data) {
    // Tell controller we're sending a command to the mouse
    ps2_mouse_wait_input();
    outb(PS2_CMD_PORT, PS2_CMD_WRITE_PORT2_INPUT);
    
    // Send the data
    ps2_mouse_wait_input();
    outb(PS2_DATA_PORT, data);
}

static int ps2_mouse_send_command(uint8_t cmd) {
    ps2_mouse_write(cmd);
    
    // Wait for ACK
    uint8_t response = ps2_mouse_read();
    if (response == MOUSE_RESPONSE_ACK) {
        return 0;
    } else if (response == MOUSE_RESPONSE_RESEND) {
        // Retry once
        ps2_mouse_write(cmd);
        response = ps2_mouse_read();
        return (response == MOUSE_RESPONSE_ACK) ? 0 : -1;
    }
    
    return -1;
}

// =============================================================================
// Mouse Packet Processing
// =============================================================================

static void ps2_mouse_process_packet(void) {
    uint8_t status = g_mouse.packet[0];
    int16_t x_movement = g_mouse.packet[1];
    int16_t y_movement = g_mouse.packet[2];
    int8_t z_movement = 0;
    
    // Check for valid packet
    if (!(status & MOUSE_PACKET_VALID)) {
        return;
    }
    
    // Handle sign extension
    if (status & MOUSE_PACKET_X_SIGN) {
        x_movement |= 0xFF00;
    }
    if (status & MOUSE_PACKET_Y_SIGN) {
        y_movement |= 0xFF00;
    }
    
    // Handle overflow
    if (status & MOUSE_PACKET_X_OVERFLOW) {
        x_movement = (status & MOUSE_PACKET_X_SIGN) ? -256 : 255;
    }
    if (status & MOUSE_PACKET_Y_OVERFLOW) {
        y_movement = (status & MOUSE_PACKET_Y_SIGN) ? -256 : 255;
    }
    
    // Handle scroll wheel if present
    if (g_mouse.has_scroll_wheel && g_mouse.packet_size == 4) {
        z_movement = (int8_t)g_mouse.packet[3];
        // Limit scroll to -8 to 7
        if (z_movement == -128) {
            z_movement = -8;
        } else if (z_movement > 7) {
            z_movement = 7;
        } else if (z_movement < -8) {
            z_movement = -8;
        }
    }
    
    // Update mouse state
    spinlock_acquire(&g_mouse_lock);
    
    // Update position
    g_mouse.x += x_movement;
    g_mouse.y -= y_movement;  // Y is inverted in PS/2
    
    // Clamp to screen bounds
    if (g_mouse.x < 0) g_mouse.x = 0;
    if (g_mouse.x >= g_mouse.screen_width) g_mouse.x = g_mouse.screen_width - 1;
    if (g_mouse.y < 0) g_mouse.y = 0;
    if (g_mouse.y >= g_mouse.screen_height) g_mouse.y = g_mouse.screen_height - 1;
    
    // Update buttons
    bool left_prev = g_mouse.left_button;
    bool right_prev = g_mouse.right_button;
    bool middle_prev = g_mouse.middle_button;
    
    g_mouse.left_button = (status & MOUSE_PACKET_LEFT_BTN) != 0;
    g_mouse.right_button = (status & MOUSE_PACKET_RIGHT_BTN) != 0;
    g_mouse.middle_button = (status & MOUSE_PACKET_MIDDLE_BTN) != 0;
    
    // Generate events
    mouse_event_t event = {0};
    event.x = g_mouse.x;
    event.y = g_mouse.y;
    event.dx = x_movement;
    event.dy = -y_movement;
    event.dz = z_movement;
    event.buttons = (g_mouse.left_button ? MOUSE_BUTTON_LEFT : 0) |
                   (g_mouse.right_button ? MOUSE_BUTTON_RIGHT : 0) |
                   (g_mouse.middle_button ? MOUSE_BUTTON_MIDDLE : 0);
    
    // Determine event type
    if (x_movement != 0 || y_movement != 0) {
        event.type = MOUSE_EVENT_MOVE;
        ps2_mouse_add_event(&event);
    }
    
    if (g_mouse.left_button != left_prev) {
        event.type = g_mouse.left_button ? MOUSE_EVENT_BUTTON_DOWN : MOUSE_EVENT_BUTTON_UP;
        event.button = MOUSE_BUTTON_LEFT;
        ps2_mouse_add_event(&event);
    }
    
    if (g_mouse.right_button != right_prev) {
        event.type = g_mouse.right_button ? MOUSE_EVENT_BUTTON_DOWN : MOUSE_EVENT_BUTTON_UP;
        event.button = MOUSE_BUTTON_RIGHT;
        ps2_mouse_add_event(&event);
    }
    
    if (g_mouse.middle_button != middle_prev) {
        event.type = g_mouse.middle_button ? MOUSE_EVENT_BUTTON_DOWN : MOUSE_EVENT_BUTTON_UP;
        event.button = MOUSE_BUTTON_MIDDLE;
        ps2_mouse_add_event(&event);
    }
    
    if (z_movement != 0) {
        event.type = MOUSE_EVENT_SCROLL;
        ps2_mouse_add_event(&event);
    }
    
    // Update statistics
    g_mouse.packets_received++;
    
    spinlock_release(&g_mouse_lock);
}

// =============================================================================
// Event Queue Management
// =============================================================================

static void ps2_mouse_add_event(mouse_event_t* event) {
    uint32_t next_write = (g_mouse.event_write + 1) % MOUSE_EVENT_QUEUE_SIZE;
    
    if (next_write != g_mouse.event_read) {
        g_mouse.event_queue[g_mouse.event_write] = *event;
        g_mouse.event_write = next_write;
        
        // Send IPC message if client is waiting
        if (g_mouse.waiting_client) {
            conduit_message_t msg = {
                .type = MSG_MOUSE_EVENT,
                .data = (uintptr_t)event,
                .size = sizeof(mouse_event_t)
            };
            conduit_send(g_mouse.waiting_client, &msg);
            g_mouse.waiting_client = 0;
        }
    }
}

bool ps2_mouse_get_event(mouse_event_t* event) {
    spinlock_acquire(&g_mouse_lock);
    
    if (g_mouse.event_read == g_mouse.event_write) {
        spinlock_release(&g_mouse_lock);
        return false;  // Queue empty
    }
    
    *event = g_mouse.event_queue[g_mouse.event_read];
    g_mouse.event_read = (g_mouse.event_read + 1) % MOUSE_EVENT_QUEUE_SIZE;
    
    spinlock_release(&g_mouse_lock);
    return true;
}

// =============================================================================
// Interrupt Handler
// =============================================================================

static void ps2_mouse_interrupt(interrupt_frame_t* frame) {
    uint8_t status = inb(PS2_STATUS_PORT);
    
    if ((status & PS2_STATUS_OUTPUT_FULL) && (status & 0x20)) {
        uint8_t data = inb(PS2_DATA_PORT);
        
        spinlock_acquire(&g_mouse_lock);
        
        // Add to packet buffer
        g_mouse.packet[g_mouse.packet_index] = data;
        g_mouse.packet_index++;
        
        // Check if packet is complete
        if (g_mouse.packet_index >= g_mouse.packet_size) {
            g_mouse.packet_index = 0;
            spinlock_release(&g_mouse_lock);
            ps2_mouse_process_packet();
        } else {
            spinlock_release(&g_mouse_lock);
        }
        
        g_mouse.interrupts++;
    }
    
    // Send EOI to interrupt controller
    pic_send_eoi(12);
}

// =============================================================================
// Mouse Configuration
// =============================================================================

static int ps2_mouse_detect_wheel(void) {
    // Try to enable scroll wheel (IntelliMouse protocol)
    
    // Set sample rate sequence: 200, 100, 80
    ps2_mouse_send_command(MOUSE_CMD_SET_SAMPLE_RATE);
    ps2_mouse_send_command(200);
    ps2_mouse_send_command(MOUSE_CMD_SET_SAMPLE_RATE);
    ps2_mouse_send_command(100);
    ps2_mouse_send_command(MOUSE_CMD_SET_SAMPLE_RATE);
    ps2_mouse_send_command(80);
    
    // Get device ID
    ps2_mouse_send_command(MOUSE_CMD_GET_ID);
    uint8_t id = ps2_mouse_read();
    
    if (id == 3) {
        // Mouse has scroll wheel
        g_mouse.has_scroll_wheel = true;
        g_mouse.packet_size = 4;
        return 1;
    }
    
    // Try 5-button mouse (IntelliMouse Explorer)
    ps2_mouse_send_command(MOUSE_CMD_SET_SAMPLE_RATE);
    ps2_mouse_send_command(200);
    ps2_mouse_send_command(MOUSE_CMD_SET_SAMPLE_RATE);
    ps2_mouse_send_command(200);
    ps2_mouse_send_command(MOUSE_CMD_SET_SAMPLE_RATE);
    ps2_mouse_send_command(80);
    
    ps2_mouse_send_command(MOUSE_CMD_GET_ID);
    id = ps2_mouse_read();
    
    if (id == 4) {
        // 5-button mouse with scroll wheel
        g_mouse.has_scroll_wheel = true;
        g_mouse.has_extra_buttons = true;
        g_mouse.packet_size = 4;
        return 2;
    }
    
    // Standard PS/2 mouse
    return 0;
}

// =============================================================================
// Initialization
// =============================================================================

int ps2_mouse_init(void) {
    // Initialize mouse state
    memset(&g_mouse, 0, sizeof(ps2_mouse_t));
    g_mouse.packet_size = 3;  // Standard packet size
    g_mouse.screen_width = 1024;  // Default screen size
    g_mouse.screen_height = 768;
    g_mouse.x = g_mouse.screen_width / 2;
    g_mouse.y = g_mouse.screen_height / 2;
    
    // Enable auxiliary device (mouse)
    ps2_mouse_wait_input();
    outb(PS2_CMD_PORT, PS2_CMD_ENABLE_PORT2);
    
    // Enable interrupts for mouse
    ps2_mouse_wait_input();
    outb(PS2_CMD_PORT, PS2_CMD_READ_CONFIG);
    ps2_mouse_wait_output();
    uint8_t config = inb(PS2_DATA_PORT);
    config |= PS2_CONFIG_PORT2_INT;
    ps2_mouse_wait_input();
    outb(PS2_CMD_PORT, PS2_CMD_WRITE_CONFIG);
    ps2_mouse_wait_input();
    outb(PS2_DATA_PORT, config);
    
    // Reset mouse
    if (ps2_mouse_send_command(MOUSE_CMD_RESET) != 0) {
        return -1;
    }
    
    // Read reset response
    uint8_t response = ps2_mouse_read();
    if (response != 0xAA) {
        return -1;
    }
    
    // Read device ID
    uint8_t id = ps2_mouse_read();
    
    // Detect scroll wheel and extra buttons
    ps2_mouse_detect_wheel();
    
    // Set defaults
    ps2_mouse_send_command(MOUSE_CMD_SET_DEFAULTS);
    
    // Enable data reporting
    ps2_mouse_send_command(MOUSE_CMD_ENABLE);
    
    // Set sample rate to 100 Hz
    ps2_mouse_send_command(MOUSE_CMD_SET_SAMPLE_RATE);
    ps2_mouse_send_command(100);
    
    // Set resolution to 4 counts/mm
    ps2_mouse_send_command(MOUSE_CMD_SET_RESOLUTION);
    ps2_mouse_send_command(2);  // 4 counts/mm
    
    // Enable streaming mode
    ps2_mouse_send_command(MOUSE_CMD_SET_STREAM_MODE);
    
    // Register interrupt handler
    interrupt_register(IRQ_MOUSE, ps2_mouse_interrupt);
    
    return 0;
}

// =============================================================================
// Public Interface
// =============================================================================

void ps2_mouse_set_screen_size(uint32_t width, uint32_t height) {
    spinlock_acquire(&g_mouse_lock);
    g_mouse.screen_width = width;
    g_mouse.screen_height = height;
    
    // Clamp current position
    if (g_mouse.x >= width) g_mouse.x = width - 1;
    if (g_mouse.y >= height) g_mouse.y = height - 1;
    spinlock_release(&g_mouse_lock);
}

void ps2_mouse_get_position(int32_t* x, int32_t* y) {
    spinlock_acquire(&g_mouse_lock);
    *x = g_mouse.x;
    *y = g_mouse.y;
    spinlock_release(&g_mouse_lock);
}

void ps2_mouse_set_position(int32_t x, int32_t y) {
    spinlock_acquire(&g_mouse_lock);
    g_mouse.x = x;
    g_mouse.y = y;
    
    // Clamp to screen bounds
    if (g_mouse.x < 0) g_mouse.x = 0;
    if (g_mouse.x >= g_mouse.screen_width) g_mouse.x = g_mouse.screen_width - 1;
    if (g_mouse.y < 0) g_mouse.y = 0;
    if (g_mouse.y >= g_mouse.screen_height) g_mouse.y = g_mouse.screen_height - 1;
    spinlock_release(&g_mouse_lock);
}

uint8_t ps2_mouse_get_buttons(void) {
    spinlock_acquire(&g_mouse_lock);
    uint8_t buttons = (g_mouse.left_button ? MOUSE_BUTTON_LEFT : 0) |
                     (g_mouse.right_button ? MOUSE_BUTTON_RIGHT : 0) |
                     (g_mouse.middle_button ? MOUSE_BUTTON_MIDDLE : 0);
    spinlock_release(&g_mouse_lock);
    return buttons;
}

// =============================================================================
// Driver Interface
// =============================================================================

static void* ps2_mouse_probe(device_node_t* node) {
    return &g_mouse;
}

static int ps2_mouse_attach(device_handle_t* handle) {
    return ps2_mouse_init();
}

static void ps2_mouse_detach(device_handle_t* handle) {
    // Disable mouse
    ps2_mouse_send_command(MOUSE_CMD_DISABLE);
    
    // Disable mouse port
    ps2_mouse_wait_input();
    outb(PS2_CMD_PORT, PS2_CMD_DISABLE_PORT2);
}

// Driver registration
static resonance_driver_t ps2_mouse_driver = {
    .name = "ps2-mouse",
    .probe = ps2_mouse_probe,
    .attach = ps2_mouse_attach,
    .detach = ps2_mouse_detach
};

void ps2_mouse_register(void) {
    resonance_register_driver(&ps2_mouse_driver);
}
