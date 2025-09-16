/*
 * Conduit IPC System Header
 * High-performance inter-quantum communication
 */

#ifndef CONDUIT_IPC_H
#define CONDUIT_IPC_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "continuum_core.h"

// =============================================================================
// Constants
// =============================================================================

#define MAX_CONDUITS           1024
#define CONDUIT_NAME_MAX       64
#define DEFAULT_BUFFER_SIZE    65536  // 64KB
#define MAX_MESSAGE_SIZE       16384  // 16KB

// Conduit flags
#define CONDUIT_FLAG_NONBLOCK      (1 << 0)
#define CONDUIT_FLAG_BROADCAST     (1 << 1)
#define CONDUIT_FLAG_PRIORITY      (1 << 2)
#define CONDUIT_FLAG_COMPRESSED    (1 << 3)
#define CONDUIT_FLAG_ENCRYPTED     (1 << 4)

// Select operations
#define CONDUIT_SELECT_READ        (1 << 0)
#define CONDUIT_SELECT_WRITE       (1 << 1)
#define CONDUIT_SELECT_ERROR       (1 << 2)
#define CONDUIT_SELECT_READ_READY  (1 << 16)
#define CONDUIT_SELECT_WRITE_READY (1 << 17)
#define CONDUIT_SELECT_ERROR_READY (1 << 18)

// Error codes (negative)
#define EPIPE      32
#define EMSGSIZE   90
#define EAGAIN     11

// =============================================================================
// Type Definitions
// =============================================================================

typedef uint32_t conduit_select_op_t;

// Conduit states
typedef enum {
    CONDUIT_STATE_CLOSED = 0,
    CONDUIT_STATE_OPEN,
    CONDUIT_STATE_CLOSING,
    CONDUIT_STATE_ERROR
} conduit_state_t;

// Message header
typedef struct {
    quantum_id_t sender_qid;
    size_t size;
    uint64_t timestamp;
    uint32_t flags;
} conduit_message_t;

// Ring buffer for messages
typedef struct {
    void* buffer;
    size_t size;
    size_t head;
    size_t tail;
    size_t used;
    spinlock_t lock;
} ring_buffer_t;

// Wait queue node
typedef struct wait_node {
    quantum_context_t* quantum;
    struct wait_node* next;
} wait_node_t;

// Wait queue
typedef struct {
    wait_node_t* head;
    wait_node_t* tail;
    uint32_t count;
    spinlock_t lock;
} wait_queue_t;

// Conduit statistics
typedef struct {
    uint64_t messages_sent;
    uint64_t messages_received;
    uint64_t bytes_sent;
    uint64_t bytes_received;
    uint64_t dropped_messages;
    uint64_t peak_usage;
} conduit_stats_t;

// Conduit structure
typedef struct conduit {
    uint32_t id;
    char name[CONDUIT_NAME_MAX];
    conduit_state_t state;
    
    // Buffer management
    ring_buffer_t messages;
    size_t buffer_size;
    size_t max_message_size;
    
    // Ownership and permissions
    quantum_id_t owner_qid;
    uint32_t permissions;
    uint32_t ref_count;
    
    // Wait queues
    wait_queue_t readers;
    wait_queue_t writers;
    
    // Statistics
    conduit_stats_t stats;
    
    // Synchronization
    spinlock_t lock;
} conduit_t;

// Global conduit statistics
typedef struct {
    uint32_t total_conduits;
    uint32_t active_conduits;
    uint64_t total_messages;
    uint64_t total_bytes;
} conduit_global_stats_t;

// Conduit registry
typedef struct {
    bool initialized;
    uint32_t conduit_count;
    uint64_t message_count;
    uint64_t total_bytes;
} conduit_registry_t;

// =============================================================================
// Function Prototypes
// =============================================================================

// Initialization
void conduit_init(void);

// Conduit management
conduit_t* conduit_create(const char* name, size_t buffer_size);
conduit_t* conduit_open(const char* name);
void conduit_close(conduit_t* conduit);
void conduit_destroy(conduit_t* conduit);

// Message operations
int64_t conduit_send(conduit_t* conduit, const void* message, 
                    size_t size, uint32_t flags);
int64_t conduit_receive(conduit_t* conduit, void* buffer, 
                       size_t max_size, uint32_t flags);
int64_t conduit_peek(conduit_t* conduit, void* buffer, size_t max_size);

// Advanced operations
int64_t conduit_broadcast(const char* pattern, const void* message, 
                         size_t size, uint32_t flags);
int conduit_select(conduit_t** conduits, size_t count, 
                  conduit_select_op_t* ops, uint64_t timeout);

// Buffer management
size_t conduit_get_buffer_size(conduit_t* conduit);
size_t conduit_get_used_space(conduit_t* conduit);
size_t conduit_get_free_space(conduit_t* conduit);
int conduit_resize_buffer(conduit_t* conduit, size_t new_size);

// Statistics
void conduit_get_stats(conduit_t* conduit, conduit_stats_t* stats);
void conduit_get_global_stats(conduit_global_stats_t* stats);

// Helper functions (minimal string operations)
int strncpy(char* dest, const char* src, size_t n);
char* strstr(const char* haystack, const char* needle);

#endif /* CONDUIT_IPC_H */
