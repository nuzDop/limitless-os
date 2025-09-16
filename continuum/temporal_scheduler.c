/*
 * Temporal Scheduler for Continuum Kernel
 * Advanced quantum-aware scheduling with AI-guided optimization
 */

#include "temporal_scheduler.h"
#include "continuum_core.h"
#include "flux_memory.h"

// =============================================================================
// Global Scheduler State
// =============================================================================

static temporal_scheduler_t g_scheduler = {
    .initialized = false,
    .running = false,
    .total_quanta = 0,
    .total_switches = 0,
    .scheduler_ticks = 0
};

static scheduler_queue_t g_ready_queues[PRIORITY_MAX];
static quantum_context_t* g_idle_quantum;
static spinlock_t g_scheduler_lock = SPINLOCK_INIT;

// Per-CPU run queues
static struct {
    quantum_context_t* current;
    quantum_context_t* next;
    uint64_t last_switch;
    uint32_t load;
} g_cpu_queues[MAX_CPU_CORES];

// =============================================================================
// Queue Management
// =============================================================================

static void queue_init(scheduler_queue_t* queue, priority_t priority) {
    queue->head = NULL;
    queue->tail = NULL;
    queue->count = 0;
    queue->priority = priority;
    spinlock_init(&queue->lock);
}

static void queue_enqueue(scheduler_queue_t* queue, quantum_context_t* quantum) {
    spinlock_acquire(&queue->lock);
    
    quantum->next_ready = NULL;
    quantum->prev_ready = queue->tail;
    
    if (queue->tail) {
        queue->tail->next_ready = quantum;
    } else {
        queue->head = quantum;
    }
    
    queue->tail = quantum;
    queue->count++;
    
    spinlock_release(&queue->lock);
}

static quantum_context_t* queue_dequeue(scheduler_queue_t* queue) {
    spinlock_acquire(&queue->lock);
    
    quantum_context_t* quantum = queue->head;
    if (quantum) {
        queue->head = quantum->next_ready;
        if (queue->head) {
            queue->head->prev_ready = NULL;
        } else {
            queue->tail = NULL;
        }
        queue->count--;
        
        quantum->next_ready = NULL;
        quantum->prev_ready = NULL;
    }
    
    spinlock_release(&queue->lock);
    return quantum;
}

static void queue_remove(scheduler_queue_t* queue, quantum_context_t* quantum) {
    spinlock_acquire(&queue->lock);
    
    if (quantum->prev_ready) {
        quantum->prev_ready->next_ready = quantum->next_ready;
    } else {
        queue->head = quantum->next_ready;
    }
    
    if (quantum->next_ready) {
        quantum->next_ready->prev_ready = quantum->prev_ready;
    } else {
        queue->tail = quantum->prev_ready;
    }
    
    quantum->next_ready = NULL;
    quantum->prev_ready = NULL;
    queue->count--;
    
    spinlock_release(&queue->lock);
}

// =============================================================================
// Scheduler Core
// =============================================================================

void temporal_init(uint32_t num_cores) {
    // Initialize ready queues
    for (int i = 0; i < PRIORITY_MAX; i++) {
        queue_init(&g_ready_queues[i], i);
    }
    
    // Initialize per-CPU structures
    for (uint32_t i = 0; i < num_cores && i < MAX_CPU_CORES; i++) {
        g_cpu_queues[i].current = NULL;
        g_cpu_queues[i].next = NULL;
        g_cpu_queues[i].last_switch = 0;
        g_cpu_queues[i].load = 0;
    }
    
    // Create idle quantum
    g_idle_quantum = flux_allocate(NULL, sizeof(quantum_context_t), 
                                  FLUX_ALLOC_KERNEL | FLUX_ALLOC_ZERO);
    g_idle_quantum->qid = IDLE_QUANTUM_ID;
    g_idle_quantum->state = QUANTUM_STATE_READY;
    g_idle_quantum->scheduling.priority = PRIORITY_IDLE;
    strncpy(g_idle_quantum->name, "idle", sizeof(g_idle_quantum->name));
    
    g_scheduler.num_cores = num_cores;
    g_scheduler.initialized = true;
}

void temporal_start(void) {
    if (!g_scheduler.initialized) {
        continuum_panic("Temporal scheduler not initialized!");
    }
    
    g_scheduler.running = true;
    g_scheduler.start_time = continuum_get_time();
    
    // Start scheduling on boot CPU
    temporal_schedule();
    
    // Should never return
    continuum_panic("Scheduler returned!");
}

void temporal_stop(void) {
    g_scheduler.running = false;
}

// =============================================================================
// Quantum Scheduling
// =============================================================================

void temporal_enqueue(quantum_context_t* quantum) {
    if (!quantum || quantum->state != QUANTUM_STATE_READY) {
        return;
    }
    
    priority_t priority = quantum->scheduling.priority;
    if (priority >= PRIORITY_MAX) {
        priority = PRIORITY_NORMAL;
    }
    
    queue_enqueue(&g_ready_queues[priority], quantum);
    g_scheduler.total_quanta++;
    
    // Wake up idle CPUs if needed
    temporal_wake_idle_cpu();
}

void temporal_remove_quantum(quantum_context_t* quantum) {
    if (!quantum) {
        return;
    }
    
    // Remove from ready queue if present
    for (int i = 0; i < PRIORITY_MAX; i++) {
        if (g_ready_queues[i].count > 0) {
            queue_remove(&g_ready_queues[i], quantum);
        }
    }
    
    g_scheduler.total_quanta--;
}

void temporal_yield(quantum_context_t* quantum) {
    if (!quantum) {
        quantum = temporal_get_current();
    }
    
    quantum->state = QUANTUM_STATE_READY;
    quantum->stats.context_switches++;
    
    // Re-enqueue at end of same priority queue
    temporal_enqueue(quantum);
    
    // Trigger reschedule
    temporal_schedule();
}

void temporal_block(quantum_context_t* quantum, block_reason_t reason) {
    if (!quantum) {
        quantum = temporal_get_current();
    }
    
    quantum->state = QUANTUM_STATE_BLOCKED;
    quantum->scheduling.block_reason = reason;
    quantum->scheduling.block_time = continuum_get_time();
    
    // Remove from CPU
    uint32_t cpu = temporal_get_current_cpu();
    if (g_cpu_queues[cpu].current == quantum) {
        g_cpu_queues[cpu].current = NULL;
        temporal_schedule();
    }
}

void temporal_unblock(quantum_context_t* quantum) {
    if (!quantum || quantum->state != QUANTUM_STATE_BLOCKED) {
        return;
    }
    
    quantum->state = QUANTUM_STATE_READY;
    quantum->scheduling.block_reason = BLOCK_NONE;
    
    // Add back to ready queue
    temporal_enqueue(quantum);
}

// =============================================================================
// Core Scheduling Algorithm
// =============================================================================

static quantum_context_t* select_next_quantum(uint32_t cpu_id) {
    quantum_context_t* next = NULL;
    
    // Check for explicitly scheduled quantum
    if (g_cpu_queues[cpu_id].next) {
        next = g_cpu_queues[cpu_id].next;
        g_cpu_queues[cpu_id].next = NULL;
        return next;
    }
    
    // Priority-based selection
    for (int priority = PRIORITY_REALTIME; priority >= PRIORITY_LOW; priority--) {
        if (g_ready_queues[priority].count > 0) {
            next = queue_dequeue(&g_ready_queues[priority]);
            if (next) {
                // Check CPU affinity
                if (next->scheduling.cpu_affinity == CPU_AFFINITY_ANY ||
                    (next->scheduling.cpu_mask & (1ULL << cpu_id))) {
                    return next;
                } else {
                    // Wrong CPU, re-enqueue
                    queue_enqueue(&g_ready_queues[priority], next);
                }
            }
        }
    }
    
    // No ready quantum, return idle
    return g_idle_quantum;
}

void temporal_schedule(void) {
    if (!g_scheduler.running) {
        return;
    }
    
    uint32_t cpu_id = temporal_get_current_cpu();
    quantum_context_t* current = g_cpu_queues[cpu_id].current;
    quantum_context_t* next = select_next_quantum(cpu_id);
    
    if (next == current) {
        // Same quantum continues
        return;
    }
    
    // Perform context switch
    if (current && current != g_idle_quantum) {
        // Save current context
        temporal_save_context(current);
        
        // Update statistics
        uint64_t now = continuum_get_time();
        uint64_t runtime = now - g_cpu_queues[cpu_id].last_switch;
        current->stats.cpu_time += runtime;
        
        // Re-enqueue if still ready
        if (current->state == QUANTUM_STATE_RUNNING) {
            current->state = QUANTUM_STATE_READY;
            temporal_enqueue(current);
        }
    }
    
    // Switch to next quantum
    g_cpu_queues[cpu_id].current = next;
    g_cpu_queues[cpu_id].last_switch = continuum_get_time();
    next->state = QUANTUM_STATE_RUNNING;
    next->stats.context_switches++;
    g_scheduler.total_switches++;
    
    // Load new context
    temporal_load_context(next);
}

// =============================================================================
// Timer Interrupt Handler
// =============================================================================

void temporal_tick(void) {
    g_scheduler.scheduler_ticks++;
    
    uint32_t cpu_id = temporal_get_current_cpu();
    quantum_context_t* current = g_cpu_queues[cpu_id].current;
    
    if (!current || current == g_idle_quantum) {
        // CPU is idle
        g_cpu_queues[cpu_id].load = 0;
        return;
    }
    
    // Update load tracking
    g_cpu_queues[cpu_id].load = (g_cpu_queues[cpu_id].load * 7 + 100) / 8;
    
    // Check time slice expiration
    uint64_t runtime = continuum_get_time() - g_cpu_queues[cpu_id].last_switch;
    if (runtime >= current->scheduling.time_slice) {
        // Time slice expired, reschedule
        temporal_schedule();
    }
    
    // Check deadlines for real-time quanta
    if (current->scheduling.priority == PRIORITY_REALTIME &&
        current->scheduling.deadline > 0) {
        uint64_t now = continuum_get_time();
        if (now >= current->scheduling.deadline) {
            // Deadline missed - handle appropriately
            current->stats.deadline_misses++;
        }
    }
}

// =============================================================================
// AI-Guided Optimization
// =============================================================================

void temporal_update_ai_hints(nexus_hint_t* hints, size_t count) {
    if (!hints || count == 0) {
        return;
    }
    
    spinlock_acquire(&g_scheduler.ai_lock);
    
    for (size_t i = 0; i < count; i++) {
        quantum_context_t* quantum = continuum_get_quantum(hints[i].qid);
        if (quantum) {
            // Update AI weight
            quantum->scheduling.ai_weight = hints[i].weight;
            
            // Adjust priority based on prediction
            if (hints[i].predicted_cpu_burst > 0) {
                if (hints[i].predicted_cpu_burst < 1000) {  // < 1ms
                    quantum->scheduling.priority = PRIORITY_HIGH;
                } else if (hints[i].predicted_cpu_burst > 10000) {  // > 10ms
                    quantum->scheduling.priority = PRIORITY_LOW;
                }
            }
            
            // Update CPU affinity based on cache prediction
            if (hints[i].predicted_cache_affinity >= 0) {
                quantum->scheduling.cpu_mask = 1ULL << hints[i].predicted_cache_affinity;
                quantum->scheduling.cpu_affinity = CPU_AFFINITY_SINGLE;
            }
        }
    }
    
    spinlock_release(&g_scheduler.ai_lock);
}

// =============================================================================
// Load Balancing
// =============================================================================

void temporal_balance_load(void) {
    // Simple load balancing algorithm
    uint32_t min_load = UINT32_MAX;
    uint32_t max_load = 0;
    uint32_t min_cpu = 0;
    uint32_t max_cpu = 0;
    
    // Find most and least loaded CPUs
    for (uint32_t i = 0; i < g_scheduler.num_cores; i++) {
        uint32_t load = g_cpu_queues[i].load;
        if (load < min_load) {
            min_load = load;
            min_cpu = i;
        }
        if (load > max_load) {
            max_load = load;
            max_cpu = i;
        }
    }
    
    // If imbalance is significant, migrate a quantum
    if (max_load - min_load > 50) {  // 50% difference
        // Find a quantum to migrate from max_cpu to min_cpu
        quantum_context_t* current = g_cpu_queues[max_cpu].current;
        if (current && current != g_idle_quantum &&
            current->scheduling.cpu_affinity == CPU_AFFINITY_ANY) {
            // Schedule this quantum on the less loaded CPU
            g_cpu_queues[min_cpu].next = current;
            g_cpu_queues[max_cpu].current = NULL;
            temporal_schedule();
        }
    }
}

// =============================================================================
// Statistics and Debugging
// =============================================================================

void temporal_get_stats(temporal_stats_t* stats) {
    if (!stats) {
        return;
    }
    
    stats->total_quanta = g_scheduler.total_quanta;
    stats->total_switches = g_scheduler.total_switches;
    stats->scheduler_ticks = g_scheduler.scheduler_ticks;
    stats->uptime = continuum_get_time() - g_scheduler.start_time;
    
    // Calculate ready queue lengths
    stats->ready_count = 0;
    for (int i = 0; i < PRIORITY_MAX; i++) {
        stats->ready_count += g_ready_queues[i].count;
    }
    
    // Calculate CPU utilization
    uint64_t total_load = 0;
    for (uint32_t i = 0; i < g_scheduler.num_cores; i++) {
        total_load += g_cpu_queues[i].load;
    }
    stats->cpu_utilization = total_load / g_scheduler.num_cores;
}

// =============================================================================
// Context Switching (Architecture Specific)
// =============================================================================

void temporal_save_context(quantum_context_t* quantum) {
    if (!quantum || !quantum->register_state) {
        return;
    }
    
    // Save general purpose registers
    __asm__ __volatile__(
        "movq %%rax, %0\n"
        "movq %%rbx, %1\n"
        "movq %%rcx, %2\n"
        "movq %%rdx, %3\n"
        "movq %%rsi, %4\n"
        "movq %%rdi, %5\n"
        "movq %%rbp, %6\n"
        "movq %%rsp, %7\n"
        : "=m"(quantum->register_state->rax),
          "=m"(quantum->register_state->rbx),
          "=m"(quantum->register_state->rcx),
          "=m"(quantum->register_state->rdx),
          "=m"(quantum->register_state->rsi),
          "=m"(quantum->register_state->rdi),
          "=m"(quantum->register_state->rbp),
          "=m"(quantum->register_state->rsp)
    );
    
    // Save extended registers
    __asm__ __volatile__(
        "movq %%r8, %0\n"
        "movq %%r9, %1\n"
        "movq %%r10, %2\n"
        "movq %%r11, %3\n"
        "movq %%r12, %4\n"
        "movq %%r13, %5\n"
        "movq %%r14, %6\n"
        "movq %%r15, %7\n"
        : "=m"(quantum->register_state->r8),
          "=m"(quantum->register_state->r9),
          "=m"(quantum->register_state->r10),
          "=m"(quantum->register_state->r11),
          "=m"(quantum->register_state->r12),
          "=m"(quantum->register_state->r13),
          "=m"(quantum->register_state->r14),
          "=m"(quantum->register_state->r15)
    );
    
    // Save flags and instruction pointer
    __asm__ __volatile__(
        "pushfq\n"
        "popq %0\n"
        "leaq 1f(%%rip), %1\n"
        "1:\n"
        : "=m"(quantum->register_state->rflags),
          "=m"(quantum->register_state->rip)
    );
    
    // Save CR3 (page table)
    __asm__ __volatile__(
        "movq %%cr3, %%rax\n"
        "movq %%rax, %0\n"
        : "=m"(quantum->register_state->cr3)
        : : "rax"
    );
}

void temporal_load_context(quantum_context_t* quantum) {
    if (!quantum || !quantum->register_state) {
        return;
    }
    
    // Load CR3 (switch page tables)
    __asm__ __volatile__(
        "movq %0, %%rax\n"
        "movq %%rax, %%cr3\n"
        : : "m"(quantum->register_state->cr3)
        : "rax"
    );
    
    // Load general purpose registers
    __asm__ __volatile__(
        "movq %0, %%rax\n"
        "movq %1, %%rbx\n"
        "movq %2, %%rcx\n"
        "movq %3, %%rdx\n"
        "movq %4, %%rsi\n"
        "movq %5, %%rdi\n"
        "movq %6, %%rbp\n"
        "movq %7, %%rsp\n"
        : : "m"(quantum->register_state->rax),
            "m"(quantum->register_state->rbx),
            "m"(quantum->register_state->rcx),
            "m"(quantum->register_state->rdx),
            "m"(quantum->register_state->rsi),
            "m"(quantum->register_state->rdi),
            "m"(quantum->register_state->rbp),
            "m"(quantum->register_state->rsp)
    );
    
    // Load extended registers
    __asm__ __volatile__(
        "movq %0, %%r8\n"
        "movq %1, %%r9\n"
        "movq %2, %%r10\n"
        "movq %3, %%r11\n"
        "movq %4, %%r12\n"
        "movq %5, %%r13\n"
        "movq %6, %%r14\n"
        "movq %7, %%r15\n"
        : : "m"(quantum->register_state->r8),
            "m"(quantum->register_state->r9),
            "m"(quantum->register_state->r10),
            "m"(quantum->register_state->r11),
            "m"(quantum->register_state->r12),
            "m"(quantum->register_state->r13),
            "m"(quantum->register_state->r14),
            "m"(quantum->register_state->r15)
    );
    
    // Load flags and jump to saved instruction pointer
    __asm__ __volatile__(
        "pushq %0\n"
        "popfq\n"
        "jmpq *%1\n"
        : : "m"(quantum->register_state->rflags),
            "m"(quantum->register_state->rip)
    );
}

// =============================================================================
// Helper Functions
// =============================================================================

quantum_context_t* temporal_get_current(void) {
    uint32_t cpu_id = temporal_get_current_cpu();
    return g_cpu_queues[cpu_id].current;
}

uint32_t temporal_get_current_cpu(void) {
    // Read APIC ID or use other method to get current CPU
    // Simplified for now
    return 0;
}

void temporal_wake_idle_cpu(void) {
    // Send IPI to wake up an idle CPU
    // Implementation depends on interrupt controller
}
