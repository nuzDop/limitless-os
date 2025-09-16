/*
 * VESA Graphics Driver for Continuum Kernel
 * VESA BIOS Extensions framebuffer driver
 */

#include "vesa.h"
#include "../resonance.h"
#include "../../flux_memory.h"

// =============================================================================
// Global VESA State
// =============================================================================

static vesa_info_t g_vesa_info;
static vesa_mode_info_t g_current_mode;
static uint32_t* g_framebuffer;
static uint32_t* g_backbuffer;
static spinlock_t g_vesa_lock = SPINLOCK_INIT;
static bool g_vesa_initialized = false;

// =============================================================================
// Pixel Operations
// =============================================================================

void vesa_put_pixel(uint32_t x, uint32_t y, uint32_t color) {
    if (!g_vesa_initialized || x >= g_current_mode.width || y >= g_current_mode.height) {
        return;
    }
    
    uint32_t* fb = g_backbuffer ? g_backbuffer : g_framebuffer;
    fb[y * g_current_mode.pitch_pixels + x] = color;
}

uint32_t vesa_get_pixel(uint32_t x, uint32_t y) {
    if (!g_vesa_initialized || x >= g_current_mode.width || y >= g_current_mode.height) {
        return 0;
    }
    
    uint32_t* fb = g_backbuffer ? g_backbuffer : g_framebuffer;
    return fb[y * g_current_mode.pitch_pixels + x];
}

// =============================================================================
// Drawing Primitives
// =============================================================================

void vesa_fill_rect(uint32_t x, uint32_t y, uint32_t width, uint32_t height,
                    uint32_t color) {
    if (!g_vesa_initialized) {
        return;
    }
    
    spinlock_acquire(&g_vesa_lock);
    
    // Clip rectangle to screen bounds
    if (x >= g_current_mode.width || y >= g_current_mode.height) {
        spinlock_release(&g_vesa_lock);
        return;
    }
    
    if (x + width > g_current_mode.width) {
        width = g_current_mode.width - x;
    }
    
    if (y + height > g_current_mode.height) {
        height = g_current_mode.height - y;
    }
    
    uint32_t* fb = g_backbuffer ? g_backbuffer : g_framebuffer;
    
    for (uint32_t row = 0; row < height; row++) {
        uint32_t* line = &fb[(y + row) * g_current_mode.pitch_pixels + x];
        for (uint32_t col = 0; col < width; col++) {
            line[col] = color;
        }
    }
    
    spinlock_release(&g_vesa_lock);
}

void vesa_draw_line(int32_t x0, int32_t y0, int32_t x1, int32_t y1, uint32_t color) {
    if (!g_vesa_initialized) {
        return;
    }
    
    // Bresenham's line algorithm
    int32_t dx = abs(x1 - x0);
    int32_t dy = abs(y1 - y0);
    int32_t sx = x0 < x1 ? 1 : -1;
    int32_t sy = y0 < y1 ? 1 : -1;
    int32_t err = dx - dy;
    
    while (true) {
        vesa_put_pixel(x0, y0, color);
        
        if (x0 == x1 && y0 == y1) {
            break;
        }
        
        int32_t e2 = 2 * err;
        
        if (e2 > -dy) {
            err -= dy;
            x0 += sx;
        }
        
        if (e2 < dx) {
            err += dx;
            y0 += sy;
        }
    }
}

void vesa_draw_circle(int32_t cx, int32_t cy, int32_t radius, uint32_t color) {
    if (!g_vesa_initialized) {
        return;
    }
    
    // Midpoint circle algorithm
    int32_t x = radius;
    int32_t y = 0;
    int32_t err = 0;
    
    while (x >= y) {
        vesa_put_pixel(cx + x, cy + y, color);
        vesa_put_pixel(cx + y, cy + x, color);
        vesa_put_pixel(cx - y, cy + x, color);
        vesa_put_pixel(cx - x, cy + y, color);
        vesa_put_pixel(cx - x, cy - y, color);
        vesa_put_pixel(cx - y, cy - x, color);
        vesa_put_pixel(cx + y, cy - x, color);
        vesa_put_pixel(cx + x, cy - y, color);
        
        if (err <= 0) {
            y++;
            err += 2 * y + 1;
        }
        
        if (err > 0) {
            x--;
            err -= 2 * x + 1;
        }
    }
}

// =============================================================================
// Bitmap Operations
// =============================================================================

void vesa_blit(uint32_t dx, uint32_t dy, uint32_t width, uint32_t height,
              uint32_t* src, uint32_t src_pitch) {
    if (!g_vesa_initialized) {
        return;
    }
    
    spinlock_acquire(&g_vesa_lock);
    
    // Clip to screen bounds
    if (dx >= g_current_mode.width || dy >= g_current_mode.height) {
        spinlock_release(&g_vesa_lock);
        return;
    }
    
    if (dx + width > g_current_mode.width) {
        width = g_current_mode.width - dx;
    }
    
    if (dy + height > g_current_mode.height) {
        height = g_current_mode.height - dy;
    }
    
    uint32_t* fb = g_backbuffer ? g_backbuffer : g_framebuffer;
    
    for (uint32_t y = 0; y < height; y++) {
        uint32_t* dst_line = &fb[(dy + y) * g_current_mode.pitch_pixels + dx];
        uint32_t* src_line = &src[y * src_pitch];
        memcpy(dst_line, src_line, width * sizeof(uint32_t));
    }
    
    spinlock_release(&g_vesa_lock);
}

void vesa_scroll(int32_t dx, int32_t dy) {
    if (!g_vesa_initialized) {
        return;
    }
    
    spinlock_acquire(&g_vesa_lock);
    
    uint32_t* fb = g_backbuffer ? g_backbuffer : g_framebuffer;
    uint32_t pitch = g_current_mode.pitch_pixels;
    uint32_t width = g_current_mode.width;
    uint32_t height = g_current_mode.height;
    
    if (dy > 0) {
        // Scroll down
        for (int32_t y = height - 1; y >= dy; y--) {
            memcpy(&fb[y * pitch], &fb[(y - dy) * pitch], width * sizeof(uint32_t));
        }
        // Clear top
        for (int32_t y = 0; y < dy && y < height; y++) {
            memset(&fb[y * pitch], 0, width * sizeof(uint32_t));
        }
    } else if (dy < 0) {
        // Scroll up
        dy = -dy;
        for (uint32_t y = 0; y < height - dy; y++) {
            memcpy(&fb[y * pitch], &fb[(y + dy) * pitch], width * sizeof(uint32_t));
        }
        // Clear bottom
        for (uint32_t y = height - dy; y < height; y++) {
            memset(&fb[y * pitch], 0, width * sizeof(uint32_t));
        }
    }
    
    // Handle horizontal scroll similarly if needed
    
    spinlock_release(&g_vesa_lock);
}

// =============================================================================
// Double Buffering
// =============================================================================

int vesa_enable_double_buffer(void) {
    if (!g_vesa_initialized || g_backbuffer) {
        return -1;
    }
    
    size_t buffer_size = g_current_mode.pitch * g_current_mode.height;
    g_backbuffer = flux_allocate(NULL, buffer_size, FLUX_ALLOC_KERNEL);
    
    if (!g_backbuffer) {
        return -1;
    }
    
    memset(g_backbuffer, 0, buffer_size);
    return 0;
}

void vesa_swap_buffers(void) {
    if (!g_vesa_initialized || !g_backbuffer) {
        return;
    }
    
    spinlock_acquire(&g_vesa_lock);
    
    size_t buffer_size = g_current_mode.pitch * g_current_mode.height;
    memcpy(g_framebuffer, g_backbuffer, buffer_size);
    
    spinlock_release(&g_vesa_lock);
}

// =============================================================================
// Text Rendering (Simple 8x16 Font)
// =============================================================================

// Basic 8x16 font data (partial - ASCII 32-126)
static const uint8_t font_8x16[][16] = {
    // Space (32)
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
     0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
    // ! (33)
    {0x00, 0x00, 0x18, 0x3C, 0x3C, 0x3C, 0x18, 0x18,
     0x18, 0x00, 0x18, 0x18, 0x00, 0x00, 0x00, 0x00},
    // ... more characters ...
};

void vesa_draw_char(uint32_t x, uint32_t y, char c, uint32_t fg_color, uint32_t bg_color) {
    if (!g_vesa_initialized || c < 32 || c > 126) {
        return;
    }
    
    const uint8_t* glyph = font_8x16[c - 32];
    
    for (uint32_t row = 0; row < 16; row++) {
        uint8_t line = glyph[row];
        for (uint32_t col = 0; col < 8; col++) {
            uint32_t color = (line & (0x80 >> col)) ? fg_color : bg_color;
            vesa_put_pixel(x + col, y + row, color);
        }
    }
}

void vesa_draw_string(uint32_t x, uint32_t y, const char* str,
                     uint32_t fg_color, uint32_t bg_color) {
    if (!g_vesa_initialized || !str) {
        return;
    }
    
    uint32_t cx = x;
    uint32_t cy = y;
    
    while (*str) {
        if (*str == '\n') {
            cx = x;
            cy += 16;
        } else if (*str >= 32 && *str <= 126) {
            vesa_draw_char(cx, cy, *str, fg_color, bg_color);
            cx += 8;
            
            // Wrap to next line if needed
            if (cx + 8 > g_current_mode.width) {
                cx = x;
                cy += 16;
            }
        }
        str++;
    }
}

// =============================================================================
// Mode Management
// =============================================================================

int vesa_set_mode(uint16_t mode_number) {
    // This would require real mode or VM86 mode to call VESA BIOS
    // For now, we assume the bootloader has set up the mode
    return -1;
}

void vesa_get_mode_info(vesa_mode_info_t* info) {
    if (info && g_vesa_initialized) {
        memcpy(info, &g_current_mode, sizeof(vesa_mode_info_t));
    }
}

// =============================================================================
// Initialization
// =============================================================================

int vesa_init(uint64_t framebuffer_addr, uint32_t width, uint32_t height,
             uint32_t pitch, uint8_t bpp) {
    if (g_vesa_initialized) {
        return 0;
    }
    
    // Map framebuffer
    size_t fb_size = pitch * height;
    g_framebuffer = (uint32_t*)framebuffer_addr;
    
    // TODO: Properly map framebuffer to virtual memory
    // flux_map_physical(framebuffer_addr, fb_size, FLUX_MAP_WRITE | FLUX_MAP_NOCACHE);
    
    // Setup mode info
    g_current_mode.width = width;
    g_current_mode.height = height;
    g_current_mode.pitch = pitch;
    g_current_mode.pitch_pixels = pitch / (bpp / 8);
    g_current_mode.bpp = bpp;
    g_current_mode.framebuffer = framebuffer_addr;
    
    // Assume RGB format
    if (bpp == 32) {
        g_current_mode.red_mask = 0x00FF0000;
        g_current_mode.red_shift = 16;
        g_current_mode.green_mask = 0x0000FF00;
        g_current_mode.green_shift = 8;
        g_current_mode.blue_mask = 0x000000FF;
        g_current_mode.blue_shift = 0;
    }
    
    g_vesa_initialized = true;
    
    // Clear screen
    vesa_clear(0x000000);
    
    return 0;
}

void vesa_clear(uint32_t color) {
    if (!g_vesa_initialized) {
        return;
    }
    
    vesa_fill_rect(0, 0, g_current_mode.width, g_current_mode.height, color);
}

// =============================================================================
// Driver Interface
// =============================================================================

static void* vesa_probe(device_node_t* node) {
    // VESA is typically detected via multiboot info or ACPI
    return &g_vesa_info;
}

static int vesa_attach(device_handle_t* handle) {
    // Initialize with default mode from bootloader
    // This info would come from multiboot or boot parameters
    return vesa_init(0xE0000000, 1024, 768, 4096, 32);
}

static void vesa_detach(device_handle_t* handle) {
    if (g_backbuffer) {
        flux_free(g_backbuffer);
        g_backbuffer = NULL;
    }
    g_vesa_initialized = false;
}

// Driver registration
static resonance_driver_t vesa_driver = {
    .name = "vesa",
    .probe = vesa_probe,
    .attach = vesa_attach,
    .detach = vesa_detach
};

void vesa_register(void) {
    resonance_register_driver(&vesa_driver);
}
