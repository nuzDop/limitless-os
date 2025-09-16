/*
 * VESA Graphics Driver Header
 * VESA BIOS Extensions framebuffer definitions
 */

#ifndef VESA_H
#define VESA_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "../resonance.h"

// =============================================================================
// VESA Constants
// =============================================================================

// VESA Function Numbers
#define VESA_GET_INFO           0x4F00
#define VESA_GET_MODE_INFO      0x4F01
#define VESA_SET_MODE           0x4F02
#define VESA_GET_CURRENT_MODE   0x4F03
#define VESA_SAVE_RESTORE_STATE 0x4F04
#define VESA_DISPLAY_WINDOW     0x4F05
#define VESA_SET_LOGICAL_SCAN   0x4F06
#define VESA_SET_DISPLAY_START  0x4F07
#define VESA_SET_DAC_PALETTE    0x4F08
#define VESA_GET_DAC_PALETTE    0x4F09
#define VESA_SET_PALETTE        0x4F0A

// VESA Return Status
#define VESA_SUCCESS            0x004F
#define VESA_FAILED             0x014F
#define VESA_NOT_SUPPORTED      0x024F
#define VESA_INVALID            0x034F

// Mode Attributes
#define VESA_MODE_SUPPORTED     0x0001
#define VESA_MODE_COLOR         0x0008
#define VESA_MODE_GRAPHICS      0x0010
#define VESA_MODE_NOT_VGA       0x0020
#define VESA_MODE_NO_BANK       0x0040
#define VESA_MODE_LINEAR_FB     0x0080
#define VESA_MODE_DOUBLE_SCAN   0x0100
#define VESA_MODE_INTERLACE     0x0200
#define VESA_MODE_TRIPLE_BUFFER 0x0400
#define VESA_MODE_STEREO        0x0800
#define VESA_MODE_DUAL_DISPLAY  0x1000

// Memory Models
#define VESA_MODEL_TEXT         0x00
#define VESA_MODEL_CGA          0x01
#define VESA_MODEL_HERCULES     0x02
#define VESA_MODEL_PLANAR       0x03
#define VESA_MODEL_PACKED_PIXEL 0x04
#define VESA_MODEL_NON_CHAIN4   0x05
#define VESA_MODEL_DIRECT_COLOR 0x06
#define VESA_MODEL_YUV          0x07

// Color formats
#define VESA_RGB888             0x00
#define VESA_BGR888             0x01
#define VESA_RGB565             0x02
#define VESA_RGB555             0x03

// =============================================================================
// VESA Data Structures
// =============================================================================

// VESA Info Block
typedef struct __attribute__((packed)) {
    char signature[4];          // "VESA"
    uint16_t version;          // VESA version
    uint32_t oem_string_ptr;   // Pointer to OEM string
    uint32_t capabilities;     // Capabilities
    uint32_t mode_list_ptr;    // Pointer to mode list
    uint16_t total_memory;     // Total memory in 64KB blocks
    uint16_t oem_software_rev;
    uint32_t oem_vendor_name_ptr;
    uint32_t oem_product_name_ptr;
    uint32_t oem_product_rev_ptr;
    uint8_t reserved[222];
    uint8_t oem_data[256];
} vesa_info_t;

// VESA Mode Info Block
typedef struct __attribute__((packed)) {
    uint16_t attributes;
    uint8_t window_a_attributes;
    uint8_t window_b_attributes;
    uint16_t window_granularity;
    uint16_t window_size;
    uint16_t window_a_segment;
    uint16_t window_b_segment;
    uint32_t window_function_ptr;
    uint16_t bytes_per_scanline;
    
    // VBE 1.2+
    uint16_t width;
    uint16_t height;
    uint8_t char_width;
    uint8_t char_height;
    uint8_t planes;
    uint8_t bpp;
    uint8_t banks;
    uint8_t memory_model;
    uint8_t bank_size;
    uint8_t image_pages;
    uint8_t reserved1;
    
    // Direct color
    uint8_t red_mask_size;
    uint8_t red_field_position;
    uint8_t green_mask_size;
    uint8_t green_field_position;
    uint8_t blue_mask_size;
    uint8_t blue_field_position;
    uint8_t reserved_mask_size;
    uint8_t reserved_field_position;
    uint8_t direct_color_mode_info;
    
    // VBE 2.0+
    uint32_t framebuffer;
    uint32_t off_screen_mem_offset;
    uint16_t off_screen_mem_size;
    uint8_t reserved2[206];
} vesa_mode_info_packed_t;

// Simplified mode info for runtime use
typedef struct {
    uint32_t width;
    uint32_t height;
    uint32_t pitch;
    uint32_t pitch_pixels;
    uint8_t bpp;
    uint64_t framebuffer;
    uint32_t red_mask;
    uint32_t green_mask;
    uint32_t blue_mask;
    uint8_t red_shift;
    uint8_t green_shift;
    uint8_t blue_shift;
} vesa_mode_info_t;

// Color structure
typedef struct {
    uint8_t blue;
    uint8_t green;
    uint8_t red;
    uint8_t alpha;
} vesa_color_t;

// Rectangle structure
typedef struct {
    int32_t x;
    int32_t y;
    uint32_t width;
    uint32_t height;
} vesa_rect_t;

// =============================================================================
// Function Prototypes
// =============================================================================

// Initialization
int vesa_init(uint64_t framebuffer_addr, uint32_t width, uint32_t height,
             uint32_t pitch, uint8_t bpp);
void vesa_register(void);

// Mode management
int vesa_set_mode(uint16_t mode_number);
void vesa_get_mode_info(vesa_mode_info_t* info);
uint32_t vesa_get_width(void);
uint32_t vesa_get_height(void);
uint8_t vesa_get_bpp(void);

// Pixel operations
void vesa_put_pixel(uint32_t x, uint32_t y, uint32_t color);
uint32_t vesa_get_pixel(uint32_t x, uint32_t y);

// Drawing primitives
void vesa_clear(uint32_t color);
void vesa_fill_rect(uint32_t x, uint32_t y, uint32_t width, uint32_t height,
                   uint32_t color);
void vesa_draw_line(int32_t x0, int32_t y0, int32_t x1, int32_t y1, uint32_t color);
void vesa_draw_circle(int32_t cx, int32_t cy, int32_t radius, uint32_t color);
void vesa_draw_rect(uint32_t x, uint32_t y, uint32_t width, uint32_t height,
                   uint32_t color);

// Bitmap operations
void vesa_blit(uint32_t dx, uint32_t dy, uint32_t width, uint32_t height,
              uint32_t* src, uint32_t src_pitch);
void vesa_scroll(int32_t dx, int32_t dy);

// Text rendering
void vesa_draw_char(uint32_t x, uint32_t y, char c, uint32_t fg_color, uint32_t bg_color);
void vesa_draw_string(uint32_t x, uint32_t y, const char* str,
                     uint32_t fg_color, uint32_t bg_color);

// Double buffering
int vesa_enable_double_buffer(void);
void vesa_disable_double_buffer(void);
void vesa_swap_buffers(void);

// Color conversion
static inline uint32_t vesa_rgb(uint8_t r, uint8_t g, uint8_t b) {
    return (r << 16) | (g << 8) | b;
}

static inline uint32_t vesa_rgba(uint8_t r, uint8_t g, uint8_t b, uint8_t a) {
    return (a << 24) | (r << 16) | (g << 8) | b;
}

// Helper functions
int abs(int x);

#endif /* VESA_H */
