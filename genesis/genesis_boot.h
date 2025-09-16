/*
 * Genesis Boot Core Header
 * Public interface for bootloader integration
 */

#ifndef GENESIS_BOOT_H
#define GENESIS_BOOT_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

// Boot modes
#define GENESIS_BOOT_MODE_BIOS      1
#define GENESIS_BOOT_MODE_UEFI      2
#define GENESIS_BOOT_MODE_MULTIBOOT 3
#define GENESIS_BOOT_MODE_DIRECT    4

// Memory types
#define GENESIS_MEMORY_USABLE       1
#define GENESIS_MEMORY_RESERVED     2
#define GENESIS_MEMORY_ACPI_RECLAIM 3
#define GENESIS_MEMORY_ACPI_NVS     4
#define GENESIS_MEMORY_BAD          5

// Forward declarations
typedef struct genesis_boot_context genesis_boot_context_t;

// Entry points
void genesis_bios_entry(void* bios_context);
void genesis_uefi_entry(void* uefi_context);
void genesis_multiboot_entry(void* mb_info, uint32_t mb_magic);
void genesis_direct_entry(void);

// Panic handler
void genesis_panic(const char* message);

// Boot context accessors
const genesis_boot_context_t* genesis_get_boot_context(void);
const char* genesis_get_command_line(void);
uint64_t genesis_get_total_memory(void);
uint64_t genesis_get_usable_memory(void);

#endif /* GENESIS_BOOT_H */
