/*
 * Flux Memory Manager for Continuum Kernel
 * Unified virtual memory with CoW, compression, and cross-ABI page tables
 */

#include "flux_memory.h"
#include "continuum_core.h"

// =============================================================================
// Constants and Macros
// =============================================================================

#define FLUX_MAGIC              0x464C5558  // "FLUX"
#define PAGE_SIZE               4096
#define HUGE_PAGE_SIZE          (2 * 1024 * 1024)  // 2MB
#define SLAB_SIZES_COUNT        12
#define BUDDY_MAX_ORDER         11  // Up to 8MB blocks
#define COMPRESSION_THRESHOLD   (PAGE_SIZE / 2)

// Page flags
#define PAGE_PRESENT            (1ULL << 0)
#define PAGE_WRITABLE          (1ULL << 1)
#define PAGE_USER              (1ULL << 2)
#define PAGE_WRITE_THROUGH     (1ULL << 3)
#define PAGE_CACHE_DISABLE     (1ULL << 4)
#define PAGE_ACCESSED          (1ULL << 5)
#define PAGE_DIRTY             (1ULL << 6)
#define PAGE_HUGE              (1ULL << 7)
#define PAGE_GLOBAL            (1ULL << 8)
#define PAGE_COW               (1ULL << 9)
#define PAGE_COMPRESSED        (1ULL << 10)
#define PAGE_ENCRYPTED         (1ULL << 11)
#define PAGE_NX                (1ULL << 63)

// =============================================================================
// Global Memory State
// =============================================================================

static flux_memory_state_t g_memory_state = {
    .initialized = false,
    .total_memory = 0,
    .used_memory = 0,
    .free_memory = 0,
    .page_count = 0,
    .domain_count = 0
};

// Physical memory bitmap
static uint64_t* g_phys_bitmap = NULL;
static uint64_t g_phys_pages = 0;

// Buddy allocator for physical pages
static struct buddy_block {
    struct buddy_block* next;
    struct buddy_block* prev;
    uint32_t order;
    uint32_t free;
} *g_buddy_lists[BUDDY_MAX_ORDER];

// Slab allocator for small objects
static slab_cache_t g_slab_caches[SLAB_SIZES_COUNT];
static const size_t g_slab_sizes[] = {
    32, 64, 128, 256, 512, 1024, 2048, 4096, 8192, 16384, 32768, 65536
};

// Memory domains
static memory_domain_t* g_domains[MAX_DOMAINS];
static spinlock_t g_memory_lock = SPINLOCK_INIT;

// Compression engine
static struct {
    void* workspace;
    size_t workspace_size;
    uint64_t compressed_pages;
    uint64_t compression_ratio;
} g_compression;

// =============================================================================
// Physical Memory Management
// =============================================================================

static uint64_t phys_alloc_page(void) {
    spinlock_acquire(&g_memory_lock);
    
    // Find first free page in bitmap
    for (uint64_t i = 0; i < g_phys_pages / 64; i++) {
        if (g_phys_bitmap[i] != 0xFFFFFFFFFFFFFFFFULL) {
            // Found a free page
            uint64_t bit = __builtin_ctzll(~g_phys_bitmap[i]);
            g_phys_bitmap[i] |= (1ULL << bit);
            
            uint64_t page_addr = (i * 64 + bit) * PAGE_SIZE;
            g_memory_state.used_memory += PAGE_SIZE;
            g_memory_state.free_memory -= PAGE_SIZE;
            
            spinlock_release(&g_memory_lock);
            return page_addr;
        }
    }
    
    spinlock_release(&g_memory_lock);
    return 0;  // Out of memory
}

static void phys_free_page(uint64_t addr) {
    uint64_t page_num = addr / PAGE_SIZE;
    uint64_t bitmap_idx = page_num / 64;
    uint64_t bit_idx = page_num % 64;
    
    spinlock_acquire(&g_memory_lock);
    
    g_phys_bitmap[bitmap_idx] &= ~(1ULL << bit_idx);
    g_memory_state.used_memory -= PAGE_SIZE;
    g_memory_state.free_memory += PAGE_SIZE;
    
    spinlock_release(&g_memory_lock);
}

// =============================================================================
// Buddy Allocator
// =============================================================================

static void* buddy_alloc(size_t size) {
    // Calculate order (power of 2)
    int order = 0;
    size_t block_size = PAGE_SIZE;
    
    while (block_size < size && order < BUDDY_MAX_ORDER - 1) {
        block_size <<= 1;
        order++;
    }
    
    spinlock_acquire(&g_memory_lock);
    
    // Find a free block of the right size
    for (int current_order = order; current_order < BUDDY_MAX_ORDER; current_order++) {
        if (g_buddy_lists[current_order]) {
            struct buddy_block* block = g_buddy_lists[current_order];
            
            // Remove from free list
            g_buddy_lists[current_order] = block->next;
            if (block->next) {
                block->next->prev = NULL;
            }
            
            // Split if necessary
            while (current_order > order) {
                current_order--;
                struct buddy_block* buddy = (struct buddy_block*)
                    ((uint64_t)block + (PAGE_SIZE << current_order));
                buddy->order = current_order;
                buddy->free = 1;
                buddy->next = g_buddy_lists[current_order];
                buddy->prev = NULL;
                if (buddy->next) {
                    buddy->next->prev = buddy;
                }
                g_buddy_lists[current_order] = buddy;
            }
            
            block->free = 0;
            spinlock_release(&g_memory_lock);
            return block;
        }
    }
    
    spinlock_release(&g_memory_lock);
    
    // Allocate new pages from physical memory
    uint64_t phys_addr = phys_alloc_page();
    if (phys_addr) {
        return (void*)phys_addr;  // Simplified - would need virtual mapping
    }
    
    return NULL;
}

static void buddy_free(void* ptr, size_t size) {
    // Calculate order
    int order = 0;
    size_t block_size = PAGE_SIZE;
    
    while (block_size < size && order < BUDDY_MAX_ORDER - 1) {
        block_size <<= 1;
        order++;
    }
    
    struct buddy_block* block = (struct buddy_block*)ptr;
    block->order = order;
    block->free = 1;
    
    spinlock_acquire(&g_memory_lock);
    
    // Try to coalesce with buddy
    while (order < BUDDY_MAX_ORDER - 1) {
        uint64_t buddy_addr = (uint64_t)block ^ (PAGE_SIZE << order);
        struct buddy_block* buddy = (struct buddy_block*)buddy_addr;
        
        if (!buddy->free || buddy->order != order) {
            break;
        }
        
        // Remove buddy from free list
        if (buddy->prev) {
            buddy->prev->next = buddy->next;
        } else {
            g_buddy_lists[order] = buddy->next;
        }
        if (buddy->next) {
            buddy->next->prev = buddy->prev;
        }
        
        // Coalesce
        if (buddy_addr < (uint64_t)block) {
            block = buddy;
        }
        order++;
        block->order = order;
    }
    
    // Add to free list
    block->next = g_buddy_lists[order];
    block->prev = NULL;
    if (block->next) {
        block->next->prev = block;
    }
    g_buddy_lists[order] = block;
    
    spinlock_release(&g_memory_lock);
}

// =============================================================================
// Slab Allocator
// =============================================================================

static void slab_init_cache(slab_cache_t* cache, size_t object_size) {
    cache->object_size = object_size;
    cache->objects_per_slab = (PAGE_SIZE - sizeof(slab_t)) / object_size;
    cache->full_slabs = NULL;
    cache->partial_slabs = NULL;
    cache->empty_slabs = NULL;
    cache->total_objects = 0;
    cache->free_objects = 0;
    spinlock_init(&cache->lock);
}

static slab_t* slab_create(slab_cache_t* cache) {
    // Allocate a page for the slab
    void* page = buddy_alloc(PAGE_SIZE);
    if (!page) {
        return NULL;
    }
    
    slab_t* slab = (slab_t*)page;
    slab->cache = cache;
    slab->free_count = cache->objects_per_slab;
    slab->next = NULL;
    slab->prev = NULL;
    
    // Initialize free list
    uint8_t* obj_start = (uint8_t*)(slab + 1);
    for (uint32_t i = 0; i < cache->objects_per_slab; i++) {
        *(void**)(obj_start + i * cache->object_size) = 
            (i < cache->objects_per_slab - 1) ? 
            (obj_start + (i + 1) * cache->object_size) : NULL;
    }
    slab->free_list = obj_start;
    
    cache->total_objects += cache->objects_per_slab;
    cache->free_objects += cache->objects_per_slab;
    
    return slab;
}

static void* slab_alloc(slab_cache_t* cache) {
    spinlock_acquire(&cache->lock);
    
    // Try partial slabs first
    slab_t* slab = cache->partial_slabs;
    if (!slab) {
        // Try empty slabs
        slab = cache->empty_slabs;
        if (!slab) {
            // Create new slab
            slab = slab_create(cache);
            if (!slab) {
                spinlock_release(&cache->lock);
                return NULL;
            }
        } else {
            // Move from empty to partial
            cache->empty_slabs = slab->next;
            if (slab->next) {
                slab->next->prev = NULL;
            }
        }
        slab->next = cache->partial_slabs;
        slab->prev = NULL;
        if (cache->partial_slabs) {
            cache->partial_slabs->prev = slab;
        }
        cache->partial_slabs = slab;
    }
    
    // Allocate object from slab
    void* obj = slab->free_list;
    slab->free_list = *(void**)obj;
    slab->free_count--;
    cache->free_objects--;
    
    // Move to full list if necessary
    if (slab->free_count == 0) {
        // Remove from partial list
        if (slab->prev) {
            slab->prev->next = slab->next;
        } else {
            cache->partial_slabs = slab->next;
        }
        if (slab->next) {
            slab->next->prev = slab->prev;
        }
        
        // Add to full list
        slab->next = cache->full_slabs;
        slab->prev = NULL;
        if (cache->full_slabs) {
            cache->full_slabs->prev = slab;
        }
        cache->full_slabs = slab;
    }
    
    spinlock_release(&cache->lock);
    return obj;
}

static void slab_free(void* obj, slab_cache_t* cache) {
    // Find the slab containing this object
    slab_t* slab = (slab_t*)((uint64_t)obj & ~(PAGE_SIZE - 1));
    
    spinlock_acquire(&cache->lock);
    
    // Add to free list
    *(void**)obj = slab->free_list;
    slab->free_list = obj;
    slab->free_count++;
    cache->free_objects++;
    
    // Move between lists if necessary
    if (slab->free_count == 1) {
        // Was full, now partial
        if (slab->prev) {
            slab->prev->next = slab->next;
        } else {
            cache->full_slabs = slab->next;
        }
        if (slab->next) {
            slab->next->prev = slab->prev;
        }
        
        slab->next = cache->partial_slabs;
        slab->prev = NULL;
        if (cache->partial_slabs) {
            cache->partial_slabs->prev = slab;
        }
        cache->partial_slabs = slab;
    } else if (slab->free_count == cache->objects_per_slab) {
        // Now empty, move to empty list
        if (slab->prev) {
            slab->prev->next = slab->next;
        } else {
            cache->partial_slabs = slab->next;
        }
        if (slab->next) {
            slab->next->prev = slab->prev;
        }
        
        slab->next = cache->empty_slabs;
        slab->prev = NULL;
        if (cache->empty_slabs) {
            cache->empty_slabs->prev = slab;
        }
        cache->empty_slabs = slab;
    }
    
    spinlock_release(&cache->lock);
}

// =============================================================================
// Memory Domain Management
// =============================================================================

memory_domain_t* flux_create_domain(quantum_id_t owner) {
    spinlock_acquire(&g_memory_lock);
    
    // Find free domain slot
    int domain_id = -1;
    for (int i = 0; i < MAX_DOMAINS; i++) {
        if (!g_domains[i]) {
            domain_id = i;
            break;
        }
    }
    
    if (domain_id == -1) {
        spinlock_release(&g_memory_lock);
        return NULL;
    }
    
    memory_domain_t* domain = slab_alloc(&g_slab_caches[8]);  // 256 bytes
    if (!domain) {
        spinlock_release(&g_memory_lock);
        return NULL;
    }
    
    domain->domain_id = domain_id;
    domain->owner_qid = owner;
    domain->page_table_base = phys_alloc_page();
    domain->region_count = 0;
    domain->total_size = 0;
    domain->flags = 0;
    spinlock_init(&domain->lock);
    
    // Clear page table
    memset((void*)domain->page_table_base, 0, PAGE_SIZE);
    
    g_domains[domain_id] = domain;
    g_memory_state.domain_count++;
    
    spinlock_release(&g_memory_lock);
    return domain;
}

void flux_destroy_domain(memory_domain_t* domain) {
    if (!domain) {
        return;
    }
    
    spinlock_acquire(&g_memory_lock);
    
    // Free all regions
    for (uint32_t i = 0; i < domain->region_count; i++) {
        memory_region_t* region = &domain->regions[i];
        if (region->flags & REGION_FLAG_ALLOCATED) {
            // Free physical pages
            for (size_t offset = 0; offset < region->size; offset += PAGE_SIZE) {
                uint64_t vaddr = region->base_addr + offset;
                uint64_t paddr = flux_translate_address(domain, vaddr);
                if (paddr) {
                    phys_free_page(paddr);
                }
            }
        }
    }
    
    // Free page table
    if (domain->page_table_base) {
        phys_free_page(domain->page_table_base);
    }
    
    g_domains[domain->domain_id] = NULL;
    g_memory_state.domain_count--;
    
    slab_free(domain, &g_slab_caches[8]);
    
    spinlock_release(&g_memory_lock);
}

// =============================================================================
// Virtual Memory Operations
// =============================================================================

void* flux_allocate(memory_domain_t* domain, size_t size, uint32_t flags) {
    if (size == 0) {
        return NULL;
    }
    
    // Use kernel domain if not specified
    if (!domain) {
        domain = g_domains[0];  // Kernel domain
    }
    
    // Use slab allocator for small sizes
    if (!(flags & FLUX_ALLOC_LARGE) && size <= 65536) {
        for (int i = 0; i < SLAB_SIZES_COUNT; i++) {
            if (size <= g_slab_sizes[i]) {
                void* obj = slab_alloc(&g_slab_caches[i]);
                if (obj && (flags & FLUX_ALLOC_ZERO)) {
                    memset(obj, 0, g_slab_sizes[i]);
                }
                return obj;
            }
        }
    }
    
    // Use buddy allocator for larger sizes
    size = (size + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);  // Round up to page
    void* ptr = buddy_alloc(size);
    
    if (ptr && (flags & FLUX_ALLOC_ZERO)) {
        memset(ptr, 0, size);
    }
    
    // Update domain tracking
    if (ptr && domain) {
        spinlock_acquire(&domain->lock);
        
        if (domain->region_count < MAX_REGIONS_PER_DOMAIN) {
            memory_region_t* region = &domain->regions[domain->region_count++];
            region->base_addr = (uint64_t)ptr;
            region->size = size;
            region->flags = REGION_FLAG_ALLOCATED;
            if (flags & FLUX_ALLOC_EXEC) {
                region->flags |= REGION_FLAG_EXECUTABLE;
            }
            if (!(flags & FLUX_ALLOC_WRITE)) {
                region->flags |= REGION_FLAG_READONLY;
            }
            domain->total_size += size;
        }
        
        spinlock_release(&domain->lock);
    }
    
    return ptr;
}

void flux_free(void* ptr) {
    if (!ptr) {
        return;
    }
    
    // Check if it's a slab allocation
    uint64_t addr = (uint64_t)ptr;
    slab_t* slab = (slab_t*)(addr & ~(PAGE_SIZE - 1));
    
    // Simple check for slab magic (would be more robust in production)
    if (slab->cache && slab->cache->object_size > 0) {
        slab_free(ptr, slab->cache);
    } else {
        // Assume buddy allocation
        // Size would need to be tracked properly
        buddy_free(ptr, PAGE_SIZE);  // Simplified
    }
}

void* flux_map_region(memory_domain_t* domain, uint64_t vaddr, 
                     uint64_t paddr, size_t size, uint32_t flags) {
    if (!domain || !size) {
        return NULL;
    }
    
    spinlock_acquire(&domain->lock);
    
    // Get page table base
    uint64_t* pml4 = (uint64_t*)domain->page_table_base;
    
    // Map each page
    for (size_t offset = 0; offset < size; offset += PAGE_SIZE) {
        uint64_t va = vaddr + offset;
        uint64_t pa = paddr + offset;
        
        // Calculate indices
        uint64_t pml4_idx = (va >> 39) & 0x1FF;
        uint64_t pdpt_idx = (va >> 30) & 0x1FF;
        uint64_t pd_idx = (va >> 21) & 0x1FF;
        uint64_t pt_idx = (va >> 12) & 0x1FF;
        
        // Get or create PDPT
        if (!(pml4[pml4_idx] & PAGE_PRESENT)) {
            uint64_t pdpt_page = phys_alloc_page();
            if (!pdpt_page) {
                spinlock_release(&domain->lock);
                return NULL;
            }
            memset((void*)pdpt_page, 0, PAGE_SIZE);
            pml4[pml4_idx] = pdpt_page | PAGE_PRESENT | PAGE_WRITABLE | PAGE_USER;
        }
        uint64_t* pdpt = (uint64_t*)(pml4[pml4_idx] & ~0xFFF);
        
        // Get or create PD
        if (!(pdpt[pdpt_idx] & PAGE_PRESENT)) {
            uint64_t pd_page = phys_alloc_page();
            if (!pd_page) {
                spinlock_release(&domain->lock);
                return NULL;
            }
            memset((void*)pd_page, 0, PAGE_SIZE);
            pdpt[pdpt_idx] = pd_page | PAGE_PRESENT | PAGE_WRITABLE | PAGE_USER;
        }
        uint64_t* pd = (uint64_t*)(pdpt[pdpt_idx] & ~0xFFF);
        
        // Use huge pages if aligned and requested
        if ((flags & FLUX_MAP_HUGE) && 
            (va & (HUGE_PAGE_SIZE - 1)) == 0 &&
            (pa & (HUGE_PAGE_SIZE - 1)) == 0 &&
            size - offset >= HUGE_PAGE_SIZE) {
            
            // Map 2MB huge page
            uint64_t page_flags = PAGE_PRESENT | PAGE_HUGE;
            if (flags & FLUX_MAP_WRITE) page_flags |= PAGE_WRITABLE;
            if (flags & FLUX_MAP_USER) page_flags |= PAGE_USER;
            if (!(flags & FLUX_MAP_EXEC)) page_flags |= PAGE_NX;
            
            pd[pd_idx] = pa | page_flags;
            offset += HUGE_PAGE_SIZE - PAGE_SIZE;  // Skip ahead
            continue;
        }
        
        // Get or create PT
        if (!(pd[pd_idx] & PAGE_PRESENT)) {
            uint64_t pt_page = phys_alloc_page();
            if (!pt_page) {
                spinlock_release(&domain->lock);
                return NULL;
            }
            memset((void*)pt_page, 0, PAGE_SIZE);
            pd[pd_idx] = pt_page | PAGE_PRESENT | PAGE_WRITABLE | PAGE_USER;
        }
        uint64_t* pt = (uint64_t*)(pd[pd_idx] & ~0xFFF);
        
        // Map page
        uint64_t page_flags = PAGE_PRESENT;
        if (flags & FLUX_MAP_WRITE) page_flags |= PAGE_WRITABLE;
        if (flags & FLUX_MAP_USER) page_flags |= PAGE_USER;
        if (!(flags & FLUX_MAP_EXEC)) page_flags |= PAGE_NX;
        if (flags & FLUX_MAP_COW) page_flags |= PAGE_COW;
        
        pt[pt_idx] = pa | page_flags;
    }
    
    // Flush TLB
    flux_flush_tlb(vaddr, size);
    
    spinlock_release(&domain->lock);
    return (void*)vaddr;
}

// =============================================================================
// Copy-on-Write Support
// =============================================================================

void flux_handle_cow_fault(memory_domain_t* domain, uint64_t fault_addr) {
    spinlock_acquire(&domain->lock);
    
    // Get page table entry
    uint64_t pte = flux_get_pte(domain, fault_addr);
    
    if (pte & PAGE_COW) {
        // Allocate new page
        uint64_t new_page = phys_alloc_page();
        if (new_page) {
            // Copy contents
            uint64_t old_page = pte & ~0xFFF;
            memcpy((void*)new_page, (void*)old_page, PAGE_SIZE);
            
            // Update PTE
            uint64_t new_pte = new_page | (pte & 0xFFF);
            new_pte &= ~PAGE_COW;
            new_pte |= PAGE_WRITABLE;
            flux_set_pte(domain, fault_addr, new_pte);
            
            // Decrease reference count on old page
            flux_unref_page(old_page);
            
            // Flush TLB for this page
            flux_flush_tlb(fault_addr, PAGE_SIZE);
        }
    }
    
    spinlock_release(&domain->lock);
}

// =============================================================================
// Page Compression
// =============================================================================

static size_t compress_page(void* src, void* dst) {
    // Simple RLE compression for demonstration
    uint8_t* s = (uint8_t*)src;
    uint8_t* d = (uint8_t*)dst;
    size_t src_pos = 0;
    size_t dst_pos = 0;
    
    while (src_pos < PAGE_SIZE && dst_pos < PAGE_SIZE - 2) {
        uint8_t byte = s[src_pos];
        size_t run_length = 1;
        
        while (src_pos + run_length < PAGE_SIZE && 
               s[src_pos + run_length] == byte &&
               run_length < 255) {
            run_length++;
        }
        
        if (run_length > 2 || byte == 0xFF) {
            d[dst_pos++] = 0xFF;  // Escape
            d[dst_pos++] = run_length;
            d[dst_pos++] = byte;
            src_pos += run_length;
        } else {
            d[dst_pos++] = byte;
            src_pos++;
        }
    }
    
    return dst_pos;
}

static void decompress_page(void* src, void* dst, size_t compressed_size) {
    uint8_t* s = (uint8_t*)src;
    uint8_t* d = (uint8_t*)dst;
    size_t src_pos = 0;
    size_t dst_pos = 0;
    
    while (src_pos < compressed_size && dst_pos < PAGE_SIZE) {
        if (s[src_pos] == 0xFF && src_pos + 2 < compressed_size) {
            // Compressed run
            size_t run_length = s[src_pos + 1];
            uint8_t byte = s[src_pos + 2];
            for (size_t i = 0; i < run_length && dst_pos < PAGE_SIZE; i++) {
                d[dst_pos++] = byte;
            }
            src_pos += 3;
        } else {
            d[dst_pos++] = s[src_pos++];
        }
    }
    
    // Fill rest with zeros if needed
    while (dst_pos < PAGE_SIZE) {
        d[dst_pos++] = 0;
    }
}

void flux_compress_page(uint64_t paddr) {
    void* page = (void*)paddr;
    void* compressed = flux_allocate(NULL, PAGE_SIZE, FLUX_ALLOC_KERNEL);
    
    if (compressed) {
        size_t compressed_size = compress_page(page, compressed);
        
        if (compressed_size < COMPRESSION_THRESHOLD) {
            // Worth compressing
            g_compression.compressed_pages++;
            g_compression.compression_ratio = 
                (g_compression.compression_ratio * (g_compression.compressed_pages - 1) +
                 (PAGE_SIZE * 100 / compressed_size)) / g_compression.compressed_pages;
            
            // Store compressed data (simplified)
            // In reality, would need to update page tables and track compressed pages
        }
        
        flux_free(compressed);
    }
}

// =============================================================================
// Initialization
// =============================================================================

void flux_init(void* memory_map) {
    // Parse memory map from boot context
    // Simplified - would parse actual E820 or UEFI memory map
    g_memory_state.total_memory = 2ULL * 1024 * 1024 * 1024;  // 2GB for testing
    g_memory_state.free_memory = g_memory_state.total_memory;
    g_memory_state.used_memory = 0;
    
    // Initialize physical memory bitmap
    g_phys_pages = g_memory_state.total_memory / PAGE_SIZE;
    size_t bitmap_size = (g_phys_pages + 63) / 64 * sizeof(uint64_t);
    g_phys_bitmap = (uint64_t*)0x200000;  // Place at 2MB
    memset(g_phys_bitmap, 0, bitmap_size);
    
    // Mark kernel and bitmap as used
    for (uint64_t addr = 0; addr < 0x400000; addr += PAGE_SIZE) {
        uint64_t page_num = addr / PAGE_SIZE;
        g_phys_bitmap[page_num / 64] |= (1ULL << (page_num % 64));
    }
    
    // Initialize buddy allocator
    for (int i = 0; i < BUDDY_MAX_ORDER; i++) {
        g_buddy_lists[i] = NULL;
    }
    
    // Initialize slab caches
    for (int i = 0; i < SLAB_SIZES_COUNT; i++) {
        slab_init_cache(&g_slab_caches[i], g_slab_sizes[i]);
    }
    
    // Create kernel memory domain
    g_domains[0] = flux_create_domain(0);  // QID 0 for kernel
    
    // Initialize compression
    g_compression.workspace = NULL;
    g_compression.workspace_size = 0;
    g_compression.compressed_pages = 0;
    g_compression.compression_ratio = 100;
    
    g_memory_state.initialized = true;
}

// =============================================================================
// Helper Functions
// =============================================================================

uint64_t flux_translate_address(memory_domain_t* domain, uint64_t vaddr) {
    if (!domain) {
        return 0;
    }
    
    uint64_t* pml4 = (uint64_t*)domain->page_table_base;
    
    uint64_t pml4_idx = (vaddr >> 39) & 0x1FF;
    uint64_t pdpt_idx = (vaddr >> 30) & 0x1FF;
    uint64_t pd_idx = (vaddr >> 21) & 0x1FF;
    uint64_t pt_idx = (vaddr >> 12) & 0x1FF;
    uint64_t offset = vaddr & 0xFFF;
    
    if (!(pml4[pml4_idx] & PAGE_PRESENT)) return 0;
    uint64_t* pdpt = (uint64_t*)(pml4[pml4_idx] & ~0xFFF);
    
    if (!(pdpt[pdpt_idx] & PAGE_PRESENT)) return 0;
    uint64_t* pd = (uint64_t*)(pdpt[pdpt_idx] & ~0xFFF);
    
    if (pd[pd_idx] & PAGE_HUGE) {
        // 2MB huge page
        return (pd[pd_idx] & ~0x1FFFFF) | (vaddr & 0x1FFFFF);
    }
    
    if (!(pd[pd_idx] & PAGE_PRESENT)) return 0;
    uint64_t* pt = (uint64_t*)(pd[pd_idx] & ~0xFFF);
    
    if (!(pt[pt_idx] & PAGE_PRESENT)) return 0;
    
    return (pt[pt_idx] & ~0xFFF) | offset;
}

void flux_flush_tlb(uint64_t addr, size_t size) {
    // Flush TLB entries for address range
    for (uint64_t va = addr; va < addr + size; va += PAGE_SIZE) {
        __asm__ __volatile__("invlpg (%0)" : : "r"(va) : "memory");
    }
}

uint64_t flux_get_pte(memory_domain_t* domain, uint64_t vaddr) {
    // Navigate page tables to get PTE
    // Simplified - similar to translate_address
    return 0;
}

void flux_set_pte(memory_domain_t* domain, uint64_t vaddr, uint64_t pte) {
    // Navigate page tables and set PTE
    // Simplified - similar to map_region
}

void flux_unref_page(uint64_t paddr) {
    // Decrease reference count on page
    // If count reaches zero, free the page
    // Simplified for now
}

void flux_get_stats(flux_stats_t* stats) {
    if (!stats) {
        return;
    }
    
    stats->total_memory = g_memory_state.total_memory;
    stats->used_memory = g_memory_state.used_memory;
    stats->free_memory = g_memory_state.free_memory;
    stats->page_count = g_memory_state.page_count;
    stats->domain_count = g_memory_state.domain_count;
    stats->compressed_pages = g_compression.compressed_pages;
    stats->compression_ratio = g_compression.compression_ratio;
}

// Simple memory operations
void* memset(void* dest, int val, size_t len) {
    uint8_t* d = dest;
    while (len--) {
        *d++ = (uint8_t)val;
    }
    return dest;
}

void* memcpy(void* dest, const void* src, size_t len) {
    uint8_t* d = dest;
    const uint8_t* s = src;
    while (len--) {
        *d++ = *s++;
    }
    return dest;
}
