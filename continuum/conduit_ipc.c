/*
 * Conduit IPC System for Continuum Kernel
 * High-performance inter-quantum communication
 */

#include "conduit_ipc.h"
#include "continuum_core.h"
#include "flux_memory.h"
#include "temporal_scheduler.h"

// =============================================================================
// Global IPC State
// =============================================================================

static conduit_registry_t g_conduit_registry = {
    .initialized = false,
    .conduit_count = 0,
    .message_count = 0,
    .total_bytes = 0
};

static conduit_t* g_conduits[MAX_CONDUITS];
static spinlock_t g_conduit_lock = SPINLOCK_INIT;

// Name hash table for fast lookup
#define NAME_HASH_SIZE 256
static struct {
    char name[CONDUIT_NAME_MAX];
    conduit_t* conduit;
} g_name_table[NAME_HASH_SIZE];

// =============================================================================
// Hash Function
// =============================================================================

static uint32_t hash_name(const char* name) {
    uint32_t hash = 5381;
    int c;
    
    while ((c = *name++)) {
        hash = ((hash << 5) + hash) + c;
    }
    
    return hash % NAME_HASH_SIZE;
}

// =============================================================================
// Ring Buffer Operations
// =============================================================================

static void ringbuf_init(ring_buffer_t* rb, size_t size) {
    rb->buffer = flux_allocate(NULL, size, FLUX_ALLOC_KERNEL | FLUX_ALLOC_ZERO);
    rb->size = size;
    rb->head = 0;
    rb->tail = 0;
    rb->used = 0;
    spinlock_init(&rb->lock);
}

static void ringbuf_destroy(ring_buffer_t* rb) {
    if (rb->buffer) {
        flux_free(rb->buffer);
        rb->buffer = NULL;
    }
}

static size_t ringbuf_write(ring_buffer_t* rb, const void* data, size_t len) {
    spinlock_acquire(&rb->lock);
    
    size_t available = rb->size - rb->used;
    if (len > available) {
        len = available;
    }
    
    if (len == 0) {
        spinlock_release(&rb->lock);
        return 0;
    }
    
    // Write in up to two chunks (wrap around)
    size_t first_chunk = rb->size - rb->tail;
    if (first_chunk > len) {
        first_chunk = len;
    }
    
    memcpy((uint8_t*)rb->buffer + rb->tail, data, first_chunk);
    
    if (len > first_chunk) {
        memcpy(rb->buffer, (uint8_t*)data + first_chunk, len - first_chunk);
    }
    
    rb->tail = (rb->tail + len) % rb->size;
    rb->used += len;
    
    spinlock_release(&rb->lock);
    return len;
}

static size_t ringbuf_read(ring_buffer_t* rb, void* data, size_t len) {
    spinlock_acquire(&rb->lock);
    
    if (len > rb->used) {
        len = rb->used;
    }
    
    if (len == 0) {
        spinlock_release(&rb->lock);
        return 0;
    }
    
    // Read in up to two chunks
    size_t first_chunk = rb->size - rb->head;
    if (first_chunk > len) {
        first_chunk = len;
    }
    
    memcpy(data, (uint8_t*)rb->buffer + rb->head, first_chunk);
    
    if (len > first_chunk) {
        memcpy((uint8_t*)data + first_chunk, rb->buffer, len - first_chunk);
    }
    
    rb->head = (rb->head + len) % rb->size;
    rb->used -= len;
    
    spinlock_release(&rb->lock);
    return len;
}

static size_t ringbuf_peek(ring_buffer_t* rb, void* data, size_t len) {
    spinlock_acquire(&rb->lock);
    
    if (len > rb->used) {
        len = rb->used;
    }
    
    if (len == 0) {
        spinlock_release(&rb->lock);
        return 0;
    }
    
    // Peek without modifying head
    size_t first_chunk = rb->size - rb->head;
    if (first_chunk > len) {
        first_chunk = len;
    }
    
    memcpy(data, (uint8_t*)rb->buffer + rb->head, first_chunk);
    
    if (len > first_chunk) {
        memcpy((uint8_t*)data + first_chunk, rb->buffer, len - first_chunk);
    }
    
    spinlock_release(&rb->lock);
    return len;
}

// =============================================================================
// Wait Queue Operations
// =============================================================================

static void waitqueue_init(wait_queue_t* wq) {
    wq->head = NULL;
    wq->tail = NULL;
    wq->count = 0;
    spinlock_init(&wq->lock);
}

static void waitqueue_add(wait_queue_t* wq, quantum_context_t* quantum) {
    spinlock_acquire(&wq->lock);
    
    wait_node_t* node = flux_allocate(NULL, sizeof(wait_node_t), 
                                      FLUX_ALLOC_KERNEL);
    if (!node) {
        spinlock_release(&wq->lock);
        return;
    }
    
    node->quantum = quantum;
    node->next = NULL;
    
    if (wq->tail) {
        wq->tail->next = node;
    } else {
        wq->head = node;
    }
    wq->tail = node;
    wq->count++;
    
    // Block the quantum
    temporal_block(quantum, BLOCK_CONDUIT);
    
    spinlock_release(&wq->lock);
}

static quantum_context_t* waitqueue_remove(wait_queue_t* wq) {
    spinlock_acquire(&wq->lock);
    
    if (!wq->head) {
        spinlock_release(&wq->lock);
        return NULL;
    }
    
    wait_node_t* node = wq->head;
    quantum_context_t* quantum = node->quantum;
    
    wq->head = node->next;
    if (!wq->head) {
        wq->tail = NULL;
    }
    wq->count--;
    
    flux_free(node);
    
    // Unblock the quantum
    temporal_unblock(quantum);
    
    spinlock_release(&wq->lock);
    return quantum;
}

static void waitqueue_wake_all(wait_queue_t* wq) {
    while (wq->count > 0) {
        waitqueue_remove(wq);
    }
}

// =============================================================================
// Conduit Management
// =============================================================================

void conduit_init(void) {
    spinlock_acquire(&g_conduit_lock);
    
    // Clear conduit array
    for (int i = 0; i < MAX_CONDUITS; i++) {
        g_conduits[i] = NULL;
    }
    
    // Clear name table
    for (int i = 0; i < NAME_HASH_SIZE; i++) {
        g_name_table[i].name[0] = '\0';
        g_name_table[i].conduit = NULL;
    }
    
    g_conduit_registry.initialized = true;
    
    spinlock_release(&g_conduit_lock);
}

conduit_t* conduit_create(const char* name, size_t buffer_size) {
    if (!name || buffer_size == 0) {
        return NULL;
    }
    
    spinlock_acquire(&g_conduit_lock);
    
    // Check if name already exists
    uint32_t hash = hash_name(name);
    if (g_name_table[hash].conduit != NULL) {
        spinlock_release(&g_conduit_lock);
        return NULL;  // Name already in use
    }
    
    // Find free conduit slot
    int conduit_id = -1;
    for (int i = 0; i < MAX_CONDUITS; i++) {
        if (!g_conduits[i]) {
            conduit_id = i;
            break;
        }
    }
    
    if (conduit_id == -1) {
        spinlock_release(&g_conduit_lock);
        return NULL;  // No free slots
    }
    
    // Allocate conduit structure
    conduit_t* conduit = flux_allocate(NULL, sizeof(conduit_t), 
                                       FLUX_ALLOC_KERNEL | FLUX_ALLOC_ZERO);
    if (!conduit) {
        spinlock_release(&g_conduit_lock);
        return NULL;
    }
    
    // Initialize conduit
    conduit->id = conduit_id;
    strncpy(conduit->name, name, CONDUIT_NAME_MAX - 1);
    conduit->state = CONDUIT_STATE_OPEN;
    conduit->buffer_size = buffer_size;
    conduit->max_message_size = buffer_size / 4;  // Default max message size
    conduit->owner_qid = continuum_get_current_quantum()->qid;
    conduit->ref_count = 1;
    
    // Initialize ring buffer
    ringbuf_init(&conduit->messages, buffer_size);
    
    // Initialize wait queues
    waitqueue_init(&conduit->readers);
    waitqueue_init(&conduit->writers);
    
    // Initialize statistics
    conduit->stats.messages_sent = 0;
    conduit->stats.messages_received = 0;
    conduit->stats.bytes_sent = 0;
    conduit->stats.bytes_received = 0;
    
    spinlock_init(&conduit->lock);
    
    // Add to registry
    g_conduits[conduit_id] = conduit;
    g_name_table[hash].conduit = conduit;
    strncpy(g_name_table[hash].name, name, CONDUIT_NAME_MAX - 1);
    g_conduit_registry.conduit_count++;
    
    spinlock_release(&g_conduit_lock);
    return conduit;
}

conduit_t* conduit_open(const char* name) {
    if (!name) {
        return NULL;
    }
    
    spinlock_acquire(&g_conduit_lock);
    
    // Look up by name
    uint32_t hash = hash_name(name);
    conduit_t* conduit = g_name_table[hash].conduit;
    
    if (conduit && conduit->state == CONDUIT_STATE_OPEN) {
        conduit->ref_count++;
        spinlock_release(&g_conduit_lock);
        return conduit;
    }
    
    spinlock_release(&g_conduit_lock);
    return NULL;
}

void conduit_close(conduit_t* conduit) {
    if (!conduit) {
        return;
    }
    
    spinlock_acquire(&g_conduit_lock);
    
    conduit->ref_count--;
    
    if (conduit->ref_count == 0) {
        // Mark as closing
        conduit->state = CONDUIT_STATE_CLOSING;
        
        // Wake all waiting quanta
        waitqueue_wake_all(&conduit->readers);
        waitqueue_wake_all(&conduit->writers);
        
        // Remove from name table
        uint32_t hash = hash_name(conduit->name);
        if (g_name_table[hash].conduit == conduit) {
            g_name_table[hash].name[0] = '\0';
            g_name_table[hash].conduit = NULL;
        }
        
        // Free resources
        ringbuf_destroy(&conduit->messages);
        
        // Remove from registry
        g_conduits[conduit->id] = NULL;
        g_conduit_registry.conduit_count--;
        
        // Free conduit structure
        flux_free(conduit);
    }
    
    spinlock_release(&g_conduit_lock);
}

// =============================================================================
// Message Operations
// =============================================================================

int64_t conduit_send(conduit_t* conduit, const void* message, 
                    size_t size, uint32_t flags) {
    if (!conduit || !message || size == 0) {
        return -EINVAL;
    }
    
    if (conduit->state != CONDUIT_STATE_OPEN) {
        return -EPIPE;
    }
    
    if (size > conduit->max_message_size) {
        return -EMSGSIZE;
    }
    
    spinlock_acquire(&conduit->lock);
    
    // Check if there's space
    size_t header_size = sizeof(conduit_message_t);
    size_t total_size = header_size + size;
    
    if (conduit->messages.used + total_size > conduit->buffer_size) {
        if (flags & CONDUIT_FLAG_NONBLOCK) {
            spinlock_release(&conduit->lock);
            return -EAGAIN;
        }
        
        // Block until space available
        quantum_context_t* current = continuum_get_current_quantum();
        waitqueue_add(&conduit->writers, current);
        spinlock_release(&conduit->lock);
        
        // Will be woken when space is available
        temporal_yield(current);
        
        // Retry after waking
        return conduit_send(conduit, message, size, flags | CONDUIT_FLAG_NONBLOCK);
    }
    
    // Create message header
    conduit_message_t header = {
        .sender_qid = continuum_get_current_quantum()->qid,
        .size = size,
        .timestamp = continuum_get_time(),
        .flags = flags
    };
    
    // Write header and message
    ringbuf_write(&conduit->messages, &header, sizeof(header));
    ringbuf_write(&conduit->messages, message, size);
    
    // Update statistics
    conduit->stats.messages_sent++;
    conduit->stats.bytes_sent += size;
    g_conduit_registry.message_count++;
    g_conduit_registry.total_bytes += size;
    
    // Wake a reader if any
    waitqueue_remove(&conduit->readers);
    
    spinlock_release(&conduit->lock);
    return size;
}

int64_t conduit_receive(conduit_t* conduit, void* buffer, 
                       size_t max_size, uint32_t flags) {
    if (!conduit || !buffer || max_size == 0) {
        return -EINVAL;
    }
    
    if (conduit->state != CONDUIT_STATE_OPEN) {
        return -EPIPE;
    }
    
    spinlock_acquire(&conduit->lock);
    
    // Check if there's a message
    if (conduit->messages.used < sizeof(conduit_message_t)) {
        if (flags & CONDUIT_FLAG_NONBLOCK) {
            spinlock_release(&conduit->lock);
            return -EAGAIN;
        }
        
        // Block until message available
        quantum_context_t* current = continuum_get_current_quantum();
        waitqueue_add(&conduit->readers, current);
        spinlock_release(&conduit->lock);
        
        // Will be woken when message arrives
        temporal_yield(current);
        
        // Retry after waking
        return conduit_receive(conduit, buffer, max_size, 
                             flags | CONDUIT_FLAG_NONBLOCK);
    }
    
    // Peek at message header
    conduit_message_t header;
    ringbuf_peek(&conduit->messages, &header, sizeof(header));
    
    if (header.size > max_size) {
        spinlock_release(&conduit->lock);
        return -EMSGSIZE;
    }
    
    // Read header and message
    ringbuf_read(&conduit->messages, &header, sizeof(header));
    size_t bytes_read = ringbuf_read(&conduit->messages, buffer, header.size);
    
    // Update statistics
    conduit->stats.messages_received++;
    conduit->stats.bytes_received += bytes_read;
    
    // Wake a writer if any
    waitqueue_remove(&conduit->writers);
    
    spinlock_release(&conduit->lock);
    return bytes_read;
}

int64_t conduit_peek(conduit_t* conduit, void* buffer, size_t max_size) {
    if (!conduit || !buffer || max_size == 0) {
        return -EINVAL;
    }
    
    spinlock_acquire(&conduit->lock);
    
    if (conduit->messages.used < sizeof(conduit_message_t)) {
        spinlock_release(&conduit->lock);
        return 0;
    }
    
    // Peek at message header
    conduit_message_t header;
    size_t offset = 0;
    
    // Peek header
    ringbuf_peek(&conduit->messages, &header, sizeof(header));
    
    if (header.size > max_size) {
        spinlock_release(&conduit->lock);
        return -EMSGSIZE;
    }
    
    // Peek message data (skip header)
    uint8_t temp_buffer[sizeof(header) + header.size];
    ringbuf_peek(&conduit->messages, temp_buffer, sizeof(header) + header.size);
    memcpy(buffer, temp_buffer + sizeof(header), header.size);
    
    spinlock_release(&conduit->lock);
    return header.size;
}

// =============================================================================
// Broadcast and Multicast
// =============================================================================

int64_t conduit_broadcast(const char* pattern, const void* message, 
                         size_t size, uint32_t flags) {
    int64_t sent_count = 0;
    
    spinlock_acquire(&g_conduit_lock);
    
    // Send to all matching conduits
    for (int i = 0; i < MAX_CONDUITS; i++) {
        if (g_conduits[i] && g_conduits[i]->state == CONDUIT_STATE_OPEN) {
            // Simple pattern matching (could be more sophisticated)
            if (strstr(g_conduits[i]->name, pattern)) {
                spinlock_release(&g_conduit_lock);
                
                if (conduit_send(g_conduits[i], message, size, 
                               flags | CONDUIT_FLAG_NONBLOCK) > 0) {
                    sent_count++;
                }
                
                spinlock_acquire(&g_conduit_lock);
            }
        }
    }
    
    spinlock_release(&g_conduit_lock);
    return sent_count;
}

// =============================================================================
// Conduit Selection
// =============================================================================

int conduit_select(conduit_t** conduits, size_t count, 
                  conduit_select_op_t* ops, uint64_t timeout) {
    uint64_t start_time = continuum_get_time();
    int ready_count = 0;
    
    while (1) {
        // Check each conduit
        for (size_t i = 0; i < count; i++) {
            if (!conduits[i]) continue;
            
            bool ready = false;
            
            if (ops[i] & CONDUIT_SELECT_READ) {
                if (conduits[i]->messages.used >= sizeof(conduit_message_t)) {
                    ready = true;
                    ops[i] |= CONDUIT_SELECT_READ_READY;
                }
            }
            
            if (ops[i] & CONDUIT_SELECT_WRITE) {
                if (conduits[i]->messages.used < conduits[i]->buffer_size / 2) {
                    ready = true;
                    ops[i] |= CONDUIT_SELECT_WRITE_READY;
                }
            }
            
            if (ready) {
                ready_count++;
            }
        }
        
        if (ready_count > 0) {
            return ready_count;
        }
        
        // Check timeout
        if (timeout > 0) {
            uint64_t elapsed = continuum_get_time() - start_time;
            if (elapsed >= timeout) {
                return 0;  // Timeout
            }
        }
        
        // Yield to scheduler
        temporal_yield(NULL);
    }
}

// =============================================================================
// Statistics and Debugging
// =============================================================================

void conduit_get_stats(conduit_t* conduit, conduit_stats_t* stats) {
    if (!conduit || !stats) {
        return;
    }
    
    spinlock_acquire(&conduit->lock);
    *stats = conduit->stats;
    spinlock_release(&conduit->lock);
}

void conduit_get_global_stats(conduit_global_stats_t* stats) {
    if (!stats) {
        return;
    }
    
    spinlock_acquire(&g_conduit_lock);
    
    stats->total_conduits = g_conduit_registry.conduit_count;
    stats->total_messages = g_conduit_registry.message_count;
    stats->total_bytes = g_conduit_registry.total_bytes;
    
    // Calculate active conduits
    stats->active_conduits = 0;
    for (int i = 0; i < MAX_CONDUITS; i++) {
        if (g_conduits[i] && g_conduits[i]->state == CONDUIT_STATE_OPEN) {
            stats->active_conduits++;
        }
    }
    
    spinlock_release(&g_conduit_lock);
}

// =============================================================================
// Helper Functions
// =============================================================================

int strncpy(char* dest, const char* src, size_t n) {
    size_t i;
    for (i = 0; i < n - 1 && src[i]; i++) {
        dest[i] = src[i];
    }
    dest[i] = '\0';
    return i;
}

char* strstr(const char* haystack, const char* needle) {
    if (!*needle) return (char*)haystack;
    
    for (; *haystack; haystack++) {
        const char* h = haystack;
        const char* n = needle;
        
        while (*h && *n && *h == *n) {
            h++;
            n++;
        }
        
        if (!*n) {
            return (char*)haystack;
        }
    }
    
    return NULL;
}
