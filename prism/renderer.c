/*
 * Prism Renderer
 * GPU-accelerated rendering backend
 */

#include "renderer.h"
#include "prism.h"
#include "../continuum/flux_memory.h"
#include "../continuum/drivers/graphics/vesa.h"

// =============================================================================
// Renderer State
// =============================================================================

typedef struct {
    // Render targets
    uint32_t* front_buffer;
    uint32_t* back_buffer;
    uint32_t buffer_width;
    uint32_t buffer_height;
    uint32_t buffer_stride;
    
    // Shader programs (for future GPU support)
    void* surface_shader;
    void* blur_shader;
    void* shadow_shader;
    
    // Texture cache
    struct {
        uint32_t id;
        void* data;
        uint32_t width;
        uint32_t height;
        uint64_t last_used;
    } texture_cache[256];
    uint32_t texture_count;
    
    // Blur buffers
    uint32_t* blur_buffer_h;
    uint32_t* blur_buffer_v;
    
    // Shadow cache
    uint32_t* shadow_texture;
    uint32_t shadow_size;
} prism_renderer_t;

static prism_renderer_t* g_renderer;

// =============================================================================
// Pixel Operations
// =============================================================================

uint32_t prism_alpha_blend(uint32_t dst, uint32_t src) {
    uint8_t src_a = (src >> 24) & 0xFF;
    
    if (src_a == 0) {
        return dst;
    }
    if (src_a == 255) {
        return src;
    }
    
    uint8_t dst_r = (dst >> 16) & 0xFF;
    uint8_t dst_g = (dst >> 8) & 0xFF;
    uint8_t dst_b = dst & 0xFF;
    uint8_t dst_a = (dst >> 24) & 0xFF;
    
    uint8_t src_r = (src >> 16) & 0xFF;
    uint8_t src_g = (src >> 8) & 0xFF;
    uint8_t src_b = src & 0xFF;
    
    // Porter-Duff "over" operator
    uint8_t out_a = src_a + (dst_a * (255 - src_a)) / 255;
    uint8_t out_r = (src_r * src_a + dst_r * dst_a * (255 - src_a) / 255) / out_a;
    uint8_t out_g = (src_g * src_a + dst_g * dst_a * (255 - src_a) / 255) / out_a;
    uint8_t out_b = (src_b * src_a + dst_b * dst_a * (255 - src_a) / 255) / out_a;
    
    return (out_a << 24) | (out_r << 16) | (out_g << 8) | out_b;
}

uint32_t prism_blend_alpha(uint32_t pixel, float alpha) {
    uint8_t a = (pixel >> 24) & 0xFF;
    a = (uint8_t)(a * alpha);
    return (pixel & 0x00FFFFFF) | (a << 24);
}

uint32_t prism_sample_pixel(prism_buffer_t* buffer, float x, float y) {
    if (!buffer || !buffer->data) {
        return 0;
    }
    
    // Clamp coordinates
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if (x >= buffer->width - 1) x = buffer->width - 1;
    if (y >= buffer->height - 1) y = buffer->height - 1;
    
    // Simple nearest neighbor for now
    // TODO: Implement bilinear filtering
    uint32_t ix = (uint32_t)x;
    uint32_t iy = (uint32_t)y;
    
    uint32_t* pixels = (uint32_t*)buffer->data;
    return pixels[iy * (buffer->stride / 4) + ix];
}

// =============================================================================
// Clear and Fill
// =============================================================================

void prism_clear_output(prism_output_t* output) {
    if (!output || !output->framebuffer) {
        return;
    }
    
    // Clear to desktop background color
    uint32_t bg_color = 0xFF1E1E2E;  // Dark background
    
    for (uint32_t y = 0; y < output->height; y++) {
        for (uint32_t x = 0; x < output->width; x++) {
            output->framebuffer[y * output->fb_stride + x] = bg_color;
        }
    }
}

// =============================================================================
// Shadow Rendering
// =============================================================================

void prism_render_shadow(prism_output_t* output, prism_rect_t* rect) {
    const int shadow_radius = 20;
    const int shadow_offset_x = 0;
    const int shadow_offset_y = 5;
    const uint8_t shadow_alpha = 64;
    
    // Calculate shadow bounds
    int32_t shadow_x = rect->x + shadow_offset_x - shadow_radius;
    int32_t shadow_y = rect->y + shadow_offset_y - shadow_radius;
    uint32_t shadow_w = rect->width + shadow_radius * 2;
    uint32_t shadow_h = rect->height + shadow_radius * 2;
    
    // Render gaussian blur shadow
    for (int32_t y = shadow_y; y < shadow_y + shadow_h; y++) {
        for (int32_t x = shadow_x; x < shadow_x + shadow_w; x++) {
            if (x < 0 || y < 0 || x >= output->width || y >= output->height) {
                continue;
            }
            
            // Calculate distance from shadow rect
            int32_t dx = 0, dy = 0;
            if (x < rect->x + shadow_offset_x) {
                dx = rect->x + shadow_offset_x - x;
            } else if (x >= rect->x + shadow_offset_x + rect->width) {
                dx = x - (rect->x + shadow_offset_x + rect->width) + 1;
            }
            
            if (y < rect->y + shadow_offset_y) {
                dy = rect->y + shadow_offset_y - y;
            } else if (y >= rect->y + shadow_offset_y + rect->height) {
                dy = y - (rect->y + shadow_offset_y + rect->height) + 1;
            }
            
            float dist = sqrtf(dx * dx + dy * dy);
            if (dist < shadow_radius) {
                float alpha = (1.0f - dist / shadow_radius) * shadow_alpha / 255.0f;
                uint32_t shadow_color = ((uint8_t)(alpha * 255) << 24);
                
                uint32_t* pixel = &output->framebuffer[y * output->fb_stride + x];
                *pixel = prism_alpha_blend(*pixel, shadow_color);
            }
        }
    }
}

// =============================================================================
// Blur Effect
// =============================================================================

void prism_apply_blur_pass(prism_output_t* output) {
    if (!g_renderer || !g_renderer->blur_buffer_h || !g_renderer->blur_buffer_v) {
        return;
    }
    
    const int blur_radius = 10;
    const float sigma = blur_radius / 3.0f;
    
    // Generate gaussian kernel
    float kernel[blur_radius * 2 + 1];
    float kernel_sum = 0;
    
    for (int i = -blur_radius; i <= blur_radius; i++) {
        float val = expf(-(i * i) / (2 * sigma * sigma));
        kernel[i + blur_radius] = val;
        kernel_sum += val;
    }
    
    // Normalize kernel
    for (int i = 0; i < blur_radius * 2 + 1; i++) {
        kernel[i] /= kernel_sum;
    }
    
    // Horizontal pass
    for (uint32_t y = 0; y < output->height; y++) {
        for (uint32_t x = 0; x < output->width; x++) {
            float r = 0, g = 0, b = 0, a = 0;
            
            for (int i = -blur_radius; i <= blur_radius; i++) {
                int32_t sx = x + i;
                if (sx < 0) sx = 0;
                if (sx >= output->width) sx = output->width - 1;
                
                uint32_t pixel = output->framebuffer[y * output->fb_stride + sx];
                float weight = kernel[i + blur_radius];
                
                a += ((pixel >> 24) & 0xFF) * weight;
                r += ((pixel >> 16) & 0xFF) * weight;
                g += ((pixel >> 8) & 0xFF) * weight;
                b += (pixel & 0xFF) * weight;
            }
            
            g_renderer->blur_buffer_h[y * output->width + x] = 
                ((uint8_t)a << 24) | ((uint8_t)r << 16) | ((uint8_t)g << 8) | (uint8_t)b;
        }
    }
    
    // Vertical pass
    for (uint32_t y = 0; y < output->height; y++) {
        for (uint32_t x = 0; x < output->width; x++) {
            float r = 0, g = 0, b = 0, a = 0;
            
            for (int i = -blur_radius; i <= blur_radius; i++) {
                int32_t sy = y + i;
                if (sy < 0) sy = 0;
                if (sy >= output->height) sy = output->height - 1;
                
                uint32_t pixel = g_renderer->blur_buffer_h[sy * output->width + x];
                float weight = kernel[i + blur_radius];
                
                a += ((pixel >> 24) & 0xFF) * weight;
                r += ((pixel >> 16) & 0xFF) * weight;
                g += ((pixel >> 8) & 0xFF) * weight;
                b += (pixel & 0xFF) * weight;
            }
            
            output->framebuffer[y * output->fb_stride + x] = 
                ((uint8_t)a << 24) | ((uint8_t)r << 16) | ((uint8_t)g << 8) | (uint8_t)b;
        }
    }
}

// =============================================================================
// Matrix Operations
// =============================================================================

void prism_matrix_identity(prism_matrix_t* matrix) {
    matrix->m[0] = 1.0f; matrix->m[1] = 0.0f; matrix->m[2] = 0.0f;
    matrix->m[3] = 0.0f; matrix->m[4] = 1.0f; matrix->m[5] = 0.0f;
    matrix->m[6] = 0.0f; matrix->m[7] = 0.0f; matrix->m[8] = 1.0f;
}

void prism_matrix_translate(prism_matrix_t* matrix, float x, float y) {
    prism_matrix_t translation;
    prism_matrix_identity(&translation);
    translation.m[2] = x;
    translation.m[5] = y;
    
    prism_matrix_t result;
    prism_matrix_multiply(&result, matrix, &translation);
    *matrix = result;
}

void prism_matrix_scale(prism_matrix_t* matrix, float x, float y) {
    prism_matrix_t scale;
    prism_matrix_identity(&scale);
    scale.m[0] = x;
    scale.m[4] = y;
    
    prism_matrix_t result;
    prism_matrix_multiply(&result, matrix, &scale);
    *matrix = result;
}

void prism_matrix_rotate(prism_matrix_t* matrix, float angle) {
    float c = cosf(angle);
    float s = sinf(angle);
    
    prism_matrix_t rotation;
    prism_matrix_identity(&rotation);
    rotation.m[0] = c;  rotation.m[1] = -s;
    rotation.m[3] = s;  rotation.m[4] = c;
    
    prism_matrix_t result;
    prism_matrix_multiply(&result, matrix, &rotation);
    *matrix = result;
}

void prism_matrix_multiply(prism_matrix_t* result, prism_matrix_t* a, prism_matrix_t* b) {
    result->m[0] = a->m[0] * b->m[0] + a->m[1] * b->m[3] + a->m[2] * b->m[6];
    result->m[1] = a->m[0] * b->m[1] + a->m[1] * b->m[4] + a->m[2] * b->m[7];
    result->m[2] = a->m[0] * b->m[2] + a->m[1] * b->m[5] + a->m[2] * b->m[8];
    
    result->m[3] = a->m[3] * b->m[0] + a->m[4] * b->m[3] + a->m[5] * b->m[6];
    result->m[4] = a->m[3] * b->m[1] + a->m[4] * b->m[4] + a->m[5] * b->m[7];
    result->m[5] = a->m[3] * b->m[2] + a->m[4] * b->m[5] + a->m[5] * b->m[8];
    
    result->m[6] = a->m[6] * b->m[0] + a->m[7] * b->m[3] + a->m[8] * b->m[6];
    result->m[7] = a->m[6] * b->m[1] + a->m[7] * b->m[4] + a->m[8] * b->m[7];
    result->m[8] = a->m[6] * b->m[2] + a->m[7] * b->m[5] + a->m[8] * b->m[8];
}

void prism_matrix_transform_point(prism_matrix_t* matrix, float x, float y,
                                 float* tx, float* ty) {
    *tx = matrix->m[0] * x + matrix->m[1] * y + matrix->m[2];
    *ty = matrix->m[3] * x + matrix->m[4] * y + matrix->m[5];
}

// =============================================================================
// Renderer Initialization
// =============================================================================

void* prism_renderer_create(void) {
    g_renderer = flux_allocate(NULL, sizeof(prism_renderer_t),
                             FLUX_ALLOC_KERNEL | FLUX_ALLOC_ZERO);
    if (!g_renderer) {
        return NULL;
    }
    
    // Allocate blur buffers (for 4K support)
    g_renderer->blur_buffer_h = flux_allocate(NULL, 3840 * 2160 * 4,
                                             FLUX_ALLOC_KERNEL);
    g_renderer->blur_buffer_v = flux_allocate(NULL, 3840 * 2160 * 4,
                                             FLUX_ALLOC_KERNEL);
    
    // Initialize shadow texture
    g_renderer->shadow_size = 256;
    g_renderer->shadow_texture = flux_allocate(NULL, 256 * 256 * 4,
                                              FLUX_ALLOC_KERNEL);
    
    // Generate shadow texture
    prism_generate_shadow_texture();
    
    return g_renderer;
}

void prism_renderer_destroy(void* renderer) {
    if (g_renderer) {
        if (g_renderer->blur_buffer_h) {
            flux_free(g_renderer->blur_buffer_h);
        }
        if (g_renderer->blur_buffer_v) {
            flux_free(g_renderer->blur_buffer_v);
        }
        if (g_renderer->shadow_texture) {
            flux_free(g_renderer->shadow_texture);
        }
        flux_free(g_renderer);
        g_renderer = NULL;
    }
}

// =============================================================================
// Helper Functions
// =============================================================================

float prism_lerp(float a, float b, float t) {
    return a + (b - a) * t;
}

float prism_ease_in_out_cubic(float t) {
    if (t < 0.5f) {
        return 4 * t * t * t;
    } else {
        float p = 2 * t - 2;
        return 1 + p * p * p / 2;
    }
}

bool prism_rect_contains_point(prism_rect_t* rect, prism_point_t* point) {
    return point->x >= rect->x && 
           point->x < rect->x + rect->width &&
           point->y >= rect->y && 
           point->y < rect->y + rect->height;
}

bool prism_rect_intersects(prism_rect_t* a, prism_rect_t* b) {
    return !(a->x + a->width <= b->x ||
            b->x + b->width <= a->x ||
            a->y + a->height <= b->y ||
            b->y + b->height <= a->y);
}
