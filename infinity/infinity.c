/*
 * Infinity Package Manager
 * Core package management implementation
 */

#include "infinity.h"
#include "archive.h"
#include "solver.h"
#include "../harmony/harmony_net.h"
#include "../manifold/manifold.h"
#include "../continuum/flux_memory.h"
#include <string.h>
#include <stdio.h>

// =============================================================================
// Global State
// =============================================================================

static infinity_state_t g_infinity;
static spinlock_t g_infinity_lock = SPINLOCK_INIT;

// =============================================================================
// Package Operations
// =============================================================================

int infinity_install_package(const char* name) {
    if (!name) {
        return -1;
    }
    
    // Check if already installed
    package_t* installed = infinity_find_installed(name);
    if (installed) {
        printf("Package '%s' is already installed\n", name);
        return 0;
    }
    
    // Find package in repositories
    package_t* pkg = infinity_find_available(name, NULL);
    if (!pkg) {
        printf("Package '%s' not found in repositories\n", name);
        return -1;
    }
    
    // Begin transaction
    transaction_t* trans = infinity_begin_transaction(TRANS_INSTALL);
    if (!trans) {
        return -1;
    }
    
    // Resolve dependencies
    solver_state_t solver = {0};
    if (infinity_resolve_dependencies(pkg, &solver) != 0) {
        printf("Failed to resolve dependencies for '%s'\n", name);
        infinity_abort_transaction(trans);
        return -1;
    }
    
    // Add all packages to transaction
    for (uint32_t i = 0; i < solver.install_count; i++) {
        infinity_add_to_transaction(trans, solver.install_queue[i], TRANS_INSTALL);
    }
    
    // Check for conflicts
    for (uint32_t i = 0; i < solver.install_count; i++) {
        if (infinity_check_conflicts(solver.install_queue[i]) != 0) {
            printf("Conflicts detected for package '%s'\n", 
                   solver.install_queue[i]->metadata.name);
            infinity_abort_transaction(trans);
            return -1;
        }
    }
    
    // Calculate required space
    uint64_t required_space = 0;
    for (uint32_t i = 0; i < trans->operation_count; i++) {
        required_space += trans->operations[i].package->metadata.installed_size;
    }
    
    // Check available space
    if (!infinity_check_disk_space(required_space)) {
        printf("Insufficient disk space. Required: %s\n", 
               infinity_format_size(required_space));
        infinity_abort_transaction(trans);
        return -1;
    }
    
    // Download packages
    printf("Downloading packages...\n");
    for (uint32_t i = 0; i < trans->operation_count; i++) {
        package_t* p = trans->operations[i].package;
        
        // Check cache first
        if (!infinity_is_cached(p)) {
            download_job_t* job = infinity_download_package(p);
            if (!job || job->failed) {
                printf("Failed to download package '%s'\n", p->metadata.name);
                infinity_abort_transaction(trans);
                return -1;
            }
        }
    }
    
    // Commit transaction
    printf("Installing packages...\n");
    if (infinity_commit_transaction(trans) != 0) {
        printf("Installation failed\n");
        infinity_rollback_transaction(trans);
        return -1;
    }
    
    printf("Successfully installed '%s'\n", name);
    return 0;
}

int infinity_remove_package(const char* name, bool purge) {
    if (!name) {
        return -1;
    }
    
    // Find installed package
    package_t* pkg = infinity_find_installed(name);
    if (!pkg) {
        printf("Package '%s' is not installed\n", name);
        return -1;
    }
    
    // Check if required by other packages
    package_t** dependents = infinity_find_dependents(pkg);
    if (dependents && dependents[0]) {
        printf("Package '%s' is required by:\n", name);
        for (int i = 0; dependents[i]; i++) {
            printf("  - %s\n", dependents[i]->metadata.name);
        }
        printf("Cannot remove package\n");
        return -1;
    }
    
    // Begin transaction
    transaction_t* trans = infinity_begin_transaction(purge ? TRANS_PURGE : TRANS_REMOVE);
    if (!trans) {
        return -1;
    }
    
    // Add package to transaction
    infinity_add_to_transaction(trans, pkg, purge ? TRANS_PURGE : TRANS_REMOVE);
    
    // Find orphaned packages
    if (g_infinity.auto_remove) {
        package_t** orphaned = infinity_find_orphaned_after_remove(pkg);
        if (orphaned) {
            printf("The following packages are no longer needed:\n");
            for (int i = 0; orphaned[i]; i++) {
                printf("  - %s\n", orphaned[i]->metadata.name);
                infinity_add_to_transaction(trans, orphaned[i], TRANS_REMOVE);
            }
        }
    }
    
    // Commit transaction
    printf("Removing packages...\n");
    if (infinity_commit_transaction(trans) != 0) {
        printf("Removal failed\n");
        infinity_rollback_transaction(trans);
        return -1;
    }
    
    printf("Successfully removed '%s'\n", name);
    return 0;
}

// =============================================================================
// Transaction Management
// =============================================================================

transaction_t* infinity_begin_transaction(uint8_t type) {
    transaction_t* trans = flux_allocate(NULL, sizeof(transaction_t),
                                        FLUX_ALLOC_KERNEL | FLUX_ALLOC_ZERO);
    if (!trans) {
        return NULL;
    }
    
    static uint32_t next_id = 1;
    trans->id = next_id++;
    trans->type = type;
    trans->start_time = time(NULL);
    trans->in_progress = true;
    
    // Save current state for rollback
    trans->rollback_data = infinity_save_state();
    
    spinlock_acquire(&g_infinity_lock);
    g_infinity.current_transaction = trans;
    spinlock_release(&g_infinity_lock);
    
    return trans;
}

int infinity_commit_transaction(transaction_t* trans) {
    if (!trans || !trans->in_progress) {
        return -1;
    }
    
    // Execute operations in order
    for (uint32_t i = 0; i < trans->operation_count; i++) {
        package_t* pkg = trans->operations[i].package;
        uint8_t action = trans->operations[i].action;
        
        int result = 0;
        switch (action) {
            case TRANS_INSTALL:
                result = infinity_do_install(pkg);
                break;
                
            case TRANS_UPGRADE:
                result = infinity_do_upgrade(pkg);
                break;
                
            case TRANS_REMOVE:
                result = infinity_do_remove(pkg, false);
                break;
                
            case TRANS_PURGE:
                result = infinity_do_remove(pkg, true);
                break;
        }
        
        if (result != 0) {
            trans->successful = false;
            snprintf(trans->error_message, sizeof(trans->error_message),
                    "Failed to %s package '%s'",
                    infinity_action_to_string(action), pkg->metadata.name);
            return -1;
        }
        
        trans->operations[i].completed = true;
    }
    
    // Update database
    infinity_save_database();
    
    // Clean up
    trans->end_time = time(NULL);
    trans->in_progress = false;
    trans->successful = true;
    
    // Add to history
    trans->next = g_infinity.transaction_history;
    g_infinity.transaction_history = trans;
    
    spinlock_acquire(&g_infinity_lock);
    g_infinity.current_transaction = NULL;
    spinlock_release(&g_infinity_lock);
    
    // Trigger post-transaction hooks
    infinity_trigger_hook("post-transaction", trans);
    
    return 0;
}

// =============================================================================
// Package Installation
// =============================================================================

static int infinity_do_install(package_t* pkg) {
    printf("Installing %s (%s)...\n", 
           pkg->metadata.name, 
           infinity_version_to_string(&pkg->metadata.version));
    
    // Extract package
    char extract_dir[1024];
    snprintf(extract_dir, sizeof(extract_dir), "/tmp/infinity.%s.XXXXXX", 
             pkg->metadata.name);
    
    if (mkdtemp(extract_dir) == NULL) {
        return -1;
    }
    
    if (infinity_extract_package(pkg->archive_path, extract_dir) != 0) {
        rmdir(extract_dir);
        return -1;
    }
    
    // Run pre-install script
    char preinst[1024];
    snprintf(preinst, sizeof(preinst), "%s/DEBIAN/preinst", extract_dir);
    if (manifold_stat(preinst, NULL) == 0) {
        if (system(preinst) != 0) {
            printf("Pre-installation script failed\n");
            infinity_cleanup_temp_dir(extract_dir);
            return -1;
        }
    }
    
    // Install files
    for (uint32_t i = 0; i < pkg->metadata.file_count; i++) {
        pkg_file_t* file = &pkg->metadata.files[i];
        
        char src_path[2048];
        snprintf(src_path, sizeof(src_path), "%s%s", extract_dir, file->path);
        
        // Create parent directories
        char* parent = infinity_dirname(file->path);
        manifold_mkdir_p(parent, 0755);
        
        // Copy file
        if (file->is_config) {
            // Handle configuration files specially
            if (manifold_stat(file->path, NULL) == 0) {
                // Config file exists - back it up
                char backup[1024];
                snprintf(backup, sizeof(backup), "%s.old", file->path);
                manifold_rename(file->path, backup);
            }
        }
        
        if (infinity_copy_file(src_path, file->path) != 0) {
            printf("Failed to install file: %s\n", file->path);
            infinity_cleanup_temp_dir(extract_dir);
            return -1;
        }
        
        // Set permissions
        manifold_chmod(file->path, file->mode);
        manifold_chown(file->path, file->uid, file->gid);
    }
    
    // Run post-install script
    char postinst[1024];
    snprintf(postinst, sizeof(postinst), "%s/DEBIAN/postinst", extract_dir);
    if (manifold_stat(postinst, NULL) == 0) {
        if (system(postinst) != 0) {
            printf("Post-installation script failed\n");
        }
    }
    
    // Update package state
    pkg->state = PKG_STATE_INSTALLED;
    pkg->metadata.install_date = time(NULL);
    
    // Add to installed packages
    spinlock_acquire(&g_infinity_lock);
    pkg->next = g_infinity.installed_packages;
    g_infinity.installed_packages = pkg;
    g_infinity.installed_count++;
    g_infinity.total_installed++;
    spinlock_release(&g_infinity_lock);
    
    // Clean up
    infinity_cleanup_temp_dir(extract_dir);
    
    // Trigger post-install hook
    infinity_trigger_hook("post-install", pkg);
    
    return 0;
}

// =============================================================================
// Dependency Resolution
// =============================================================================

int infinity_resolve_dependencies(package_t* pkg, solver_state_t* state) {
    if (!pkg || !state) {
        return -1;
    }
    
    // Add package to install queue
    infinity_solver_add_install(state, pkg);
    
    // Process each dependency
    for (uint32_t i = 0; i < pkg->metadata.depend_count; i++) {
        pkg_dependency_t* dep = &pkg->metadata.depends[i];
        
        // Check if dependency is already satisfied
        package_t* installed = infinity_find_installed(dep->name);
        if (installed) {
            if (infinity_version_satisfies(&installed->metadata.version,
                                          dep->version_constraint)) {
                continue;  // Dependency satisfied
            }
            
            // Need to upgrade
            package_t* newer = infinity_find_available(dep->name,
                                                      dep->version_constraint);
            if (!newer) {
                printf("Cannot satisfy dependency: %s %s\n",
                       dep->name, dep->version_constraint);
                return -1;
            }
            
            infinity_solver_add_upgrade(state, newer);
            
            // Recursively resolve dependencies
            if (infinity_resolve_dependencies(newer, state) != 0) {
                return -1;
            }
        } else {
            // Need to install
            package_t* available = infinity_find_available(dep->name,
                                                          dep->version_constraint);
            if (!available) {
                if (dep->optional) {
                    continue;  // Skip optional dependency
                }
                
                // Try to find virtual package provider
                available = infinity_find_provider(dep->name);
                if (!available) {
                    printf("Cannot find package: %s\n", dep->name);
                    return -1;
                }
            }
            
            // Check if already in queue
            bool in_queue = false;
            for (uint32_t j = 0; j < state->install_count; j++) {
                if (state->install_queue[j] == available) {
                    in_queue = true;
                    break;
                }
            }
            
            if (!in_queue) {
                infinity_solver_add_install(state, available);
                
                // Recursively resolve dependencies
                if (infinity_resolve_dependencies(available, state) != 0) {
                    return -1;
                }
            }
        }
    }
    
    // Process recommendations if enabled
    if (g_infinity.install_recommends) {
        for (uint32_t i = 0; i < pkg->metadata.recommend_count; i++) {
            pkg_dependency_t* rec = &pkg->metadata.recommends[i];
            
            package_t* available = infinity_find_available(rec->name, NULL);
            if (available && !infinity_is_package_installed(rec->name)) {
                infinity_solver_add_install(state, available);
            }
        }
    }
    
    return 0;
}

// =============================================================================
// Repository Management
// =============================================================================

int infinity_update_repositories(void) {
    printf("Updating package lists...\n");
    
    repository_t* repo = g_infinity.repositories;
    int updated = 0;
    
    while (repo) {
        if (repo->enabled) {
            printf("Updating %s...\n", repo->name);
            
            // Download package list
            char url[1024];
            char dest[1024];
            
            snprintf(url, sizeof(url), "%s/Packages.gz", repo->url);
            snprintf(dest, sizeof(dest), "%s/%s.packages.gz",
                    g_infinity.cache_dir, repo->name);
            
            if (infinity_download_file(url, dest) == 0) {
                // Decompress and parse package list
                if (infinity_parse_package_list(dest, repo) == 0) {
                    repo->last_update = time(NULL);
                    updated++;
                } else {
                    printf("Failed to parse package list for %s\n", repo->name);
                }
            } else {
                printf("Failed to download package list for %s\n", repo->name);
            }
        }
        repo = repo->next;
    }
    
    // Rebuild package cache
    infinity_rebuild_package_cache();
    
    printf("Updated %d repositories\n", updated);
    return (updated > 0) ? 0 : -1;
}

// =============================================================================
// Initialization
// =============================================================================

int infinity_init(void) {
    memset(&g_infinity, 0, sizeof(g_infinity));
    
    // Set default paths
    strncpy(g_infinity.cache_dir, INFINITY_CACHE_DIR, sizeof(g_infinity.cache_dir) - 1);
    strncpy(g_infinity.db_dir, INFINITY_DB_DIR, sizeof(g_infinity.db_dir) - 1);
    strncpy(g_infinity.config_dir, INFINITY_CONFIG_DIR, sizeof(g_infinity.config_dir) - 1);
    
    // Set default configuration
    g_infinity.cache_limit = 500 * 1024 * 1024;  // 500MB
    g_infinity.auto_update = true;
    g_infinity.auto_remove = false;
    g_infinity.install_recommends = true;
    g_infinity.install_suggests = false;
    g_infinity.max_downloads = 4;
    
    // Create directories
    manifold_mkdir_p(g_infinity.cache_dir, 0755);
    manifold_mkdir_p(g_infinity.db_dir, 0755);
    manifold_mkdir_p(g_infinity.config_dir, 0755);
    
    // Load configuration
    char config_file[512];
    snprintf(config_file, sizeof(config_file), "%s/infinity.conf", g_infinity.config_dir);
    infinity_load_config(config_file);
    
    // Load database
    infinity_load_database();
    
    // Load repository list
    char sources_file[512];
    snprintf(sources_file, sizeof(sources_file), "%s/sources.list", g_infinity.config_dir);
    infinity_load_sources(sources_file);
    
    // Add default repositories if none configured
    if (g_infinity.repo_count == 0) {
        infinity_add_repository("main", "https://packages.limitless.com/main", REPO_TYPE_HTTPS);
        infinity_add_repository("community", "https://packages.limitless.com/community", REPO_TYPE_HTTPS);
        infinity_add_repository("nonfree", "https://packages.limitless.com/nonfree", REPO_TYPE_HTTPS);
    }
    
    // Initialize download manager
    infinity_init_downloader();
    
    // Initialize package solver
    infinity_init_solver();
    
    // Start background daemon
    infinity_start_daemon();
    
    return 0;
}

void infinity_shutdown(void) {
    // Stop daemon
    infinity_stop_daemon();
    
    // Save database
    infinity_save_database();
    
    // Clean up repositories
    repository_t* repo = g_infinity.repositories;
    while (repo) {
        repository_t* next = repo->next;
        infinity_free_repository(repo);
        repo = next;
    }
    
    // Clean up packages
    package_t* pkg = g_infinity.installed_packages;
    while (pkg) {
        package_t* next = pkg->next;
        infinity_free_package(pkg);
        pkg = next;
    }
    
    pkg = g_infinity.available_packages;
    while (pkg) {
        package_t* next = pkg->next;
        infinity_free_package(pkg);
        pkg = next;
    }
    
    // Clean up transactions
    transaction_t* trans = g_infinity.transaction_history;
    while (trans) {
        transaction_t* next = trans->next;
        infinity_free_transaction(trans);
        trans = next;
    }
}
