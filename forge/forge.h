/*
 * Forge Build System
 * Advanced build automation for Limitless OS
 */

#ifndef FORGE_H
#define FORGE_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <time.h>

// =============================================================================
// Build Constants
// =============================================================================

#define FORGE_MAX_NAME_LEN          256
#define FORGE_MAX_PATH_LEN          4096
#define FORGE_MAX_TARGETS           1024
#define FORGE_MAX_DEPENDENCIES      256
#define FORGE_MAX_RULES             512
#define FORGE_MAX_VARIABLES         1024
#define FORGE_MAX_JOBS              64
#define FORGE_MAX_INCLUDES          32

// Build states
#define BUILD_STATE_PENDING         0x01
#define BUILD_STATE_RUNNING         0x02
#define BUILD_STATE_SUCCESS         0x03
#define BUILD_STATE_FAILED          0x04
#define BUILD_STATE_SKIPPED         0x05
#define BUILD_STATE_CACHED          0x06

// Target types
#define TARGET_TYPE_FILE            0x01
#define TARGET_TYPE_PHONY           0x02
#define TARGET_TYPE_PATTERN         0x03
#define TARGET_TYPE_IMPLICIT        0x04
#define TARGET_TYPE_GROUP           0x05

// Build modes
#define BUILD_MODE_DEBUG            0x01
#define BUILD_MODE_RELEASE          0x02
#define BUILD_MODE_PROFILE          0x04
#define BUILD_MODE_SANITIZE         0x08
#define BUILD_MODE_COVERAGE         0x10

// Rule flags
#define RULE_FLAG_SILENT            0x01
#define RULE_FLAG_IGNORE_ERROR      0x02
#define RULE_FLAG_ALWAYS_RUN        0x04
#define RULE_FLAG_RECURSIVE         0x08
#define RULE_FLAG_PARALLEL          0x10

// =============================================================================
// Data Structures
// =============================================================================

// Build variable
typedef struct build_var {
    char name[FORGE_MAX_NAME_LEN];
    char* value;
    bool exported;
    bool override;
    bool append;
    
    // Variable expansion
    bool lazy_eval;
    char* raw_value;
    
    struct build_var* next;
} build_var_t;

// Build dependency
typedef struct build_dep {
    char name[FORGE_MAX_PATH_LEN];
    time_t mtime;
    bool exists;
    bool is_target;
    
    struct build_dep* next;
} build_dep_t;

// Build command
typedef struct build_cmd {
    char* command;
    uint32_t flags;
    
    // Command variables
    build_var_t* local_vars;
    
    struct build_cmd* next;
} build_cmd_t;

// Build rule
typedef struct build_rule {
    char pattern[FORGE_MAX_PATH_LEN];
    char target_pattern[FORGE_MAX_PATH_LEN];
    
    build_dep_t* dependencies;
    build_cmd_t* commands;
    
    uint32_t flags;
    uint8_t priority;
    
    struct build_rule* next;
} build_rule_t;

// Build target
typedef struct build_target {
    char name[FORGE_MAX_PATH_LEN];
    uint8_t type;
    uint8_t state;
    
    // Dependencies
    build_dep_t* dependencies;
    uint32_t dep_count;
    
    // Commands
    build_cmd_t* commands;
    uint32_t cmd_count;
    
    // Build info
    time_t mtime;
    time_t build_time;
    bool needs_rebuild;
    bool is_default;
    
    // Parent targets
    struct build_target** parents;
    uint32_t parent_count;
    
    // Build statistics
    uint64_t build_duration;
    uint32_t build_count;
    uint32_t failure_count;
    
    struct build_target* next;
} build_target_t;

// Build job
typedef struct build_job {
    uint32_t id;
    build_target_t* target;
    build_cmd_t* current_cmd;
    
    // Process info
    pid_t pid;
    int stdout_fd;
    int stderr_fd;
    
    // Status
    uint8_t state;
    int exit_code;
    time_t start_time;
    time_t end_time;
    
    // Output capture
    char* stdout_buffer;
    size_t stdout_size;
    char* stderr_buffer;
    size_t stderr_size;
    
    struct build_job* next;
} build_job_t;

// Toolchain configuration
typedef struct {
    // Compilers
    char cc[FORGE_MAX_PATH_LEN];
    char cxx[FORGE_MAX_PATH_LEN];
    char as[FORGE_MAX_PATH_LEN];
    char ld[FORGE_MAX_PATH_LEN];
    char ar[FORGE_MAX_PATH_LEN];
    char ranlib[FORGE_MAX_PATH_LEN];
    char strip[FORGE_MAX_PATH_LEN];
    char objcopy[FORGE_MAX_PATH_LEN];
    char objdump[FORGE_MAX_PATH_LEN];
    
    // Flags
    char* cflags;
    char* cxxflags;
    char* ldflags;
    char* asflags;
    char* arflags;
    
    // Paths
    char* include_paths;
    char* library_paths;
    
    // Target architecture
    char arch[64];
    char target_triple[128];
    char sysroot[FORGE_MAX_PATH_LEN];
} toolchain_t;

// Build cache entry
typedef struct cache_entry {
    char path[FORGE_MAX_PATH_LEN];
    uint8_t hash[32];  // SHA-256
    time_t mtime;
    uint64_t size;
    
    // Compilation info
    char* command;
    char* flags;
    
    // Cached output
    void* object_data;
    size_t object_size;
    
    // Dependencies
    char** dependencies;
    uint32_t dep_count;
    
    struct cache_entry* next;
} cache_entry_t;

// Build graph
typedef struct {
    build_target_t* targets;
    uint32_t target_count;
    
    build_rule_t* rules;
    uint32_t rule_count;
    
    build_var_t* variables;
    uint32_t var_count;
    
    // Dependency graph
    void* dep_graph;
    
    // Build order
    build_target_t** build_order;
    uint32_t build_order_count;
} build_graph_t;

// Build context
typedef struct {
    // Configuration
    char build_file[FORGE_MAX_PATH_LEN];
    char build_dir[FORGE_MAX_PATH_LEN];
    char source_dir[FORGE_MAX_PATH_LEN];
    char install_dir[FORGE_MAX_PATH_LEN];
    
    uint8_t build_mode;
    uint32_t max_jobs;
    bool verbose;
    bool keep_going;
    bool dry_run;
    bool force_rebuild;
    
    // Build graph
    build_graph_t graph;
    
    // Active jobs
    build_job_t* jobs;
    uint32_t active_jobs;
    
    // Toolchain
    toolchain_t toolchain;
    
    // Cache
    cache_entry_t* cache;
    bool use_cache;
    char cache_dir[FORGE_MAX_PATH_LEN];
    
    // Statistics
    uint32_t targets_built;
    uint32_t targets_failed;
    uint32_t targets_skipped;
    uint64_t total_build_time;
    
    // Include paths
    char* include_paths[FORGE_MAX_INCLUDES];
    uint32_t include_count;
} build_context_t;

// Build profile
typedef struct {
    char name[FORGE_MAX_NAME_LEN];
    
    // Timing data
    struct {
        char target[FORGE_MAX_PATH_LEN];
        uint64_t duration;
        uint64_t cpu_time;
        uint64_t memory_peak;
    } timings[FORGE_MAX_TARGETS];
    uint32_t timing_count;
    
    // Hot paths
    build_target_t** critical_path;
    uint32_t critical_path_length;
    
    // Bottlenecks
    build_target_t** bottlenecks;
    uint32_t bottleneck_count;
} build_profile_t;

// =============================================================================
// Function Prototypes
// =============================================================================

// Initialization
int forge_init(void);
void forge_cleanup(void);

// Build file parsing
int forge_parse_file(const char* filename, build_context_t* ctx);
int forge_parse_buffer(const char* buffer, size_t size, build_context_t* ctx);
int forge_include_file(const char* filename, build_context_t* ctx);

// Target management
build_target_t* forge_add_target(build_context_t* ctx, const char* name, uint8_t type);
build_target_t* forge_find_target(build_context_t* ctx, const char* name);
int forge_add_dependency(build_target_t* target, const char* dep);
int forge_add_command(build_target_t* target, const char* cmd, uint32_t flags);
bool forge_target_needs_rebuild(build_target_t* target);

// Rule management
build_rule_t* forge_add_rule(build_context_t* ctx, const char* pattern);
build_rule_t* forge_find_rule(build_context_t* ctx, const char* target);
int forge_apply_rule(build_rule_t* rule, const char* target, build_context_t* ctx);

// Variable management
int forge_set_variable(build_context_t* ctx, const char* name, const char* value);
const char* forge_get_variable(build_context_t* ctx, const char* name);
char* forge_expand_variables(build_context_t* ctx, const char* str);
int forge_export_variable(build_context_t* ctx, const char* name);

// Build execution
int forge_build(build_context_t* ctx, const char* target);
int forge_build_all(build_context_t* ctx);
int forge_clean(build_context_t* ctx);
int forge_install(build_context_t* ctx);

// Job management
build_job_t* forge_create_job(build_target_t* target);
int forge_start_job(build_job_t* job, build_context_t* ctx);
int forge_wait_job(build_job_t* job);
void forge_cancel_job(build_job_t* job);
int forge_run_parallel(build_context_t* ctx);

// Dependency analysis
int forge_analyze_dependencies(build_context_t* ctx);
int forge_build_dependency_graph(build_context_t* ctx);
int forge_topological_sort(build_context_t* ctx);
build_target_t** forge_get_build_order(build_context_t* ctx, uint32_t* count);

// Cache management
int forge_cache_init(build_context_t* ctx);
cache_entry_t* forge_cache_lookup(build_context_t* ctx, const char* path);
int forge_cache_store(build_context_t* ctx, const char* path, const void* data, size_t size);
int forge_cache_invalidate(build_context_t* ctx, const char* path);
int forge_cache_clean(build_context_t* ctx);

// Toolchain detection
int forge_detect_toolchain(toolchain_t* toolchain);
int forge_configure_toolchain(toolchain_t* toolchain, const char* prefix);
int forge_cross_compile_setup(toolchain_t* toolchain, const char* target);

// Built-in functions
int forge_builtin_compile_c(build_context_t* ctx, const char* src, const char* obj);
int forge_builtin_compile_cpp(build_context_t* ctx, const char* src, const char* obj);
int forge_builtin_link_executable(build_context_t* ctx, const char** objs, 
                                  uint32_t count, const char* output);
int forge_builtin_create_library(build_context_t* ctx, const char** objs,
                                uint32_t count, const char* output);
int forge_builtin_generate_deps(build_context_t* ctx, const char* src, const char* deps);

// Pattern matching
bool forge_match_pattern(const char* pattern, const char* str);
char* forge_apply_pattern(const char* pattern, const char* stem, const char* replacement);
char** forge_glob(const char* pattern, uint32_t* count);

// Profiling
int forge_enable_profiling(build_context_t* ctx);
build_profile_t* forge_get_profile(build_context_t* ctx);
int forge_save_profile(build_profile_t* profile, const char* filename);
int forge_analyze_profile(build_profile_t* profile);

// Utilities
char* forge_get_stem(const char* target, const char* pattern);
time_t forge_get_mtime(const char* path);
bool forge_file_exists(const char* path);
int forge_mkdir_p(const char* path);
int forge_copy_file(const char* src, const char* dst);
int forge_remove_file(const char* path);

// Error handling
void forge_error(const char* format, ...);
void forge_warning(const char* format, ...);
void forge_info(const char* format, ...);
void forge_debug(const char* format, ...);

#endif /* FORGE_H */
