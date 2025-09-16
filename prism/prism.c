/*
 * Prism Display Compositor
 * Core compositor implementation
 */

#include "prism.h"
#include "renderer.h"
#include "wayland_protocol.h"
#include "../continuum/flux_memory.h"
#include "../continuum/temporal_scheduler.h"
#include "../continuum/conduit_ipc.h"

// =============================================================================
// Global Compositor State
// =============================================================================

static prism_compositor_t g_compositor;
static spinlock_t g_compositor_lock = SPINLOCK_INIT;
static bool g_running = false;

// =============================================================================
// Surface Management
// =============================================================================

prism_surface_t* prism_create_surface(prism_client_t* client, uint8_t type) {
    prism_surface_t* surface = flux_allocate(NULL, sizeof(prism_surface_t),
                                            FLUX_ALLOC_KERNEL | FLUX_ALLOC_ZERO);
    if (!surface) {
        return NULL;
    }
    
    static uint32_t next_id = 1;
    surface->id = next_id++;
    surface->client = client;
    surface->type = type;
    surface->opacity = 1.0f;
    surface->accepts_input = true;
    
    // Initialize transform matrix to identity
    prism_matrix_identity(&surface->transform);
    
    // Add to client's surface list
    spinlock_acquire(&g_compositor_lock);
    
    surface->next_sibling = client->surfaces;
    client->surfaces = surface;
    client->surface_count++;
    
    // Add to global surface list
    prism_surface_t** list = &g_compositor.surfaces;
    while (*list) {
        list = &(*list)->next_sibling;
    }
    *list = surface;
    
    spinlock_release(&g_compositor_lock);
    
    return surface;
}

void prism_destroy_surface(prism_surface_t* surface) {
    if (!surface) {
        return;
    }
    
    spinlock_acquire(&g_compositor_lock);
    
    // Remove from surface stack
    prism_surface_t** stack = &g_compositor.surface_stack_top;
    while (*stack) {
        if (*stack == surface) {
            *stack = surface->next_sibling;
            break;
        }
        stack = &(*stack)->next_sibling;
    }
    
    // Remove from client's surface list
    if (surface->client) {
        prism_surface_t** list = &surface->client->surfaces;
        while (*list) {
            if (*list == surface) {
                *list = surface->next_sibling;
                surface->client->surface_count--;
                break;
            }
            list = &(*list)->next_sibling;
        }
    }
    
    // Remove from global surface list
    prism_surface_t** global = &g_compositor.surfaces;
    while (*global) {
        if (*global == surface) {
            *global = surface->next_sibling;
            break;
        }
        global = &(*global)->next_sibling;
    }
    
    spinlock_release(&g_compositor_lock);
    
    // Release buffers
    if (surface->buffer) {
        prism_buffer_release(surface->buffer);
    }
    if (surface->pending_buffer) {
        prism_buffer_release(surface->pending_buffer);
    }
    
    flux_free(surface);
}

void prism_surface_attach_buffer(prism_surface_t* surface, prism_buffer_t* buffer) {
    if (!surface) {
        return;
    }
    
    // Release old pending buffer
    if (surface->pending_buffer) {
        prism_buffer_release(surface->pending_buffer);
    }
    
    // Reference new buffer
    if (buffer) {
        buffer->ref_count++;
    }
    
    surface->pending_buffer = buffer;
}

void prism_surface_commit(prism_surface_t* surface) {
    if (!surface) {
        return;
    }
    
    spinlock_acquire(&g_compositor_lock);
    
    // Swap buffers
    if (surface->pending_buffer) {
        if (surface->buffer) {
            prism_buffer_release(surface->buffer);
        }
        surface->buffer = surface->pending_buffer;
        surface->pending_buffer = NULL;
    }
    
    // Apply pending geometry
    if (surface->pending_geometry.width != 0) {
        surface->geometry = surface->pending_geometry;
        memset(&surface->pending_geometry, 0, sizeof(prism_rect_t));
    }
    
    // Mark outputs for repaint
    prism_output_t* output = g_compositor.outputs;
    while (output) {
        output->needs_repaint = true;
        output = output->next;
    }
    
    spinlock_release(&g_compositor_lock);
    
    // Call frame callback if set
    if (surface->frame_callback) {
        uint32_t time = (uint32_t)(temporal_get_time() / 1000);
        surface->frame_callback(surface, time);
    }
}

void prism_map_surface(prism_surface_t* surface) {
    if (!surface || (surface->state & SURFACE_STATE_MAPPED)) {
        return;
    }
    
    surface->state |= SURFACE_STATE_MAPPED;
    
    // Add to surface stack
    spinlock_acquire(&g_compositor_lock);
    
    prism_surface_t** stack = &g_compositor.surface_stack_top;
    while (*stack) {
        stack = &(*stack)->next_sibling;
    }
    *stack = surface;
    
    spinlock_release(&g_compositor_lock);
}

void prism_raise_surface(prism_surface_t* surface) {
    if (!surface) {
        return;
    }
    
    spinlock_acquire(&g_compositor_lock);
    
    // Remove from current position
    prism_surface_t** stack = &g_compositor.surface_stack_top;
    while (*stack) {
        if (*stack == surface) {
            *stack = surface->next_sibling;
            break;
        }
        stack = &(*stack)->next_sibling;
    }
    
    // Add to top
    surface->next_sibling = g_compositor.surface_stack_top;
    g_compositor.surface_stack_top = surface;
    
    spinlock_release(&g_compositor_lock);
}

// =============================================================================
// Rendering Pipeline
// =============================================================================

void prism_repaint(prism_output_t* output) {
    if (!output || !output->needs_repaint) {
        return;
    }
    
    // Clear framebuffer
    prism_clear_output(output);
    
    // Render surfaces from bottom to top
    prism_surface_t* surfaces[PRISM_MAX_SURFACES];
    int surface_count = 0;
    
    spinlock_acquire(&g_compositor_lock);
    
    // Build surface list in reverse order
    prism_surface_t* surface = g_compositor.surface_stack_top;
    while (surface && surface_count < PRISM_MAX_SURFACES) {
        if (surface->state & SURFACE_STATE_MAPPED) {
            surfaces[surface_count++] = surface;
        }
        surface = surface->next_sibling;
    }
    
    spinlock_release(&g_compositor_lock);
    
    // Render surfaces
    for (int i = surface_count - 1; i >= 0; i--) {
        prism_render_surface(surfaces[i], output);
    }
    
    // Apply post-processing effects
    if (g_compositor.enable_blur) {
        prism_apply_blur_pass(output);
    }
    
    // Present to display
    prism_present(output);
    
    output->needs_repaint = false;
    output->last_frame_time = temporal_get_time();
}

void prism_render_surface(prism_surface_t* surface, prism_output_t* output) {
    if (!surface || !surface->buffer || !output) {
        return;
    }
    
    // Calculate surface position in output space
    prism_rect_t dst_rect = {
        .x = surface->geometry.x - output->x,
        .y = surface->geometry.y - output->y,
        .width = surface->geometry.width,
        .height = surface->geometry.height
    };
    
    // Clip to output bounds
    if (dst_rect.x >= (int32_t)output->width || 
        dst_rect.y >= (int32_t)output->height ||
        dst_rect.x + (int32_t)dst_rect.width <= 0 ||
        dst_rect.y + (int32_t)dst_rect.height <= 0) {
        return;
    }
    
    // Apply shadow if enabled
    if (g_compositor.enable_shadows && surface->type == SURFACE_TYPE_WINDOW) {
        prism_render_shadow(output, &dst_rect);
    }
    
    // Render surface content
    prism_blit_surface(output, surface, &dst_rect);
    
    // Render subsurfaces
    prism_surface_t* child = surface->children;
    while (child) {
        prism_render_surface(child, output);
        child = child->next_sibling;
    }
}

static void prism_blit_surface(prism_output_t* output, prism_surface_t* surface,
                              prism_rect_t* dst_rect) {
    prism_buffer_t* buffer = surface->buffer;
    uint32_t* src = (uint32_t*)buffer->data;
    uint32_t* dst = output->framebuffer;
    
    // Calculate actual blit region
    int32_t src_x = 0, src_y = 0;
    int32_t dst_x = dst_rect->x;
    int32_t dst_y = dst_rect->y;
    uint32_t width = dst_rect->width;
    uint32_t height = dst_rect->height;
    
    // Clip source rectangle
    if (dst_x < 0) {
        src_x = -dst_x;
        width += dst_x;
        dst_x = 0;
    }
    if (dst_y < 0) {
        src_y = -dst_y;
        height += dst_y;
        dst_y = 0;
    }
    if (dst_x + width > output->width) {
        width = output->width - dst_x;
    }
    if (dst_y + height > output->height) {
        height = output->height - dst_y;
    }
    
    // Apply transform matrix
    for (uint32_t y = 0; y < height; y++) {
        for (uint32_t x = 0; x < width; x++) {
            // Transform coordinates
            float tx, ty;
            prism_matrix_transform_point(&surface->transform,
                                        src_x + x, src_y + y, &tx, &ty);
            
            // Sample source pixel (with bilinear filtering if needed)
            uint32_t pixel = prism_sample_pixel(buffer, tx, ty);
            
            // Apply opacity
            if (surface->opacity < 1.0f) {
                pixel = prism_blend_alpha(pixel, surface->opacity);
            }
            
            // Blend with destination
            uint32_t dst_offset = (dst_y + y) * output->fb_stride + (dst_x + x);
            dst[dst_offset] = prism_alpha_blend(dst[dst_offset], pixel);
        }
    }
}

// =============================================================================
// Input Handling
// =============================================================================

void prism_handle_motion(prism_seat_t* seat, int32_t x, int32_t y) {
    if (!seat) {
        return;
    }
    
    seat->pointer_pos.x = x;
    seat->pointer_pos.y = y;
    
    // Find surface under pointer
    prism_point_t point = {x, y};
    prism_surface_t* surface = prism_surface_at(&point);
    
    if (surface != seat->pointer_focus) {
        // Send leave event to old surface
        if (seat->pointer_focus) {
            prism_send_pointer_leave(seat->pointer_focus);
        }
        
        // Send enter event to new surface
        if (surface) {
            prism_send_pointer_enter(surface, x - surface->geometry.x,
                                   y - surface->geometry.y);
        }
        
        seat->pointer_focus = surface;
    } else if (surface) {
        // Send motion event
        prism_send_pointer_motion(surface, x - surface->geometry.x,
                                y - surface->geometry.y);
    }
}

void prism_handle_button(prism_seat_t* seat, uint32_t button, bool pressed) {
    if (!seat || !seat->pointer_focus) {
        return;
    }
    
    if (pressed) {
        seat->button_state |= (1 << button);
    } else {
        seat->button_state &= ~(1 << button);
    }
    
    prism_send_pointer_button(seat->pointer_focus, button, pressed);
    
    // Handle window operations
    if (pressed && button == 1) {  // Left click
        prism_surface_t* surface = seat->pointer_focus;
        
        // Raise window on click
        if (surface->type == SURFACE_TYPE_WINDOW) {
            prism_raise_surface(surface);
            prism_set_keyboard_focus(seat, surface);
        }
    }
}

void prism_handle_key(prism_seat_t* seat, uint32_t key, bool pressed) {
    if (!seat || !seat->keyboard_focus) {
        return;
    }
    
    // Update pressed keys
    if (pressed) {
        // Add to pressed keys
        for (uint32_t i = 0; i < seat->key_count; i++) {
            if (seat->pressed_keys[i] == 0) {
                seat->pressed_keys[i] = key;
                break;
            }
        }
    } else {
        // Remove from pressed keys
        for (uint32_t i = 0; i < seat->key_count; i++) {
            if (seat->pressed_keys[i] == key) {
                seat->pressed_keys[i] = 0;
                break;
            }
        }
    }
    
    prism_send_keyboard_key(seat->keyboard_focus, key, pressed);
}

prism_surface_t* prism_surface_at(prism_point_t* point) {
    prism_surface_t* found = NULL;
    
    spinlock_acquire(&g_compositor_lock);
    
    // Search from top to bottom
    prism_surface_t* surface = g_compositor.surface_stack_top;
    while (surface) {
        if ((surface->state & SURFACE_STATE_MAPPED) && surface->accepts_input) {
            if (prism_rect_contains_point(&surface->geometry, point)) {
                found = surface;
                break;
            }
        }
        surface = surface->next_sibling;
    }
    
    spinlock_release(&g_compositor_lock);
    
    return found;
}

// =============================================================================
// Animation System
// =============================================================================

typedef struct {
    prism_surface_t* surface;
    prism_rect_t from_geometry;
    prism_rect_t to_geometry;
    float from_opacity;
    float to_opacity;
    uint64_t start_time;
    uint32_t duration;
    bool animating_geometry;
    bool animating_opacity;
} prism_animation_t;

static prism_animation_t g_animations[128];
static uint32_t g_animation_count = 0;

void prism_animate_surface(prism_surface_t* surface, prism_rect_t* from,
                          prism_rect_t* to, uint32_t duration) {
    if (!surface || !from || !to || g_animation_count >= 128) {
        return;
    }
    
    prism_animation_t* anim = &g_animations[g_animation_count++];
    anim->surface = surface;
    anim->from_geometry = *from;
    anim->to_geometry = *to;
    anim->start_time = temporal_get_time();
    anim->duration = duration * 1000;  // Convert to microseconds
    anim->animating_geometry = true;
    anim->animating_opacity = false;
}

static void prism_update_animations(void) {
    uint64_t now = temporal_get_time();
    
    for (uint32_t i = 0; i < g_animation_count; i++) {
        prism_animation_t* anim = &g_animations[i];
        uint64_t elapsed = now - anim->start_time;
        
        if (elapsed >= anim->duration) {
            // Animation complete
            if (anim->animating_geometry) {
                anim->surface->geometry = anim->to_geometry;
            }
            if (anim->animating_opacity) {
                anim->surface->opacity = anim->to_opacity;
            }
            
            // Remove animation
            if (i < g_animation_count - 1) {
                g_animations[i] = g_animations[g_animation_count - 1];
                i--;
            }
            g_animation_count--;
        } else {
            // Interpolate
            float t = (float)elapsed / (float)anim->duration;
            t = prism_ease_in_out_cubic(t);
            
            if (anim->animating_geometry) {
                anim->surface->geometry.x = prism_lerp(anim->from_geometry.x,
                                                      anim->to_geometry.x, t);
                anim->surface->geometry.y = prism_lerp(anim->from_geometry.y,
                                                      anim->to_geometry.y, t);
                anim->surface->geometry.width = prism_lerp(anim->from_geometry.width,
                                                          anim->to_geometry.width, t);
                anim->surface->geometry.height = prism_lerp(anim->from_geometry.height,
                                                           anim->to_geometry.height, t);
            }
            
            if (anim->animating_opacity) {
                anim->surface->opacity = prism_lerp(anim->from_opacity,
                                                   anim->to_opacity, t);
            }
        }
    }
}

// =============================================================================
// Main Compositor Loop
// =============================================================================

static void prism_compositor_thread(void* arg) {
    while (g_running) {
        // Update animations
        if (g_compositor.enable_animations) {
            prism_update_animations();
        }
        
        // Repaint outputs that need it
        prism_output_t* output = g_compositor.outputs;
        while (output) {
            if (output->needs_repaint || g_animation_count > 0) {
                prism_repaint(output);
            }
            output = output->next;
        }
        
        // Process client messages
        prism_client_t* client = g_compositor.clients;
        while (client) {
            prism_dispatch_client(client);
            client = client->next;
        }
        
        // Sleep until next frame (60 FPS)
        temporal_sleep(16666);  // ~16.67ms
    }
}

// =============================================================================
// Initialization
// =============================================================================

int prism_init(void) {
    memset(&g_compositor, 0, sizeof(g_compositor));
    
    // Initialize default settings
    g_compositor.enable_animations = true;
    g_compositor.enable_shadows = true;
    g_compositor.enable_blur = false;  // Performance intensive
    g_compositor.animation_duration = 200;  // 200ms default
    
    // Initialize renderer
    g_compositor.renderer = prism_renderer_create();
    if (!g_compositor.renderer) {
        return -1;
    }
    
    // Create default output (primary display)
    prism_output_t* primary = prism_create_output("primary", 1920, 1080);
    if (!primary) {
        prism_renderer_destroy(g_compositor.renderer);
        return -1;
    }
    
    // Create default seat (primary input)
    prism_seat_t* seat = prism_create_seat("seat0");
    if (!seat) {
        prism_destroy_output(primary);
        prism_renderer_destroy(g_compositor.renderer);
        return -1;
    }
    
    // Start compositor thread
    g_running = true;
    thread_t* compositor_thread = temporal_create_thread(prism_compositor_thread,
                                                        NULL, THREAD_PRIORITY_HIGH);
    if (!compositor_thread) {
        g_running = false;
        return -1;
    }
    
    return 0;
}

void prism_shutdown(void) {
    g_running = false;
    
    // Wait for compositor thread to finish
    temporal_sleep(100000);
    
    // Clean up surfaces
    while (g_compositor.surfaces) {
        prism_destroy_surface(g_compositor.surfaces);
    }
    
    // Clean up clients
    while (g_compositor.clients) {
        prism_disconnect_client(g_compositor.clients);
    }
    
    // Clean up outputs
    while (g_compositor.outputs) {
        prism_destroy_output(g_compositor.outputs);
    }
    
    // Clean up seats
    while (g_compositor.seats) {
        prism_destroy_seat(g_compositor.seats);
    }
    
    // Destroy renderer
    if (g_compositor.renderer) {
        prism_renderer_destroy(g_compositor.renderer);
    }
}
