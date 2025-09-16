/*
 * Infinity Package Manager
 * Modern package management for Limitless OS
 */

#ifndef INFINITY_H
#define INFINITY_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <time.h>

// =============================================================================
// Package Constants
// =============================================================================

#define INFINITY_MAX_NAME_LEN       256
#define INFINITY_MAX_VERSION_LEN    64
#define INFINITY_MAX_DEPS           128
#define INFINITY_MAX_FILES          65536
#define INFINITY_MAX_REPOS          32
#define INFINITY_CACHE_DIR          "/var/cache/infinity"
#define INFINITY_DB_DIR             "/var/lib/infinity"
#define INFINITY_CONFIG_DIR         "/etc/infinity"

// Package states
#define PKG_STATE_AVAILABLE         0x01
#define PKG_STATE_INSTALLED         0x02
#define PKG_STATE_UPGRADABLE        0x04
#define PKG_STATE_BROKEN            0x08
#define PKG_STATE_HELD              0x10
#define PKG_STATE_ORPHANED          0x20
#define PKG_STATE_CONFIGURING       0x40

// Package priorities
#define PKG_PRIORITY_REQUIRED       5
#define PKG_PRIORITY_IMPORTANT      4
#define PKG_PRIORITY_STANDARD       3
#define PKG_PRIORITY_OPTIONAL       2
#define PKG_PRIORITY_EXTRA          1

// Transaction types
#define TRANS_INSTALL               0x01
#define TRANS_UPGRADE               0x02
#define TRANS_REMOVE                0x03
#define TRANS_PURGE                 0x04
#define TRANS_DOWNGRADE             0x05
#define TRANS_REINSTALL             0x06

// Repository types
#define REPO_TYPE_HTTP              0x01
#define REPO_TYPE_HTTPS             0x02
#define REPO_TYPE_FTP               0x03
#define REPO_TYPE_FILE              0x04
#define REPO_TYPE_CDROM             0x05

// =============================================================================
// Data Structures
// =============================================================================

// Version structure
typedef struct {
    uint32_t major;
    uint32_t minor;
    uint32_t patch;
    char pre_release[32];
    char build_metadata[32];
} pkg_version_t;

// Package dependency
typedef struct {
    char name[INFINITY_MAX_NAME_LEN];
    char version_constraint[64];
    bool optional;
    bool build_only;
} pkg_dependency_t;

// Package file entry
typedef struct {
    char path[1024];
    uint32_t mode;
    uint32_t uid;
    uint32_t gid;
    uint64_t size;
    uint8_t hash[32];  // SHA-256
    bool is_config;
} pkg_file_t;

// Package metadata
typedef struct {
    char name[INFINITY_MAX_NAME_LEN];
    pkg_version_t version;
    char description[1024];
    char long_description[4096];
    char maintainer[256];
    char homepage[512];
    char license[128];
    char architecture[32];
    
    uint64_t installed_size;
    uint64_t download_size;
    
    uint8_t priority;
    char section[64];
    
    // Dependencies
    pkg_dependency_t* depends;
    uint32_t depend_count;
    pkg_dependency_t* recommends;
    uint32_t recommend_count;
    pkg_dependency_t* suggests;
    uint32_t suggest_count;
    pkg_dependency_t* conflicts;
    uint32_t conflict_count;
    pkg_dependency_t* provides;
    uint32_t provide_count;
    pkg_dependency_t* replaces;
    uint32_t replace_count;
    
    // Build dependencies
    pkg_dependency_t* build_depends;
    uint32_t build_depend_count;
    
    // Files
    pkg_file_t* files;
    uint32_t file_count;
    
    // Installation info
    time_t install_date;
    bool auto_installed;
    char install_reason[256];
} pkg_metadata_t;

// Package structure
typedef struct package {
    pkg_metadata_t metadata;
    uint8_t state;
    
    // Archive info
    char archive_path[1024];
    uint8_t archive_hash[32];
    
    // Repository info
    char repo_name[128];
    char repo_url[512];
    
    // Cache
    void* cached_data;
    size_t cached_size;
    
    struct package* next;
} package_t;

// Repository
typedef struct repository {
    char name[128];
    char url[512];
    uint8_t type;
    bool enabled;
    uint32_t priority;
    
    // Authentication
    char username[128];
    char password[128];
    char gpg_key[2048];
    
    // Package list
    package_t* packages;
    uint32_t package_count;
    time_t last_update;
    
    // Mirror list
    char* mirrors[16];
    uint32_t mirror_count;
    uint32_t current_mirror;
    
    struct repository* next;
} repository_t;

// Transaction
typedef struct {
    uint32_t id;
    uint8_t type;
    time_t start_time;
    time_t end_time;
    
    // Packages involved
    package_t** packages;
    uint32_t package_count;
    
    // Operations
    struct {
        package_t* package;
        uint8_t action;
        bool completed;
    } operations[1024];
    uint32_t operation_count;
    
    // Rollback info
    void* rollback_data;
    
    // Status
    bool in_progress;
    bool successful;
    char error_message[1024];
} transaction_t;

// Download job
typedef struct download_job {
    char url[1024];
    char dest_path[1024];
    uint64_t size;
    uint64_t downloaded;
    uint8_t expected_hash[32];
    
    // Progress
    float progress;
    uint32_t speed;  // Bytes per second
    time_t eta;
    
    // Status
    bool active;
    bool completed;
    bool failed;
    char error[256];
    
    // Callbacks
    void (*progress_callback)(struct download_job* job);
    void (*completion_callback)(struct download_job* job);
    
    struct download_job* next;
} download_job_t;

// Solver state (for dependency resolution)
typedef struct {
    package_t** install_queue;
    uint32_t install_count;
    
    package_t** remove_queue;
    uint32_t remove_count;
    
    package_t** upgrade_queue;
    uint32_t upgrade_count;
    
    // Conflict resolution
    struct {
        package_t* pkg1;
        package_t* pkg2;
        char reason[256];
    } conflicts[256];
    uint32_t conflict_count;
    
    // Solutions
    struct {
        package_t** packages;
        uint32_t count;
        int score;
    } solutions[16];
    uint32_t solution_count;
} solver_state_t;

// Package manager state
typedef struct {
    repository_t* repositories;
    uint32_t repo_count;
    
    package_t* installed_packages;
    uint32_t installed_count;
    
    package_t* available_packages;
    uint32_t available_count;
    
    transaction_t* current_transaction;
    transaction_t* transaction_history;
    
    download_job_t* download_queue;
    uint32_t active_downloads;
    uint32_t max_downloads;
    
    // Configuration
    char cache_dir[256];
    char db_dir[256];
    char config_dir[256];
    uint64_t cache_limit;
    bool auto_update;
    bool auto_remove;
    bool install_recommends;
    bool install_suggests;
    
    // Statistics
    uint64_t total_installed;
    uint64_t total_removed;
    uint64_t total_upgraded;
    uint64_t total_downloaded;
    uint64_t cache_size;
} infinity_state_t;

// =============================================================================
// Function Prototypes
// =============================================================================

// Initialization
int infinity_init(void);
void infinity_shutdown(void);

// Repository management
int infinity_add_repository(const char* name, const char* url, uint8_t type);
int infinity_remove_repository(const char* name);
int infinity_enable_repository(const char* name, bool enable);
int infinity_update_repositories(void);
int infinity_refresh_package_lists(void);
repository_t* infinity_find_repository(const char* name);

// Package queries
package_t* infinity_find_package(const char* name);
package_t* infinity_find_installed(const char* name);
package_t* infinity_find_available(const char* name, const char* version);
package_t** infinity_search_packages(const char* query, uint32_t* count);
package_t** infinity_list_installed(uint32_t* count);
package_t** infinity_list_upgradable(uint32_t* count);
package_t** infinity_list_orphaned(uint32_t* count);

// Package information
int infinity_get_package_info(package_t* pkg, pkg_metadata_t* info);
int infinity_get_package_files(package_t* pkg, pkg_file_t** files, uint32_t* count);
int infinity_get_package_dependencies(package_t* pkg, pkg_dependency_t** deps, 
                                     uint32_t* count);
char* infinity_get_package_changelog(package_t* pkg);
bool infinity_is_package_installed(const char* name);
bool infinity_is_package_upgradable(const char* name);

// Package operations
int infinity_install_package(const char* name);
int infinity_remove_package(const char* name, bool purge);
int infinity_upgrade_package(const char* name);
int infinity_reinstall_package(const char* name);
int infinity_downgrade_package(const char* name, const char* version);
int infinity_hold_package(const char* name, bool hold);

// Batch operations
int infinity_install_packages(char** names, uint32_t count);
int infinity_remove_packages(char** names, uint32_t count, bool purge);
int infinity_upgrade_all(void);
int infinity_autoremove(void);
int infinity_clean_cache(void);

// Transaction management
transaction_t* infinity_begin_transaction(uint8_t type);
int infinity_add_to_transaction(transaction_t* trans, package_t* pkg, uint8_t action);
int infinity_commit_transaction(transaction_t* trans);
int infinity_rollback_transaction(transaction_t* trans);
void infinity_abort_transaction(transaction_t* trans);
transaction_t* infinity_get_current_transaction(void);

// Dependency resolution
int infinity_resolve_dependencies(package_t* pkg, solver_state_t* state);
int infinity_check_conflicts(package_t* pkg);
bool infinity_satisfies_dependency(package_t* pkg, pkg_dependency_t* dep);
package_t* infinity_find_provider(const char* virtual_pkg);
int infinity_calculate_install_order(package_t** packages, uint32_t count,
                                    package_t** ordered);

// Version comparison
int infinity_compare_versions(pkg_version_t* v1, pkg_version_t* v2);
int infinity_parse_version(const char* str, pkg_version_t* version);
bool infinity_version_satisfies(pkg_version_t* version, const char* constraint);

// Package building
int infinity_build_package(const char* spec_file, const char* output_dir);
int infinity_create_package(pkg_metadata_t* metadata, const char* root_dir,
                          const char* output_file);
int infinity_extract_package(const char* package_file, const char* dest_dir);
int infinity_verify_package(const char* package_file);

// Download management
download_job_t* infinity_download_package(package_t* pkg);
int infinity_download_file(const char* url, const char* dest);
void infinity_cancel_download(download_job_t* job);
void infinity_pause_downloads(void);
void infinity_resume_downloads(void);
float infinity_get_download_progress(void);

// Database operations
int infinity_load_database(void);
int infinity_save_database(void);
int infinity_rebuild_database(void);
int infinity_verify_database(void);
int infinity_import_package_list(const char* file);
int infinity_export_package_list(const char* file);

// Configuration
int infinity_load_config(const char* config_file);
int infinity_save_config(const char* config_file);
int infinity_set_option(const char* key, const char* value);
const char* infinity_get_option(const char* key);

// Hooks and plugins
int infinity_register_hook(const char* event, void (*callback)(void*));
int infinity_unregister_hook(const char* event, void (*callback)(void*));
int infinity_trigger_hook(const char* event, void* data);
int infinity_load_plugin(const char* plugin_file);
int infinity_unload_plugin(const char* plugin_name);

// Statistics
void infinity_get_statistics(infinity_state_t* stats);
uint64_t infinity_get_cache_size(void);
uint64_t infinity_get_installed_size(void);

// Helper functions
char* infinity_format_size(uint64_t bytes);
char* infinity_format_time(time_t time);
bool infinity_verify_signature(const void* data, size_t size, const char* signature);
int infinity_compute_hash(const void* data, size_t size, uint8_t* hash);

#endif /* INFINITY_H */
