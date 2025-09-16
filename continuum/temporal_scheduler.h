/*
 * Temporal Scheduler Header
 * Quantum scheduling with AI-guided optimization
 */

#ifndef TEMPORAL_SCHEDULER_H
#define TEMPORAL_SCHEDULER_H

#include <stdint.h>
#include <stdbool.h>
#include "continuum_core.h"

// =============================================================================
// Constants
// =============================================================================

#define PRIORITY_MAX            5
#define IDLE_QUANTUM_ID        0xFFFFFFFFFFFFFFFFULL
#define DEFAULT_TIME_SLICE     10000  // 10ms in microseconds
#define MIN_TIME_SLICE         1000   // 1ms
#define MAX_TIME_SLICE         100000 // 100ms

// =============================================================================
// Type Definitions
// =============================================================================

// Block reasons
typedef enum {
    BLOCK_NONE = 0,
    BLOCK_IO,
    BLOCK_CONDUIT,
    BLOCK_MUTEX,
    BLOCK_SLEEP,
    BLOCK_WAIT
} block_reason_t;

// =============================================================================
// Data Structures
// =============================================================================

// Scheduler queue
typedef struct {
    quantum_context_t* head;
    quantum_context_t* tail;
    uint32_t count;
    priority_t priority;
    spinlock_t lock;
} scheduler_queue_t;

// Nexus AI hint
typedef struct {
    quantum_id_t qid;
    float weight;
    uint64_t predicted_cpu_burst;
    int32_t predicted_cache_affinity;
    uint32_t predicted_io_wait;
} nexus_hint_t;

// Scheduler statistics
typedef struct {
    uint64_t total_quanta;
    uint64_t total_switches;
    uint64_t scheduler_ticks;
    uint64_t uptime;
    uint32_t ready_count;
    uint32_t blocked_count;
    uint32_t cpu_utilization;
} temporal_stats_t;

// Main scheduler structure
typedef struct {
    bool initialized;
    bool running;
    uint32_t num_cores;
    uint64_t start_time;
    uint64_t total_quanta;
    uint64_t total_switches;
    uint64_t scheduler_ticks;
    spinlock_t ai_lock;
} temporal_scheduler_t;

// =============================================================================
// Function Prototypes
// =============================================================================

// Initialization
void temporal_init(uint32_t num_cores);
void temporal_start(void);
void temporal_stop(void);

// Quantum management
void temporal_enqueue(quantum_context_t* quantum);
void temporal_remove_quantum(quantum_context_t* quantum);
void temporal_yield(quantum_context_t* quantum);
void temporal_block(quantum_context_t* quantum, block_reason_t reason);
void temporal_unblock(quantum_context_t* quantum);

// Scheduling
void temporal_schedule(void);
void temporal_tick(void);
quantum_context_t* temporal_get_current(void);
uint32_t temporal_get_current_cpu(void);

// AI integration
void temporal_update_ai_hints(nexus_hint_t* hints, size_t count);

// Load balancing
void temporal_balance_load(void);
void temporal_wake_idle_cpu(void);

// Statistics
void temporal_get_stats(temporal_stats_t* stats);

// Context switching
void temporal_save_context(quantum_context_t* quantum);
void temporal_load_context(quantum_context_t* quantum);

#endif /* TEMPORAL_SCHEDULER_H */
