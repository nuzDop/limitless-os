/*
 * Terminal Emulator
 * Basic terminal application for Limitless OS
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "../../prism/prism.h"
#include "../../manifold/manifold.h"

#define TERM_WIDTH  80
#define TERM_HEIGHT 25
#define CHAR_WIDTH  9
#define CHAR_HEIGHT 16

typedef struct {
    prism_surface_t* surface;
    char buffer[TERM_HEIGHT][TERM_WIDTH];
    uint8_t colors[TERM_HEIGHT][TERM_WIDTH];
    uint32_t cursor_x;
    uint32_t cursor_y;
    bool cursor_visible;
    
    // Shell process
    pid_t shell_pid;
    int master_fd;
    int slave_fd;
} terminal_t;

static terminal_t* g_terminal;

// =============================================================================
// Terminal Operations
// =============================================================================

void terminal_init(void) {
    g_terminal = calloc(1, sizeof(terminal_t));
    
    // Create window
    g_terminal->surface = prism_create_window(TERM_WIDTH * CHAR_WIDTH,
                                             TERM_HEIGHT * CHAR_HEIGHT,
                                             "Terminal");
    
    // Clear buffer
    terminal_clear();
    
    // Create pseudo-terminal
    if (openpty(&g_terminal->master_fd, &g_terminal->slave_fd,
               NULL, NULL, NULL) < 0) {
        fprintf(stderr, "Failed to create PTY\n");
        return;
    }
    
    // Fork shell
    g_terminal->shell_pid = fork();
    if (g_terminal->shell_pid == 0) {
        // Child process
        close(g_terminal->master_fd);
        
        // Set up PTY
        setsid();
        ioctl(g_terminal->slave_fd, TIOCSCTTY, 0);
        
        // Redirect stdio
        dup2(g_terminal->slave_fd, 0);
        dup2(g_terminal->slave_fd, 1);
        dup2(g_terminal->slave_fd, 2);
        close(g_terminal->slave_fd);
        
        // Execute shell
        execl("/bin/sh", "sh", NULL);
        exit(1);
    }
    
    // Parent process
    close(g_terminal->slave_fd);
    
    // Start input/output threads
    terminal_start_io_threads();
}

void terminal_clear(void) {
    memset(g_terminal->buffer, ' ', sizeof(g_terminal->buffer));
    memset(g_terminal->colors, 0x07, sizeof(g_terminal->colors));
    g_terminal->cursor_x = 0;
    g_terminal->cursor_y = 0;
    terminal_redraw();
}

void terminal_putchar(char c) {
    switch (c) {
        case '\n':
            g_terminal->cursor_x = 0;
            g_terminal->cursor_y++;
            break;
            
        case '\r':
            g_terminal->cursor_x = 0;
            break;
            
        case '\b':
            if (g_terminal->cursor_x > 0) {
                g_terminal->cursor_x--;
                g_terminal->buffer[g_terminal->cursor_y][g_terminal->cursor_x] = ' ';
            }
            break;
            
        case '\t':
            g_terminal->cursor_x = (g_terminal->cursor_x + 8) & ~7;
            break;
            
        default:
            if (c >= 32 && c < 127) {
                g_terminal->buffer[g_terminal->cursor_y][g_terminal->cursor_x] = c;
                g_terminal->cursor_x++;
            }
            break;
    }
    
    // Handle line wrap
    if (g_terminal->cursor_x >= TERM_WIDTH) {
        g_terminal->cursor_x = 0;
        g_terminal->cursor_y++;
    }
    
    // Handle scroll
    if (g_terminal->cursor_y >= TERM_HEIGHT) {
        terminal_scroll();
        g_terminal->cursor_y = TERM_HEIGHT - 1;
    }
    
    terminal_redraw();
}

void terminal_scroll(void) {
    // Move lines up
    memmove(g_terminal->buffer[0], g_terminal->buffer[1],
           (TERM_HEIGHT - 1) * TERM_WIDTH);
    memmove(g_terminal->colors[0], g_terminal->colors[1],
           (TERM_HEIGHT - 1) * TERM_WIDTH);
    
    // Clear last line
    memset(g_terminal->buffer[TERM_HEIGHT - 1], ' ', TERM_WIDTH);
    memset(g_terminal->colors[TERM_HEIGHT - 1], 0x07, TERM_WIDTH);
}

// =============================================================================
// Rendering
// =============================================================================

void terminal_redraw(void) {
    uint32_t* framebuffer = prism_surface_get_buffer(g_terminal->surface);
    if (!framebuffer) {
        return;
    }
    
    // Clear background
    for (int y = 0; y < TERM_HEIGHT * CHAR_HEIGHT; y++) {
        for (int x = 0; x < TERM_WIDTH * CHAR_WIDTH; x++) {
            framebuffer[y * TERM_WIDTH * CHAR_WIDTH + x] = 0xFF000000;
        }
    }
    
    // Draw characters
    for (int row = 0; row < TERM_HEIGHT; row++) {
        for (int col = 0; col < TERM_WIDTH; col++) {
            char c = g_terminal->buffer[row][col];
            uint8_t color = g_terminal->colors[row][col];
            
            uint32_t fg = terminal_get_color(color & 0x0F);
            uint32_t bg = terminal_get_color((color >> 4) & 0x0F);
            
            terminal_draw_char(framebuffer, col * CHAR_WIDTH, row * CHAR_HEIGHT,
                             c, fg, bg);
        }
    }
    
    // Draw cursor
    if (g_terminal->cursor_visible) {
        int x = g_terminal->cursor_x * CHAR_WIDTH;
        int y = g_terminal->cursor_y * CHAR_HEIGHT;
        
        for (int i = 0; i < CHAR_HEIGHT; i++) {
            framebuffer[(y + i) * TERM_WIDTH * CHAR_WIDTH + x] = 0xFFFFFFFF;
        }
    }
    
    prism_surface_damage_all(g_terminal->surface);
    prism_surface_commit(g_terminal->surface);
}

// =============================================================================
// Input Handling
// =============================================================================

void terminal_handle_key(uint32_t key, bool pressed) {
    if (!pressed) {
        return;
    }
    
    char c = terminal_key_to_char(key);
    if (c) {
        // Send to shell
        write(g_terminal->master_fd, &c, 1);
    }
}

// =============================================================================
// Main Entry
// =============================================================================

int main(int argc, char* argv[]) {
    // Connect to compositor
    if (prism_connect() < 0) {
        fprintf(stderr, "Failed to connect to compositor\n");
        return 1;
    }
    
    // Initialize terminal
    terminal_init();
    
    // Main event loop
    while (1) {
        prism_event_t event;
        if (prism_wait_event(&event) == 0) {
            switch (event.type) {
                case PRISM_EVENT_KEY:
                    terminal_handle_key(event.key.keycode, event.key.pressed);
                    break;
                    
                case PRISM_EVENT_CLOSE:
                    goto cleanup;
                    
                default:
                    break;
            }
        }
    }
    
cleanup:
    // Clean up
    if (g_terminal->shell_pid > 0) {
        kill(g_terminal->shell_pid, SIGTERM);
    }
    
    prism_destroy_surface(g_terminal->surface);
    prism_disconnect();
    
    return 0;
}
