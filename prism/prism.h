/*
 * Prism Display Compositor
 * Wayland-compatible compositor for Limitless OS
 */

#ifndef PRISM_H
#define PRISM_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

// =============================================================================
// Compositor Constants
// =============================================================================

#define PRISM_MAX_CLIENTS       128
#define PRISM_MAX_SURFACES      1024
#define PRISM_MAX_OUTPUTS       8
#define PRISM_MAX_SEATS         4

// Surface types
#define SURFACE_TYPE_WINDOW     0x01
#define SURFACE_TYPE_POPUP      0x02
#define SURFACE_TYPE_SUBSURFACE 0x03
#define SURFACE_TYPE_CURSOR     0x04
#define SURFACE_TYPE_DRAG_ICON  0x05

// Surface states
#define SURFACE_STATE_MAPPED    0x01
#define SURFACE_STATE_ACTIVATED 0x02
#define SURFACE_STATE_MAXIMIZED 0x04
#define SURFACE_STATE_FULLSCREEN 0x08
#define SURFACE_STATE_RESIZING  0x10
#define SURFACE_STATE_MOVING    0x20

// Input events
#define INPUT_EVENT_KEY_PRESS   0x01
#define INPUT_EVENT_KEY_RELEASE 0x02
#define INPUT_EVENT_BUTTON_PRESS 0x03
#define INPUT_EVENT_BUTTON_RELEASE 0x04
#define INPUT_EVENT_MOTION      0x05
#define INPUT_EVENT_SCROLL      0x06
#define INPUT_EVENT_TOUCH_DOWN  0x07
#define INPUT_EVENT_TOUCH_UP    0x08
#define INPUT_EVENT_TOUCH_MOTION 0x09

// =============================================================================
// Data Structures
// =============================================================================

// Forward declarations
typedef struct prism_client prism_client_t;
typedef struct prism_surface prism_surface_t;
typedef struct prism_output prism_output_t;
typedef struct prism_seat prism_seat_t;

// Rectangle
typedef struct {
    int32_t x;
    int32_t y;
    uint32_t width;
    uint32_t height;
} prism_rect_t;

// Point
typedef struct {
    int32_t x;
    int32_t y;
} prism_point_t;

// Color
typedef struct {
    uint8_t r;
    uint8_t g;
    uint8_t b;
    uint8_t a;
} prism_color_t;

// Transform matrix
typedef struct {
    float m[9];
} prism_matrix_t;

// Surface buffer
typedef struct {
    uint32_t id;
    void* data;
    uint32_t width;
    uint32_t height;
    uint32_t stride;
    uint32_t format;
    bool y_inverted;
    
    // Damage tracking
    prism_rect_t* damage_rects;
    uint32_t damage_count;
    
    // Reference counting
    uint32_t ref_count;
} prism_buffer_t;

// Surface
struct prism_surface {
    uint32_t id;
    prism_client_t* client;
    uint8_t type;
    uint32_t state;
    
    // Geometry
    prism_rect_t geometry;
    prism_rect_t pending_geometry;
    
    // Buffers
    prism_buffer_t* buffer;
    prism_buffer_t* pending_buffer;
    
    // Transform
    prism_matrix_t transform;
    float opacity;
    
    // Hierarchy
    prism_surface_t* parent;
    prism_surface_t* children;
    prism_surface_t* next_sibling;
    
    // Input
    bool accepts_input;
    prism_rect_t input_region;
    
    // Frame callbacks
    void (*frame_callback)(prism_surface_t* surface, uint32_t time);
    
    // User data
    void* user_data;
};

// Output (display)
struct prism_output {
    uint32_t id;
    char name[64];
    
    // Physical properties
    uint32_t width_mm;
    uint32_t height_mm;
    
    // Mode
    uint32_t width;
    uint32_t height;
    uint32_t refresh_rate;
    
    // Position in global space
    int32_t x;
    int32_t y;
    
    // Transform
    uint32_t transform;  // 0, 90, 180, 270 degrees
    float scale;
    
    // Backend specific data
    void* backend_data;
    
    // Framebuffer
    uint32_t* framebuffer;
    uint32_t fb_width;
    uint32_t fb_height;
    uint32_t fb_stride;
    
    // Rendering
    bool needs_repaint;
    uint64_t last_frame_time;
    
    prism_output_t* next;
};

// Input seat
struct prism_seat {
    uint32_t id;
    char name[64];
    
    // Capabilities
    bool has_pointer;
    bool has_keyboard;
    bool has_touch;
    
    // Pointer state
    prism_point_t pointer_pos;
    uint32_t button_state;
    prism_surface_t* pointer_focus;
    
    // Keyboard state
    uint32_t* pressed_keys;
    uint32_t key_count;
    prism_surface_t* keyboard_focus;
    uint32_t modifiers;
    
    // Touch state
    struct {
        int32_t id;
        prism_point_t pos;
        prism_surface_t* surface;
    } touch_points[10];
    uint32_t touch_count;
    
    // Drag and drop
    prism_surface_t* drag_surface;
    prism_point_t drag_offset;
    
    prism_seat_t* next;
};

// Client connection
struct prism_client {
    uint32_t id;
    int socket_fd;
    
    // Surfaces owned by this client
    prism_surface_t* surfaces;
    uint32_t surface_count;
    
    // Event queue
    void* event_queue;
    size_t event_queue_size;
    
    // Callbacks
    void (*disconnect_callback)(prism_client_t* client);
    
    // User data
    void* user_data;
    
    prism_client_t* next;
};

// Compositor state
typedef struct {
    // Clients
    prism_client_t* clients;
    uint32_t client_count;
    
    // Surfaces
    prism_surface_t* surfaces;
    prism_surface_t* surface_stack_top;  // Z-order
    
    // Outputs
    prism_output_t* outputs;
    uint32_t output_count;
    
    // Seats
    prism_seat_t* seats;
    uint32_t seat_count;
    
    // Renderer
    void* renderer;
    
    // Backend
    void* backend;
    
    // Configuration
    bool enable_animations;
    bool enable_shadows;
    bool enable_blur;
    uint32_t animation_duration;
} prism_compositor_t;

// =============================================================================
// Function Prototypes
// =============================================================================

// Initialization
int prism_init(void);
void prism_shutdown(void);

// Client management
prism_client_t* prism_accept_client(int socket_fd);
void prism_disconnect_client(prism_client_t* client);
int prism_dispatch_client(prism_client_t* client);

// Surface management
prism_surface_t* prism_create_surface(prism_client_t* client, uint8_t type);
void prism_destroy_surface(prism_surface_t* surface);
void prism_map_surface(prism_surface_t* surface);
void prism_unmap_surface(prism_surface_t* surface);
void prism_raise_surface(prism_surface_t* surface);
void prism_lower_surface(prism_surface_t* surface);

// Surface operations
void prism_surface_attach_buffer(prism_surface_t* surface, prism_buffer_t* buffer);
void prism_surface_commit(prism_surface_t* surface);
void prism_surface_damage(prism_surface_t* surface, prism_rect_t* rect);
void prism_surface_set_geometry(prism_surface_t* surface, prism_rect_t* geometry);
void prism_surface_set_opacity(prism_surface_t* surface, float opacity);

// Output management
prism_output_t* prism_create_output(const char* name, uint32_t width, uint32_t height);
void prism_destroy_output(prism_output_t* output);
void prism_output_set_mode(prism_output_t* output, uint32_t width, uint32_t height,
                          uint32_t refresh);
void prism_output_set_transform(prism_output_t* output, uint32_t transform);
void prism_output_set_scale(prism_output_t* output, float scale);

// Rendering
void prism_repaint(prism_output_t* output);
void prism_render_surface(prism_surface_t* surface, prism_output_t* output);
void prism_composite(prism_output_t* output);
void prism_present(prism_output_t* output);

// Input handling
void prism_handle_key(prism_seat_t* seat, uint32_t key, bool pressed);
void prism_handle_button(prism_seat_t* seat, uint32_t button, bool pressed);
void prism_handle_motion(prism_seat_t* seat, int32_t x, int32_t y);
void prism_handle_scroll(prism_seat_t* seat, int32_t dx, int32_t dy);
void prism_handle_touch(prism_seat_t* seat, int32_t id, int32_t x, int32_t y,
                       bool down);

// Focus management
void prism_set_keyboard_focus(prism_seat_t* seat, prism_surface_t* surface);
void prism_set_pointer_focus(prism_seat_t* seat, prism_surface_t* surface);

// Animation
void prism_animate_surface(prism_surface_t* surface, prism_rect_t* from,
                          prism_rect_t* to, uint32_t duration);
void prism_animate_opacity(prism_surface_t* surface, float from, float to,
                          uint32_t duration);

// Effects
void prism_apply_blur(prism_surface_t* surface, float radius);
void prism_apply_shadow(prism_surface_t* surface, float radius, prism_color_t* color);

// Helper functions
prism_surface_t* prism_surface_at(prism_point_t* point);
bool prism_rect_contains_point(prism_rect_t* rect, prism_point_t* point);
bool prism_rect_intersects(prism_rect_t* a, prism_rect_t* b);
void prism_matrix_multiply(prism_matrix_t* result, prism_matrix_t* a, prism_matrix_t* b);
void prism_matrix_translate(prism_matrix_t* matrix, float x, float y);
void prism_matrix_scale(prism_matrix_t* matrix, float x, float y);
void prism_matrix_rotate(prism_matrix_t* matrix, float angle);

#endif /* PRISM_H */
