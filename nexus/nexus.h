/*
 * Nexus System Services Manager
 * Init system and service orchestration for Limitless OS
 */

#ifndef NEXUS_H
#define NEXUS_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

// =============================================================================
// Service Constants
// =============================================================================

#define NEXUS_MAX_SERVICES      256
#define NEXUS_MAX_DEPENDENCIES  16
#define NEXUS_MAX_NAME_LEN      64
#define NEXUS_MAX_PATH_LEN      256
#define NEXUS_MAX_ARGS          32
#define NEXUS_MAX_ENV_VARS      64

// Service states
#define SERVICE_STATE_STOPPED    0x00
#define SERVICE_STATE_STARTING   0x01
#define SERVICE_STATE_RUNNING    0x02
#define SERVICE_STATE_STOPPING   0x03
#define SERVICE_STATE_FAILED     0x04
#define SERVICE_STATE_DISABLED   0x05
#define SERVICE_STATE_WAITING    0x06

// Service types
#define SERVICE_TYPE_DAEMON      0x01
#define SERVICE_TYPE_ONESHOT     0x02
#define SERVICE_TYPE_NOTIFY      0x03
#define SERVICE_TYPE_IDLE        0x04
#define SERVICE_TYPE_BOOT        0x05

// Service flags
#define SERVICE_FLAG_ESSENTIAL   0x01
#define SERVICE_FLAG_RESTART     0x02
#define SERVICE_FLAG_SINGLETON   0x04
#define SERVICE_FLAG_NETWORK     0x08
#define SERVICE_FLAG_FILESYSTEM  0x10
#define SERVICE_FLAG_GRAPHICS    0x20

// Runlevel definitions
#define RUNLEVEL_HALT           0
#define RUNLEVEL_SINGLE         1
#define RUNLEVEL_MULTI_USER     3
#define RUNLEVEL_GRAPHICAL      5
#define RUNLEVEL_REBOOT         6

// =============================================================================
// Data Structures
// =============================================================================

// Service dependency
typedef struct {
    char name[NEXUS_MAX_NAME_LEN];
    bool required;
    bool before;  // This service must start before the dependency
} service_dependency_t;

// Environment variable
typedef struct {
    char name[64];
    char value[256];
} env_var_t;

// Resource limits
typedef struct {
    uint64_t memory_limit;      // Max memory in bytes
    uint64_t cpu_limit;         // CPU percentage (0-100)
    uint32_t max_files;         // Max open files
    uint32_t max_threads;       // Max threads
    uint32_t io_priority;       // I/O priority (0-7)
    uint32_t nice_level;        // Nice level (-20 to 19)
} resource_limits_t;

// Service definition
typedef struct nexus_service {
    // Identification
    char name[NEXUS_MAX_NAME_LEN];
    char description[256];
    uint32_t id;
    
    // Execution
    char exec_path[NEXUS_MAX_PATH_LEN];
    char* args[NEXUS_MAX_ARGS];
    uint32_t arg_count;
    env_var_t env_vars[NEXUS_MAX_ENV_VARS];
    uint32_t env_count;
    char working_dir[NEXUS_MAX_PATH_LEN];
    uint32_t user_id;
    uint32_t group_id;
    
    // Configuration
    uint8_t type;
    uint32_t flags;
    uint8_t runlevel;
    uint32_t start_timeout;     // Seconds
    uint32_t stop_timeout;      // Seconds
    uint32_t restart_delay;     // Seconds
    uint32_t max_restarts;
    
    // Dependencies
    service_dependency_t dependencies[NEXUS_MAX_DEPENDENCIES];
    uint32_t dependency_count;
    
    // Resource limits
    resource_limits_t limits;
    
    // Runtime state
    uint8_t state;
    pid_t pid;
    uint64_t start_time;
    uint64_t stop_time;
    uint32_t restart_count;
    int exit_code;
    
    // Socket activation
    int* listen_fds;
    uint32_t listen_fd_count;
    
    // Health check
    void (*health_check)(struct nexus_service* service);
    uint32_t health_check_interval;
    uint64_t last_health_check;
    bool healthy;
    
    // Callbacks
    void (*on_start)(struct nexus_service* service);
    void (*on_stop)(struct nexus_service* service);
    void (*on_failure)(struct nexus_service* service);
    
    // Logging
    int stdout_fd;
    int stderr_fd;
    char log_file[NEXUS_MAX_PATH_LEN];
    
    struct nexus_service* next;
} nexus_service_t;

// Service manager state
typedef struct {
    nexus_service_t* services;
    uint32_t service_count;
    uint8_t current_runlevel;
    uint8_t target_runlevel;
    bool shutdown_requested;
    
    // Event queue
    struct {
        uint32_t type;
        nexus_service_t* service;
        void* data;
    } event_queue[256];
    uint32_t event_head;
    uint32_t event_tail;
    
    // Statistics
    uint64_t services_started;
    uint64_t services_stopped;
    uint64_t services_failed;
    uint64_t total_restarts;
} nexus_manager_t;

// Service configuration
typedef struct {
    char name[NEXUS_MAX_NAME_LEN];
    char exec[NEXUS_MAX_PATH_LEN];
    char* dependencies[NEXUS_MAX_DEPENDENCIES];
    uint8_t type;
    uint32_t flags;
    uint8_t runlevel;
    resource_limits_t limits;
} service_config_t;

// =============================================================================
// Function Prototypes
// =============================================================================

// Initialization
int nexus_init(void);
void nexus_shutdown(void);

// Service management
nexus_service_t* nexus_create_service(const char* name, const char* exec_path);
int nexus_register_service(nexus_service_t* service);
int nexus_unregister_service(const char* name);
nexus_service_t* nexus_find_service(const char* name);

// Service control
int nexus_start_service(const char* name);
int nexus_stop_service(const char* name);
int nexus_restart_service(const char* name);
int nexus_reload_service(const char* name);
int nexus_enable_service(const char* name);
int nexus_disable_service(const char* name);

// Service lifecycle
int nexus_spawn_service(nexus_service_t* service);
int nexus_terminate_service(nexus_service_t* service);
int nexus_monitor_service(nexus_service_t* service);
void nexus_handle_service_exit(pid_t pid, int exit_code);

// Dependencies
int nexus_add_dependency(nexus_service_t* service, const char* dependency,
                        bool required, bool before);
int nexus_resolve_dependencies(nexus_service_t* service);
bool nexus_check_dependencies(nexus_service_t* service);
void nexus_build_dependency_graph(void);

// Runlevel management
int nexus_change_runlevel(uint8_t runlevel);
uint8_t nexus_get_runlevel(void);
int nexus_start_runlevel_services(uint8_t runlevel);
int nexus_stop_runlevel_services(uint8_t runlevel);

// Configuration
int nexus_load_config(const char* config_file);
int nexus_save_config(const char* config_file);
int nexus_parse_service_file(const char* file_path, service_config_t* config);

// Socket activation
int nexus_add_socket(nexus_service_t* service, const char* address, uint16_t port);
int nexus_activate_socket(nexus_service_t* service, int fd);
void nexus_handle_socket_connection(int fd);

// Health monitoring
void nexus_check_service_health(nexus_service_t* service);
void nexus_monitor_all_services(void);
void nexus_handle_unhealthy_service(nexus_service_t* service);

// Logging
void nexus_log(nexus_service_t* service, const char* format, ...);
void nexus_redirect_output(nexus_service_t* service);
void nexus_rotate_logs(nexus_service_t* service);

// Events
void nexus_emit_event(uint32_t type, nexus_service_t* service, void* data);
void nexus_process_events(void);

// Statistics
void nexus_get_statistics(nexus_manager_t* stats);
void nexus_print_status(void);

// Helper functions
const char* nexus_state_to_string(uint8_t state);
const char* nexus_type_to_string(uint8_t type);
int nexus_wait_for_service(const char* name, uint32_t timeout);

#endif /* NEXUS_H */
