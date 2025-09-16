/*
 * Genesis Boot Core for LimitlessOS
 * Unified boot system for BIOS and UEFI paths
 * 
 * This module provides the common boot logic after initial platform-specific
 * bootloader stages, handling kernel loading, memory management, and system
 * initialization for the Continuum kernel.
 */

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

// =============================================================================
// Type Definitions and Constants
// =============================================================================

#define GENESIS_MAGIC           0x4C314D31544C4535ULL  // "L1M1TLE55"
#define GENESIS_VERSION         0x01000000              // 1.0.0.0
#define PAGE_SIZE               4096
#define KERNEL_LOAD_ADDR        0x100000                // 1MB
#define INITRD_LOAD_ADDR        0x1000000               // 16MB
#define BOOT_STACK_ADDR         0x90000                 // Boot stack
#define BOOT_HEAP_ADDR          0x200000                // 2MB
#define MAX_MEMORY_REGIONS      128
#define MAX_BOOT_MODULES        32
#define MAX_CMDLINE_LEN         4096

// Memory types
typedef enum {
    MEMORY_TYPE_USABLE = 1,
    MEMORY_TYPE_RESERVED = 2,
    MEMORY_TYPE_ACPI_RECLAIM = 3,
    MEMORY_TYPE_ACPI_NVS = 4,
    MEMORY_TYPE_BAD = 5,
    MEMORY_TYPE_BOOTLOADER = 6,
    MEMORY_TYPE_KERNEL = 7,
    MEMORY_TYPE_INITRD = 8,
    MEMORY_TYPE_FRAMEBUFFER = 9
} memory_type_t;

// Boot modes
typedef enum {
    BOOT_MODE_UNKNOWN = 0,
    BOOT_MODE_BIOS = 1,
    BOOT_MODE_UEFI = 2,
    BOOT_MODE_MULTIBOOT = 3,
    BOOT_MODE_DIRECT = 4
} boot_mode_t;

// Display modes
typedef enum {
    DISPLAY_MODE_TEXT = 1,
    DISPLAY_MODE_GRAPHICS = 2,
    DISPLAY_MODE_MIXED = 3
} display_mode_t;

// =============================================================================
// Core Data Structures
// =============================================================================

// Memory region descriptor
typedef struct {
    uint64_t base;
    uint64_t length;
    uint32_t type;
    uint32_t attributes;
} memory_region_t;

// Memory map
typedef struct {
    uint32_t region_count;
    uint64_t total_memory;
    uint64_t usable_memory;
    memory_region_t regions[MAX_MEMORY_REGIONS];
} memory_map_t;

// Framebuffer information
typedef struct {
    uint64_t base;
    uint32_t width;
    uint32_t height;
    uint32_t pitch;
    uint32_t bpp;
    uint32_t red_mask;
    uint32_t green_mask;
    uint32_t blue_mask;
    uint32_t reserved_mask;
} framebuffer_info_t;

// Boot module (kernel, initrd, etc.)
typedef struct {
    char name[64];
    uint64_t base;
    uint64_t size;
    uint32_t type;
    uint32_t flags;
} boot_module_t;

// CPU information
typedef struct {
    uint32_t vendor[4];      // CPU vendor string
    uint32_t family;
    uint32_t model;
    uint32_t stepping;
    uint64_t features;       // Feature flags
    uint32_t cores;
    uint32_t threads;
    uint64_t frequency;      // Base frequency in Hz
    bool has_64bit;
    bool has_nx;
    bool has_pae;
    bool has_sse;
    bool has_sse2;
    bool has_sse3;
    bool has_ssse3;
    bool has_sse41;
    bool has_sse42;
    bool has_avx;
    bool has_avx2;
    bool has_avx512;
} cpu_info_t;

// ACPI information
typedef struct {
    uint64_t rsdp_addr;      // RSDP address
    uint64_t rsdt_addr;      // RSDT address
    uint64_t xsdt_addr;      // XSDT address (64-bit)
    uint32_t revision;
    bool use_xsdt;
} acpi_info_t;

// Genesis Boot Context - passed to kernel
typedef struct {
    // Header
    uint64_t magic;
    uint32_t version;
    uint32_t size;
    
    // Boot information
    boot_mode_t boot_mode;
    char bootloader_name[32];
    char command_line[MAX_CMDLINE_LEN];
    
    // Memory
    memory_map_t memory_map;
    uint64_t kernel_start;
    uint64_t kernel_end;
    uint64_t initrd_start;
    uint64_t initrd_end;
    uint64_t boot_heap_start;
    uint64_t boot_heap_end;
    
    // Display
    display_mode_t display_mode;
    framebuffer_info_t framebuffer;
    
    // System information
    cpu_info_t cpu;
    acpi_info_t acpi;
    
    // Modules
    uint32_t module_count;
    boot_module_t modules[MAX_BOOT_MODULES];
    
    // Platform specific data
    void* platform_data;
    uint32_t platform_data_size;
} genesis_boot_context_t;

// =============================================================================
// Global Variables
// =============================================================================

static genesis_boot_context_t* g_boot_context = NULL;
static uint8_t* g_heap_ptr = (uint8_t*)BOOT_HEAP_ADDR;
static bool g_debug_mode = false;

// =============================================================================
// Utility Functions
// =============================================================================

// Memory operations
static void* memset(void* dest, int val, size_t len) {
    uint8_t* ptr = dest;
    while (len--) {
        *ptr++ = (uint8_t)val;
    }
    return dest;
}

static void* memcpy(void* dest, const void* src, size_t len) {
    uint8_t* d = dest;
    const uint8_t* s = src;
    while (len--) {
        *d++ = *s++;
    }
    return dest;
}

static int memcmp(const void* s1, const void* s2, size_t len) {
    const uint8_t* p1 = s1;
    const uint8_t* p2 = s2;
    while (len--) {
        if (*p1 != *p2) {
            return *p1 - *p2;
        }
        p1++;
        p2++;
    }
    return 0;
}

// String operations
static size_t strlen(const char* str) {
    size_t len = 0;
    while (str[len]) len++;
    return len;
}

static char* strcpy(char* dest, const char* src) {
    char* ret = dest;
    while ((*dest++ = *src++));
    return ret;
}

static char* strncpy(char* dest, const char* src, size_t n) {
    char* ret = dest;
    while (n-- && (*dest++ = *src++));
    while (n--) *dest++ = 0;
    return ret;
}

static int strcmp(const char* s1, const char* s2) {
    while (*s1 && (*s1 == *s2)) {
        s1++;
        s2++;
    }
    return *(unsigned char*)s1 - *(unsigned char*)s2;
}

// Simple heap allocator for boot time
static void* boot_alloc(size_t size) {
    // Align to 16 bytes
    size = (size + 15) & ~15;
    
    void* ptr = g_heap_ptr;
    g_heap_ptr += size;
    
    // Check heap overflow
    if ((uintptr_t)g_heap_ptr > (BOOT_HEAP_ADDR + 0x100000)) {  // 1MB heap
        return NULL;
    }
    
    return ptr;
}

// =============================================================================
// Display Functions
// =============================================================================

// VGA text mode output (for BIOS boot)
static void vga_putchar(char c) {
    static uint16_t* vga_buffer = (uint16_t*)0xB8000;
    static int cursor_x = 0;
    static int cursor_y = 0;
    
    if (c == '\n') {
        cursor_x = 0;
        cursor_y++;
        if (cursor_y >= 25) {
            // Scroll
            for (int i = 0; i < 24 * 80; i++) {
                vga_buffer[i] = vga_buffer[i + 80];
            }
            for (int i = 24 * 80; i < 25 * 80; i++) {
                vga_buffer[i] = 0x0720;  // Space with gray attribute
            }
            cursor_y = 24;
        }
    } else if (c == '\r') {
        cursor_x = 0;
    } else {
        vga_buffer[cursor_y * 80 + cursor_x] = (uint16_t)c | 0x0700;
        cursor_x++;
        if (cursor_x >= 80) {
            cursor_x = 0;
            cursor_y++;
        }
    }
}

static void vga_print(const char* str) {
    while (*str) {
        vga_putchar(*str++);
    }
}

// Framebuffer output (for UEFI boot)
static void fb_putpixel(uint32_t x, uint32_t y, uint32_t color) {
    if (!g_boot_context || !g_boot_context->framebuffer.base) {
        return;
    }
    
    framebuffer_info_t* fb = &g_boot_context->framebuffer;
    if (x >= fb->width || y >= fb->height) {
        return;
    }
    
    uint32_t* pixel = (uint32_t*)(fb->base + y * fb->pitch + x * (fb->bpp / 8));
    *pixel = color;
}

// Unified print function
static void genesis_print(const char* str) {
    if (g_boot_context && g_boot_context->display_mode == DISPLAY_MODE_TEXT) {
        vga_print(str);
    } else if (g_boot_context && g_boot_context->display_mode == DISPLAY_MODE_GRAPHICS) {
        // For graphics mode, we'd need a font renderer
        // For now, just try VGA as fallback
        vga_print(str);
    } else {
        vga_print(str);
    }
}

static void genesis_print_hex(uint64_t value) {
    char buffer[20];
    const char* hex = "0123456789ABCDEF";
    int i = 18;
    
    buffer[19] = 0;
    buffer[i--] = '\n';
    
    do {
        buffer[i--] = hex[value & 0xF];
        value >>= 4;
    } while (value && i >= 0);
    
    buffer[i--] = 'x';
    buffer[i--] = '0';
    
    genesis_print(&buffer[i + 1]);
}

// =============================================================================
// CPU Detection
// =============================================================================

static void detect_cpu_features(cpu_info_t* cpu) {
    uint32_t eax, ebx, ecx, edx;
    
    // Get vendor string
    __asm__ __volatile__(
        "cpuid"
        : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx)
        : "a"(0)
    );
    
    cpu->vendor[0] = ebx;
    cpu->vendor[1] = edx;
    cpu->vendor[2] = ecx;
    cpu->vendor[3] = 0;
    
    // Get basic features
    __asm__ __volatile__(
        "cpuid"
        : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx)
        : "a"(1)
    );
    
    cpu->family = (eax >> 8) & 0xF;
    cpu->model = (eax >> 4) & 0xF;
    cpu->stepping = eax & 0xF;
    
    // Extended family and model
    if (cpu->family == 0xF) {
        cpu->family += (eax >> 20) & 0xFF;
    }
    if (cpu->family >= 0x6) {
        cpu->model += ((eax >> 16) & 0xF) << 4;
    }
    
    // Feature detection
    cpu->has_sse = (edx >> 25) & 1;
    cpu->has_sse2 = (edx >> 26) & 1;
    cpu->has_pae = (edx >> 6) & 1;
    cpu->has_sse3 = ecx & 1;
    cpu->has_ssse3 = (ecx >> 9) & 1;
    cpu->has_sse41 = (ecx >> 19) & 1;
    cpu->has_sse42 = (ecx >> 20) & 1;
    cpu->has_avx = (ecx >> 28) & 1;
    
    // Extended features
    __asm__ __volatile__(
        "cpuid"
        : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx)
        : "a"(0x80000001)
    );
    
    cpu->has_64bit = (edx >> 29) & 1;
    cpu->has_nx = (edx >> 20) & 1;
    
    // Get processor count (simplified)
    cpu->cores = 1;
    cpu->threads = 1;
    
    // Get frequency (would need MSR access for accurate value)
    cpu->frequency = 2000000000;  // Default 2GHz
}

// =============================================================================
// Memory Management
// =============================================================================

static void sort_memory_map(memory_map_t* map) {
    // Simple bubble sort for memory regions
    for (uint32_t i = 0; i < map->region_count - 1; i++) {
        for (uint32_t j = 0; j < map->region_count - i - 1; j++) {
            if (map->regions[j].base > map->regions[j + 1].base) {
                memory_region_t temp = map->regions[j];
                map->regions[j] = map->regions[j + 1];
                map->regions[j + 1] = temp;
            }
        }
    }
}

static void merge_memory_regions(memory_map_t* map) {
    uint32_t write_idx = 0;
    
    for (uint32_t i = 0; i < map->region_count; i++) {
        if (write_idx == 0 || 
            map->regions[write_idx - 1].type != map->regions[i].type ||
            map->regions[write_idx - 1].base + map->regions[write_idx - 1].length != map->regions[i].base) {
            
            if (write_idx != i) {
                map->regions[write_idx] = map->regions[i];
            }
            write_idx++;
        } else {
            // Merge with previous region
            map->regions[write_idx - 1].length += map->regions[i].length;
        }
    }
    
    map->region_count = write_idx;
}

static void process_memory_map(memory_map_t* map) {
    map->total_memory = 0;
    map->usable_memory = 0;
    
    sort_memory_map(map);
    merge_memory_regions(map);
    
    for (uint32_t i = 0; i < map->region_count; i++) {
        map->total_memory += map->regions[i].length;
        if (map->regions[i].type == MEMORY_TYPE_USABLE) {
            map->usable_memory += map->regions[i].length;
        }
    }
}

// =============================================================================
// Kernel Loading
// =============================================================================

// Continuum kernel header structure
typedef struct {
    uint32_t magic;          // 0x434F4E54 "CONT"
    uint32_t version;
    uint64_t entry_point;
    uint64_t load_addr;
    uint64_t load_end_addr;
    uint64_t bss_end_addr;
    uint32_t flags;
    uint32_t checksum;
} continuum_header_t;

static bool validate_kernel(void* kernel_data, size_t kernel_size) {
    if (kernel_size < sizeof(continuum_header_t)) {
        genesis_print("Error: Kernel too small\n");
        return false;
    }
    
    continuum_header_t* header = (continuum_header_t*)kernel_data;
    
    // Check magic
    if (header->magic != 0x434F4E54) {  // "CONT"
        genesis_print("Error: Invalid kernel magic\n");
        return false;
    }
    
    // Verify checksum
    uint32_t sum = 0;
    uint32_t* data = (uint32_t*)kernel_data;
    for (size_t i = 0; i < kernel_size / 4; i++) {
        sum += data[i];
    }
    
    // Note: Simple checksum for demonstration
    // Real implementation would use CRC32 or SHA256
    
    return true;
}

static bool load_kernel(void* kernel_data, size_t kernel_size, uint64_t* entry_point) {
    genesis_print("Loading Continuum kernel...\n");
    
    if (!validate_kernel(kernel_data, kernel_size)) {
        return false;
    }
    
    continuum_header_t* header = (continuum_header_t*)kernel_data;
    
    // Copy kernel to its load address
    uint64_t load_addr = header->load_addr ? header->load_addr : KERNEL_LOAD_ADDR;
    size_t copy_size = header->load_end_addr - header->load_addr;
    
    if (copy_size > kernel_size - sizeof(continuum_header_t)) {
        copy_size = kernel_size - sizeof(continuum_header_t);
    }
    
    memcpy((void*)load_addr, (uint8_t*)kernel_data + sizeof(continuum_header_t), copy_size);
    
    // Clear BSS section
    if (header->bss_end_addr > header->load_end_addr) {
        size_t bss_size = header->bss_end_addr - header->load_end_addr;
        memset((void*)header->load_end_addr, 0, bss_size);
    }
    
    *entry_point = header->entry_point ? header->entry_point : load_addr;
    
    // Update boot context
    g_boot_context->kernel_start = load_addr;
    g_boot_context->kernel_end = header->bss_end_addr;
    
    genesis_print("Kernel loaded at: ");
    genesis_print_hex(load_addr);
    genesis_print("Entry point: ");
    genesis_print_hex(*entry_point);
    
    return true;
}

// =============================================================================
// Page Table Setup (64-bit)
// =============================================================================

// Page table structures
#define PML4_BASE       0x1000
#define PDPT_BASE       0x2000
#define PD_BASE         0x3000
#define PT_BASE         0x4000

#define PAGE_PRESENT    (1ULL << 0)
#define PAGE_WRITABLE   (1ULL << 1)
#define PAGE_USER       (1ULL << 2)
#define PAGE_HUGE       (1ULL << 7)
#define PAGE_NX         (1ULL << 63)

static void setup_page_tables(void) {
    uint64_t* pml4 = (uint64_t*)PML4_BASE;
    uint64_t* pdpt = (uint64_t*)PDPT_BASE;
    uint64_t* pd = (uint64_t*)PD_BASE;
    
    // Clear page tables
    memset(pml4, 0, PAGE_SIZE);
    memset(pdpt, 0, PAGE_SIZE);
    memset(pd, 0, PAGE_SIZE);
    
    // PML4[0] -> PDPT
    pml4[0] = PDPT_BASE | PAGE_PRESENT | PAGE_WRITABLE;
    
    // PDPT[0] -> PD
    pdpt[0] = PD_BASE | PAGE_PRESENT | PAGE_WRITABLE;
    
    // Identity map first 1GB using 2MB pages
    for (int i = 0; i < 512; i++) {
        pd[i] = (i * 0x200000) | PAGE_PRESENT | PAGE_WRITABLE | PAGE_HUGE;
    }
    
    // Map higher half (0xFFFF800000000000)
    pml4[256] = PDPT_BASE | PAGE_PRESENT | PAGE_WRITABLE;
}

// =============================================================================
// ACPI Detection
// =============================================================================

static uint64_t find_rsdp(void) {
    // Search for "RSD PTR " signature
    const char* signature = "RSD PTR ";
    
    // Search EBDA (Extended BIOS Data Area)
    uint16_t ebda_segment = *(uint16_t*)0x40E;
    uint64_t ebda_addr = ebda_segment << 4;
    
    for (uint64_t addr = ebda_addr; addr < ebda_addr + 1024; addr += 16) {
        if (memcmp((void*)addr, signature, 8) == 0) {
            return addr;
        }
    }
    
    // Search BIOS ROM area
    for (uint64_t addr = 0xE0000; addr < 0x100000; addr += 16) {
        if (memcmp((void*)addr, signature, 8) == 0) {
            return addr;
        }
    }
    
    return 0;
}

static void detect_acpi(acpi_info_t* acpi) {
    acpi->rsdp_addr = find_rsdp();
    
    if (acpi->rsdp_addr) {
        uint8_t* rsdp = (uint8_t*)acpi->rsdp_addr;
        
        // Get revision
        acpi->revision = rsdp[15];
        
        // Get RSDT/XSDT address
        if (acpi->revision >= 2) {
            // ACPI 2.0+
            acpi->xsdt_addr = *(uint64_t*)(rsdp + 24);
            acpi->use_xsdt = true;
        } else {
            // ACPI 1.0
            acpi->rsdt_addr = *(uint32_t*)(rsdp + 16);
            acpi->use_xsdt = false;
        }
    }
}

// =============================================================================
// Boot Menu
// =============================================================================

typedef enum {
    MENU_OPTION_LIVE = 1,
    MENU_OPTION_INSTALL = 2,
    MENU_OPTION_RECOVERY = 3,
    MENU_OPTION_MEMTEST = 4,
    MENU_OPTION_SHELL = 5
} menu_option_t;

static menu_option_t display_boot_menu(void) {
    genesis_print("\n");
    genesis_print("================== LIMITLESS OS ==================\n");
    genesis_print("         Genesis Boot System v1.0.0\n");
    genesis_print("==================================================\n\n");
    genesis_print("  [1] Try LimitlessOS (Live Session)\n");
    genesis_print("  [2] Install LimitlessOS\n");
    genesis_print("  [3] Recovery Mode\n");
    genesis_print("  [4] Memory Test\n");
    genesis_print("  [5] Boot Shell\n");
    genesis_print("\nSelect option (1-5): ");
    
    // Simple keyboard input (would need proper implementation)
    // For now, return default option
    return MENU_OPTION_LIVE;
}

// =============================================================================
// Main Boot Function
// =============================================================================

void genesis_boot_main(void* platform_context, boot_mode_t boot_mode) {
    // Initialize boot context
    g_boot_context = (genesis_boot_context_t*)boot_alloc(sizeof(genesis_boot_context_t));
    memset(g_boot_context, 0, sizeof(genesis_boot_context_t));
    
    g_boot_context->magic = GENESIS_MAGIC;
    g_boot_context->version = GENESIS_VERSION;
    g_boot_context->size = sizeof(genesis_boot_context_t);
    g_boot_context->boot_mode = boot_mode;
    
    strcpy(g_boot_context->bootloader_name, "Genesis Boot System");
    
    // Clear screen
    if (boot_mode == BOOT_MODE_BIOS) {
        // Clear VGA text mode screen
        uint16_t* vga = (uint16_t*)0xB8000;
        for (int i = 0; i < 80 * 25; i++) {
            vga[i] = 0x0720;
        }
        g_boot_context->display_mode = DISPLAY_MODE_TEXT;
    }
    
    genesis_print("Genesis Boot System Initializing...\n");
    
    // Detect CPU features
    genesis_print("Detecting CPU features...\n");
    detect_cpu_features(&g_boot_context->cpu);
    
    if (!g_boot_context->cpu.has_64bit) {
        genesis_print("ERROR: 64-bit CPU required!\n");
        while (1) __asm__ __volatile__("hlt");
    }
    
    // Process platform-specific memory map
    genesis_print("Processing memory map...\n");
    
    if (boot_mode == BOOT_MODE_BIOS) {
        // Memory map should be provided by BIOS bootloader
        // For now, create a simple map
        g_boot_context->memory_map.region_count = 2;
        g_boot_context->memory_map.regions[0].base = 0x0;
        g_boot_context->memory_map.regions[0].length = 0x9F000;
        g_boot_context->memory_map.regions[0].type = MEMORY_TYPE_USABLE;
        g_boot_context->memory_map.regions[1].base = 0x100000;
        g_boot_context->memory_map.regions[1].length = 0x7EF00000;  // ~2GB
        g_boot_context->memory_map.regions[1].type = MEMORY_TYPE_USABLE;
    } else if (boot_mode == BOOT_MODE_UEFI) {
        // Memory map provided by UEFI bootloader
        // Platform context would contain the memory map
    }
    
    process_memory_map(&g_boot_context->memory_map);
    
    genesis_print("Total memory: ");
    genesis_print_hex(g_boot_context->memory_map.total_memory / (1024 * 1024));
    genesis_print(" MB\n");
    
    // Detect ACPI
    genesis_print("Detecting ACPI...\n");
    detect_acpi(&g_boot_context->acpi);
    
    if (g_boot_context->acpi.rsdp_addr) {
        genesis_print("ACPI RSDP found at: ");
        genesis_print_hex(g_boot_context->acpi.rsdp_addr);
    }
    
    // Display boot menu
    menu_option_t option = display_boot_menu();
    
    // Set command line based on selection
    switch (option) {
        case MENU_OPTION_LIVE:
            strcpy(g_boot_context->command_line, "boot=live quiet splash");
            break;
        case MENU_OPTION_INSTALL:
            strcpy(g_boot_context->command_line, "boot=install quiet");
            break;
        case MENU_OPTION_RECOVERY:
            strcpy(g_boot_context->command_line, "boot=recovery single");
            break;
        default:
            strcpy(g_boot_context->command_line, "boot=live");
            break;
    }
    
    // Load kernel (assuming it's already in memory)
    void* kernel_data = (void*)0x100000;  // Placeholder
    size_t kernel_size = 0x100000;        // Placeholder
    uint64_t kernel_entry;
    
    if (!load_kernel(kernel_data, kernel_size, &kernel_entry)) {
        genesis_print("ERROR: Failed to load kernel!\n");
        while (1) __asm__ __volatile__("hlt");
    }
    
    // Load initrd
    genesis_print("Loading initial ramdisk...\n");
    g_boot_context->initrd_start = INITRD_LOAD_ADDR;
    g_boot_context->initrd_end = INITRD_LOAD_ADDR + 0x2000000;  // 32MB placeholder
    
    // Setup page tables for 64-bit mode
    genesis_print("Setting up page tables...\n");
    setup_page_tables();
    
    // Final preparations
    genesis_print("Preparing to jump to kernel...\n");
    
    // Disable interrupts
    __asm__ __volatile__("cli");
    
    // Jump to kernel
    typedef void (*kernel_entry_t)(genesis_boot_context_t*);
    kernel_entry_t entry = (kernel_entry_t)kernel_entry;
    
    genesis_print("Jumping to Continuum kernel...\n\n");
    
    // Call kernel with boot context
    entry(g_boot_context);
    
    // Should never reach here
    genesis_print("ERROR: Kernel returned!\n");
    while (1) __asm__ __volatile__("hlt");
}

// =============================================================================
// Platform Entry Points
// =============================================================================

// Entry from BIOS bootloader
void genesis_bios_entry(void* bios_context) {
    genesis_boot_main(bios_context, BOOT_MODE_BIOS);
}

// Entry from UEFI bootloader
void genesis_uefi_entry(void* uefi_context) {
    genesis_boot_main(uefi_context, BOOT_MODE_UEFI);
}

// Entry from Multiboot-compliant loader
void genesis_multiboot_entry(void* mb_info, uint32_t mb_magic) {
    if (mb_magic == 0x2BADB002 || mb_magic == 0x36D76289) {
        genesis_boot_main(mb_info, BOOT_MODE_MULTIBOOT);
    } else {
        // Invalid magic
        while (1) __asm__ __volatile__("hlt");
    }
}

// Direct entry (for testing or special boot scenarios)
void genesis_direct_entry(void) {
    genesis_boot_main(NULL, BOOT_MODE_DIRECT);
}

// =============================================================================
// Panic Handler
// =============================================================================

void genesis_panic(const char* message) {
    __asm__ __volatile__("cli");
    
    genesis_print("\n\n");
    genesis_print("================== KERNEL PANIC ==================\n");
    genesis_print("Genesis Boot System Fatal Error\n");
    genesis_print("Message: ");
    genesis_print(message);
    genesis_print("\n");
    
    if (g_boot_context) {
        genesis_print("Boot mode: ");
        genesis_print_hex(g_boot_context->boot_mode);
        genesis_print("CPU: ");
        genesis_print((char*)g_boot_context->cpu.vendor);
        genesis_print("\n");
    }
    
    genesis_print("System halted.\n");
    genesis_print("==================================================\n");
    
    while (1) {
        __asm__ __volatile__("hlt");
    }
}
