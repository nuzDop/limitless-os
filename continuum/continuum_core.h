/*
 * Continuum Kernel Core Header
 * Core definitions and interfaces for the Continuum microkernel
 */

#ifndef CONTINUUM_CORE_H
#define CONTINUUM_CORE_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

// =============================================================================
// Constants and Magic Numbers
// =============================================================================

#define CONTINUUM_MAGIC         0x434F4E54494E5555ULL  // "CONTINUU"
#define CONTINUUM_VERSION       0x01000000              // 1.0.0.0
#define MAX_QUANTA             1024
#define MAX_CPU_CORES          256
#define MAX_CAPABILITIES       64
#define DEFAULT_TIME_SLICE     10000                    // 10ms in microseconds
#define INVALID_QID            0

// Error codes
#define EINVAL                 22
#define EPERM                  1
#define ENOSYS                 38
#define ENOMEM                 12

// =============================================================================
// Type Definitions
// =============================================================================

typedef uint64_t quantum_id_t;
typedef uint64_t capability_t;

struct memory_domain;
typedef struct memory_domain memory_domain_t;

// ABI Modes - The three faces of Continuum
typedef enum {
    ABI_MODE_NATIVE = 0,    // Native LimitlessOS ABI
    ABI_MODE_AXON = 1,      // Windows ABI comprehension
    ABI_MODE_VORTEX = 2,    // Linux ABI comprehension  
    ABI_MODE_CIPHER = 3     // macOS ABI comprehension
} abi_mode_t;

// Quantum states
typedef enum {
    QUANTUM_STATE_CREATED = 0,
    QUANTUM_STATE_READY,
    QUANTUM_STATE_RUNNING,
    QUANTUM_STATE_BLOCKED,
    QUANTUM_STATE_SLEEPING,
    QUANTUM_STATE_TERMINATED
} quantum_state_t;

// Priority levels
typedef enum {
    PRIORITY_IDLE = 0,
    PRIORITY_LOW = 1,
    PRIORITY_NORMAL = 2,
    PRIORITY_HIGH = 3,
    PRIORITY_REALTIME = 4
} priority_t;

// CPU affinity
typedef enum {
    CPU_AFFINITY_ANY = 0,
    CPU_AFFINITY_SINGLE = 1,
    CPU_AFFINITY_NUMA = 2,
    CPU_AFFINITY_CUSTOM = 3
} cpu_affinity_t;

// Kernel states
typedef enum {
    KERNEL_STATE_BOOTING = 0,
    KERNEL_STATE_INITIALIZING,
    KERNEL_STATE_RUNNING,
    KERNEL_STATE_SUSPENDED,
    KERNEL_STATE_PANIC
} kernel_state_t;

// Core states
typedef enum {
    CORE_STATE_OFFLINE = 0,
    CORE_STATE_IDLE,
    CORE_STATE_BUSY,
    CORE_STATE_HALTED
} core_state_t;

// =============================================================================
// Core Data Structures
// =============================================================================

// System request structure
typedef struct {
    uint64_t request_id;
    uint64_t params[8];
    uint64_t flags;
} system_request_t;

// System request IDs
enum {
    SYSREQ_MEMORY_ALLOCATE = 1,
    SYSREQ_MEMORY_FREE,
    SYSREQ_MEMORY_MAP,
    SYSREQ_MEMORY_PROTECT,
    SYSREQ_CONDUIT_CREATE,
    SYSREQ_CONDUIT_SEND,
    SYSREQ_CONDUIT_RECEIVE,
    SYSREQ_QUANTUM_SPAWN,
    SYSREQ_QUANTUM_TERMINATE,
    SYSREQ_QUANTUM_YIELD,
    SYSREQ_QUANTUM_SLEEP,
    SYSREQ_TIME_GET,
    SYSREQ_CAPABILITY_REQUEST,
    SYSREQ_CAPABILITY_DROP
};

// Capability set
typedef struct {
    uint64_t bitmap[MAX_CAPABILITIES / 64];
    uint32_t count;
} capability_set_t;

// Scheduling information
typedef struct {
    priority_t priority;
    uint64_t time_slice;
    uint64_t deadline;
    cpu_affinity_t cpu_affinity;
    uint64_t cpu_mask;
    float ai_weight;  // Nexus Core optimization hint
} scheduling_info_t;

// Quantum statistics
typedef struct {
    uint64_t creation_time;
    uint64_t cpu_time;
    uint64_t wall_time;
    uint64_t context_switches;
    uint64_t page_faults;
    uint64_t system_calls;
    uint64_t conduit_messages;
} quantum_stats_t;

// Register state (simplified)
typedef struct {
    uint64_t rax, rbx, rcx, rdx;
    uint64_t rsi, rdi, rbp, rsp;
    uint64_t r8, r9, r10, r11;
    uint64_t r12, r13, r14, r15;
    uint64_t rip, rflags;
    uint64_t cr3;  // Page table base
    // FPU/SSE state would go here
} register_state_t;

// Quantum context - The fundamental execution unit
typedef struct quantum_context {
    // Identity
    quantum_id_t qid;
    char name[64];
    abi_mode_t abi_mode;
    quantum_state_t state;
    
    // Execution
    void* entry_point;
    register_state_t* register_state;
    memory_domain_t* memory_domain;
    
    // Hierarchy
    quantum_id_t parent_qid;
    quantum_id_t* children;
    uint32_t child_count;
    
    // Scheduling
    scheduling_info_t scheduling;
    struct quantum_context* next_ready;
    struct quantum_context* prev_ready;
    
    // Security
    capability_set_t* capabilities;
    uint32_t security_level;
    
    // Statistics
    quantum_stats_t stats;
    
    // ABI-specific data
    void* abi_context;
} quantum_context_t;

// CPU core structure
typedef struct {
    uint32_t core_id;
    core_state_t state;
    quantum_context_t* current_quantum;
    uint64_t idle_ticks;
    uint64_t quantum_switches;
    void* tss;  // Task State Segment
} cpu_core_t;

// Quantum registry
typedef struct {
    quantum_context_t* quanta[MAX_QUANTA];
    uint32_t count;
    uint64_t next_qid;
} quantum_registry_t;

// Kernel state
typedef struct {
    uint64_t magic;
    uint32_t version;
    kernel_state_t state;
    uint64_t boot_time;
    uint64_t quantum_count;
    uint64_t next_qid;
} continuum_state_t;

// Boot context from Genesis
typedef struct genesis_boot_context {
    uint64_t magic;
    uint32_t version;
    uint32_t boot_mode;
    struct {
        uint64_t total_memory;
        uint64_t usable_memory;
    } memory_map;
    // Simplified for header
} genesis_boot_context_t;

// IDT structures for interrupt handling
typedef struct {
    uint16_t offset_low;
    uint16_t selector;
    uint8_t ist;
    uint8_t type_attr;
    uint16_t offset_mid;
    uint32_t offset_high;
    uint32_t zero;
} __attribute__((packed)) idt_entry_t;

typedef struct {
    uint16_t limit;
    uint64_t base;
} __attribute__((packed)) idt_ptr_t;

// =============================================================================
// Function Prototypes
// =============================================================================

// Main entry point
void continuum_main(genesis_boot_context_t* boot_context);

// Quantum management
quantum_id_t continuum_create_quantum(abi_mode_t abi_mode, void* entry_point,
                                      const char* name);
int continuum_terminate_quantum(quantum_id_t qid);
quantum_context_t* continuum_get_quantum(quantum_id_t qid);
quantum_context_t* continuum_get_current_quantum(void);

// System request handling
int64_t continuum_handle_request(quantum_context_t* quantum,
                                 system_request_t* request);

// Time management
uint64_t continuum_get_time(void);
uint64_t continuum_get_uptime(void);

// Panic handler
void continuum_panic(const char* message) __attribute__((noreturn));

// Capability management
capability_set_t* capability_create_default(void);
bool capability_check(capability_set_t* caps, uint64_t capability);
void capability_grant(capability_set_t* caps, uint64_t capability);
void capability_revoke(capability_set_t* caps, uint64_t capability);

// ABI-specific handlers (implemented in respective modules)
int64_t handle_native_request(quantum_context_t* quantum, system_request_t* request);
int64_t handle_axon_request(quantum_context_t* quantum, system_request_t* request);
int64_t handle_vortex_request(quantum_context_t* quantum, system_request_t* request);
int64_t handle_cipher_request(quantum_context_t* quantum, system_request_t* request);

// Utility functions
int strncpy(char* dest, const char* src, size_t n);
int snprintf(char* str, size_t size, const char* format, ...);

#endif /* CONTINUUM_CORE_H */
