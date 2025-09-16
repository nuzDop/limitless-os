/*
 * Window Manager for Prism Compositor
 * Window management and decoration
 */

#include "prism.h"
#include "window_manager.h"
#include "../continuum/flux_memory.h"

// =============================================================================
// Window State
// =============================================================================

typedef struct window {
    prism_surface_t* surface;
    char title[256];
    uint32_t flags;
    
    // Geometry
    prism_rect_t geometry;
    prism_rect_t saved_geometry;  // For unmaximize
    
    // Decorations
    bool decorated;
    uint32_t border_width;
    uint32_t titlebar_height;
    prism_color_t border_color;
    prism_color_t titlebar_color;
    
    // State
    bool maximized;
    bool minimized;
    bool fullscreen;
    bool focused;
    bool resizing;
    bool moving;
    
    // Resize/move state
    prism_point_t drag_start;
    prism_rect_t drag_start_geometry;
    uint8_t resize_edges;
    
    struct window* next;
} window_t;

static window_t* g_windows;
static window_t* g_focused_window;
static spinlock_t g_wm_lock = SPINLOCK_INIT;

// =============================================================================
// Window Creation
// =============================================================================

window_t* wm_create_window(prism_surface_t* surface) {
    window_t* window = flux_allocate(NULL, sizeof(window_t),
                                    FLUX_ALLOC_KERNEL | FLUX_ALLOC_ZERO);
    if (!window) {
        return NULL;
    }
    
    window->surface = surface;
    window->decorated = true;
    window->border_width = 2;
    window->titlebar_height = 30;
    
    // Default colors
    window->border_color = (prism_color_t){0x40, 0x40, 0x40, 0xFF};
    window->titlebar_color = (prism_color_t){0x20, 0x20, 0x30, 0xFF};
    
    // Set initial geometry
    window->geometry = surface->geometry;
    
    // Add to window list
    spinlock_acquire(&g_wm_lock);
    window->next = g_windows;
    g_windows = window;
    spinlock_release(&g_wm_lock);
    
    // Create decoration surfaces
    if (window->decorated) {
        wm_create_decorations(window);
    }
    
    return window;
}

// =============================================================================
// Window Operations
// =============================================================================

void wm_maximize_window(window_t* window) {
    if (!window || window->maximized) {
        return;
    }
    
    // Save current geometry
    window->saved_geometry = window->geometry;
    
    // Get output dimensions
    prism_output_t* output = prism_get_primary_output();
    if (output) {
        window->geometry.x = 0;
        window->geometry.y = 0;
        window->geometry.width = output->width;
        window->geometry.height = output->height;
        
        if (window->decorated) {
            window->geometry.y = window->titlebar_height;
            window->geometry.height -= window->titlebar_height;
        }
    }
    
    window->maximized = true;
    
    // Update surface geometry
    prism_surface_set_geometry(window->surface, &window->geometry);
}

void wm_minimize_window(window_t* window) {
    if (!window || window->minimized) {
        return;
    }
    
    window->minimized = true;
    
    // Hide window
    prism_unmap_surface(window->surface);
    
    // Focus next window
    wm_focus_next_window();
}

void wm_close_window(window_t* window) {
    if (!window) {
        return;
    }
    
    // Send close event to client
    prism_send_close_event(window->surface);
    
    // Remove from window list
    spinlock_acquire(&g_wm_lock);
    
    window_t** prev = &g_windows;
    while (*prev) {
        if (*prev == window) {
            *prev = window->next;
            break;
        }
        prev = &(*prev)->next;
    }
    
    if (g_focused_window == window) {
        g_focused_window = NULL;
        wm_focus_next_window();
    }
    
    spinlock_release(&g_wm_lock);
    
    // Destroy decorations
    if (window->decorated) {
        wm_destroy_decorations(window);
    }
    
    flux_free(window);
}

// =============================================================================
// Window Movement and Resizing
// =============================================================================

void wm_begin_move(window_t* window, int32_t x, int32_t y) {
    if (!window || window->maximized || window->fullscreen) {
        return;
    }
    
    window->moving = true;
    window->drag_start.x = x;
    window->drag_start.y = y;
    window->drag_start_geometry = window->geometry;
}

void wm_update_move(window_t* window, int32_t x, int32_t y) {
    if (!window || !window->moving) {
        return;
    }
    
    int32_t dx = x - window->drag_start.x;
    int32_t dy = y - window->drag_start.y;
    
    window->geometry.x = window->drag_start_geometry.x + dx;
    window->geometry.y = window->drag_start_geometry.y + dy;
    
    // Update surface position
    prism_surface_set_geometry(window->surface, &window->geometry);
}

void wm_end_move(window_t* window) {
    if (!window) {
        return;
    }
    
    window->moving = false;
}

void wm_begin_resize(window_t* window, int32_t x, int32_t y, uint8_t edges) {
    if (!window || window->maximized || window->fullscreen) {
        return;
    }
    
    window->resizing = true;
    window->resize_edges = edges;
    window->drag_start.x = x;
    window->drag_start.y = y;
    window->drag_start_geometry = window->geometry;
}

void wm_update_resize(window_t* window, int32_t x, int32_t y) {
    if (!window || !window->resizing) {
        return;
    }
    
    int32_t dx = x - window->drag_start.x;
    int32_t dy = y - window->drag_start.y;
    
    prism_rect_t new_geometry = window->drag_start_geometry;
    
    if (window->resize_edges & RESIZE_EDGE_LEFT) {
        new_geometry.x += dx;
        new_geometry.width -= dx;
    }
    if (window->resize_edges & RESIZE_EDGE_RIGHT) {
        new_geometry.width += dx;
    }
    if (window->resize_edges & RESIZE_EDGE_TOP) {
        new_geometry.y += dy;
        new_geometry.height -= dy;
    }
    if (window->resize_edges & RESIZE_EDGE_BOTTOM) {
        new_geometry.height += dy;
    }
    
    // Enforce minimum size
    if (new_geometry.width < 100) new_geometry.width = 100;
    if (new_geometry.height < 50) new_geometry.height = 50;
    
    window->geometry = new_geometry;
    
    // Update surface geometry
    prism_surface_set_geometry(window->surface, &window->geometry);
}

void wm_end_resize(window_t* window) {
    if (!window) {
        return;
    }
    
    window->resizing = false;
    window->resize_edges = 0;
}

// =============================================================================
// Focus Management
// =============================================================================

void wm_focus_window(window_t* window) {
    if (!window || window == g_focused_window) {
        return;
    }
    
    // Unfocus current window
    if (g_focused_window) {
        g_focused_window->focused = false;
        wm_update_decorations(g_focused_window);
    }
    
    // Focus new window
    window->focused = true;
    g_focused_window = window;
    
    // Raise window
    prism_raise_surface(window->surface);
    
    // Update decorations
    wm_update_decorations(window);
    
    // Set keyboard focus
    prism_seat_t* seat = prism_get_default_seat();
    if (seat) {
        prism_set_keyboard_focus(seat, window->surface);
    }
}

void wm_focus_next_window(void) {
    spinlock_acquire(&g_wm_lock);
    
    window_t* next = NULL;
    window_t* window = g_windows;
    
    // Find first visible window
    while (window) {
        if (!window->minimized) {
            next = window;
            break;
        }
        window = window->next;
    }
    
    spinlock_release(&g_wm_lock);
    
    if (next) {
        wm_focus_window(next);
    }
}

// =============================================================================
// Decoration Rendering
// =============================================================================

void wm_create_decorations(window_t* window) {
    if (!window->decorated) {
        return;
    }
    
    // Create titlebar surface
    prism_surface_t* titlebar = prism_create_surface(window->surface->client,
                                                    SURFACE_TYPE_SUBSURFACE);
    if (titlebar) {
        prism_rect_t titlebar_rect = {
            .x = window->geometry.x,
            .y = window->geometry.y - window->titlebar_height,
            .width = window->geometry.width,
            .height = window->titlebar_height
        };
        prism_surface_set_geometry(titlebar, &titlebar_rect);
        
        // Render titlebar
        wm_render_titlebar(window, titlebar);
    }
}

void wm_render_titlebar(window_t* window, prism_surface_t* titlebar) {
    // Allocate buffer for titlebar
    size_t buffer_size = window->geometry.width * window->titlebar_height * 4;
    uint32_t* pixels = flux_allocate(NULL, buffer_size, FLUX_ALLOC_KERNEL);
    if (!pixels) {
        return;
    }
    
    // Fill with titlebar color
    uint32_t color = (window->titlebar_color.a << 24) |
                    (window->titlebar_color.r << 16) |
                    (window->titlebar_color.g << 8) |
                    window->titlebar_color.b;
    
    for (uint32_t i = 0; i < window->geometry.width * window->titlebar_height; i++) {
        pixels[i] = color;
    }
    
    // Draw title text
    if (window->title[0]) {
        wm_draw_text(pixels, window->geometry.width, window->titlebar_height,
                    10, 7, window->title, 0xFFFFFFFF);
    }
    
    // Draw window controls (close, maximize, minimize)
    uint32_t button_size = 20;
    uint32_t button_y = (window->titlebar_height - button_size) / 2;
    uint32_t button_spacing = 5;
    
    // Close button
    uint32_t close_x = window->geometry.width - button_size - button_spacing;
    wm_draw_button(pixels, window->geometry.width, window->titlebar_height,
                  close_x, button_y, button_size, 0xFFFF4444);
    
    // Maximize button
    uint32_t max_x = close_x - button_size - button_spacing;
    wm_draw_button(pixels, window->geometry.width, window->titlebar_height,
                  max_x, button_y, button_size, 0xFF44FF44);
    
    // Minimize button
    uint32_t min_x = max_x - button_size - button_spacing;
    wm_draw_button(pixels, window->geometry.width, window->titlebar_height,
                  min_x, button_y, button_size, 0xFFFFFF44);
    
    // Create buffer and attach to surface
    prism_buffer_t* buffer = prism_create_buffer(pixels, window->geometry.width,
                                                window->titlebar_height,
                                                window->geometry.width * 4,
                                                PIXEL_FORMAT_ARGB8888);
    prism_surface_attach_buffer(titlebar, buffer);
    prism_surface_commit(titlebar);
    
    flux_free(pixels);
}
