/*
 * Flux Memory Manager Header
 * Unified virtual memory with CoW, compression, and cross-ABI page tables
 */

#ifndef FLUX_MEMORY_H
#define FLUX_MEMORY_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "continuum_core.h"

// =============================================================================
// Constants
// =============================================================================

#define MAX_DOMAINS                 256
#define MAX_REGIONS_PER_DOMAIN      1024
#define FLUX_PAGE_SIZE              4096
#define FLUX_HUGE_PAGE_SIZE         (2 * 1024 * 1024)

// Allocation flags
#define FLUX_ALLOC_KERNEL           (1 << 0)
#define FLUX_ALLOC_USER            (1 << 1)
#define FLUX_ALLOC_ZERO            (1 << 2)
#define FLUX_ALLOC_EXEC            (1 << 3)
#define FLUX_ALLOC_WRITE           (1 << 4)
#define FLUX_ALLOC_LARGE           (1 << 5)
#define FLUX_ALLOC_CONTIGUOUS      (1 << 6)
#define FLUX_ALLOC_DMA             (1 << 7)

// Mapping flags
#define FLUX_MAP_READ              (1 << 0)
#define FLUX_MAP_WRITE             (1 << 1)
#define FLUX_MAP_EXEC              (1 << 2)
#define FLUX_MAP_USER              (1 << 3)
#define FLUX_MAP_COW               (1 << 4)
#define FLUX_MAP_SHARED            (1 << 5)
#define FLUX_MAP_HUGE              (1 << 6)
#define FLUX_MAP_NOCACHE           (1 << 7)

// Region flags
#define REGION_FLAG_ALLOCATED       (1 << 0)
#define REGION_FLAG_MAPPED         (1 << 1)
#define REGION_FLAG_SHARED         (1 << 2)
#define REGION_FLAG_COW            (1 << 3)
#define REGION_FLAG_COMPRESSED     (1 << 4)
#define REGION_FLAG_ENCRYPTED      (1 << 5)
#define REGION_FLAG_EXECUTABLE     (1 << 6)
#define REGION_FLAG_READONLY       (1 << 7)

// =============================================================================
// Type Definitions
// =============================================================================

// Forward declarations
typedef struct memory_domain memory_domain_t;
typedef struct memory_region memory_region_t;
typedef struct slab_cache slab_cache_t;

// Memory region
struct memory_region {
    uint64_t base_addr;
    size_t size;
    uint32_t flags;
    uint32_t protection;
    uint64_t physical_addr;
    struct memory_region* next;
};

// Memory domain
struct memory_domain {
    uint32_t domain_id;
    quantum_id_t owner_qid;
    uint64_t page_table_base;
    memory_region_t regions[MAX_REGIONS_PER_DOMAIN];
    uint32_t region_count;
    size_t total_size;
    uint32_t flags;
    spinlock_t lock;
};

// Slab object
typedef struct slab {
    struct slab* next;
    struct slab* prev;
    slab_cache_t* cache;
    void* free_list;
    uint32_t free_count;
    uint32_t color_offset;
} slab_t;

// Slab cache
struct slab_cache {
    size_t object_size;
    uint32_t objects_per_slab;
    slab_t* full_slabs;
    slab_t* partial_slabs;
    slab_t* empty_slabs;
    uint64_t total_objects;
    uint64_t free_objects;
    spinlock_t lock;
};

// Memory statistics
typedef struct {
    uint64_t total_memory;
    uint64_t used_memory;
    uint64_t free_memory;
    uint64_t page_count;
    uint32_t domain_count;
    uint64_t compressed_pages;
    uint32_t compression_ratio;
    uint64_t cow_faults;
    uint64_t page_faults;
} flux_stats_t;

// Global memory state
typedef struct {
    bool initialized;
    uint64_t total_memory;
    uint64_t used_memory;
    uint64_t free_memory;
    uint64_t page_count;
    uint32_t domain_count;
} flux_memory_state_t;

// =============================================================================
// Function Prototypes
// =============================================================================

// Initialization
void flux_init(void* memory_map);

// Domain management
memory_domain_t* flux_create_domain(quantum_id_t owner);
void flux_destroy_domain(memory_domain_t* domain);
memory_domain_t* flux_get_current_domain(void);

// Memory allocation
void* flux_allocate(memory_domain_t* domain, size_t size, uint32_t flags);
void flux_free(void* ptr);
void* flux_reallocate(void* ptr, size_t new_size);

// Memory mapping
void* flux_map_region(memory_domain_t* domain, uint64_t vaddr, 
                     uint64_t paddr, size_t size, uint32_t flags);
void flux_unmap_region(memory_domain_t* domain, uint64_t vaddr, size_t size);
void flux_protect_region(memory_domain_t* domain, uint64_t vaddr, 
                        size_t size, uint32_t protection);

// Copy-on-Write
void flux_mark_cow(memory_domain_t* domain, uint64_t vaddr, size_t size);
void flux_handle_cow_fault(memory_domain_t* domain, uint64_t fault_addr);

// Page compression
void flux_compress_page(uint64_t paddr);
void flux_decompress_page(uint64_t paddr);
bool flux_is_compressed(uint64_t paddr);

// Shared memory
void* flux_create_shared(size_t size, const char* name);
void* flux_attach_shared(const char* name);
void flux_detach_shared(void* addr);

// Statistics
void flux_get_stats(flux_stats_t* stats);
size_t flux_get_domain_usage(memory_domain_t* domain);

// Helper functions
uint64_t flux_translate_address(memory_domain_t* domain, uint64_t vaddr);
void flux_flush_tlb(uint64_t addr, size_t size);
uint64_t flux_get_pte(memory_domain_t* domain, uint64_t vaddr);
void flux_set_pte(memory_domain_t* domain, uint64_t vaddr, uint64_t pte);
void flux_unref_page(uint64_t paddr);

// Memory operations
void* memset(void* dest, int val, size_t len);
void* memcpy(void* dest, const void* src, size_t len);
void* memmove(void* dest, const void* src, size_t len);
int memcmp(const void* s1, const void* s2, size_t len);

#endif /* FLUX_MEMORY_H */
