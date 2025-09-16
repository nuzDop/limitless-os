/*
 * Nexus System Services Manager
 * Core service management implementation
 */

#include "nexus.h"
#include "../continuum/temporal_scheduler.h"
#include "../continuum/flux_memory.h"
#include "../continuum/conduit_ipc.h"
#include <signal.h>
#include <sys/wait.h>

// =============================================================================
// Global State
// =============================================================================

static nexus_manager_t g_manager;
static spinlock_t g_nexus_lock = SPINLOCK_INIT;
static bool g_nexus_running = false;

// =============================================================================
// Service Lifecycle
// =============================================================================

nexus_service_t* nexus_create_service(const char* name, const char* exec_path) {
    nexus_service_t* service = flux_allocate(NULL, sizeof(nexus_service_t),
                                            FLUX_ALLOC_KERNEL | FLUX_ALLOC_ZERO);
    if (!service) {
        return NULL;
    }
    
    strncpy(service->name, name, NEXUS_MAX_NAME_LEN - 1);
    strncpy(service->exec_path, exec_path, NEXUS_MAX_PATH_LEN - 1);
    
    // Set defaults
    service->type = SERVICE_TYPE_DAEMON;
    service->runlevel = RUNLEVEL_MULTI_USER;
    service->start_timeout = 30;
    service->stop_timeout = 30;
    service->restart_delay = 1;
    service->max_restarts = 3;
    service->state = SERVICE_STATE_STOPPED;
    
    // Default resource limits
    service->limits.memory_limit = 512 * 1024 * 1024;  // 512MB
    service->limits.cpu_limit = 100;  // No limit
    service->limits.max_files = 1024;
    service->limits.max_threads = 256;
    service->limits.io_priority = 4;
    service->limits.nice_level = 0;
    
    static uint32_t next_id = 1;
    service->id = next_id++;
    
    return service;
}

int nexus_register_service(nexus_service_t* service) {
    if (!service) {
        return -1;
    }
    
    spinlock_acquire(&g_nexus_lock);
    
    // Check for duplicate
    nexus_service_t* existing = g_manager.services;
    while (existing) {
        if (strcmp(existing->name, service->name) == 0) {
            spinlock_release(&g_nexus_lock);
            return -1;
        }
        existing = existing->next;
    }
    
    // Add to service list
    service->next = g_manager.services;
    g_manager.services = service;
    g_manager.service_count++;
    
    spinlock_release(&g_nexus_lock);
    
    nexus_log(service, "Registered service: %s", service->name);
    
    return 0;
}

int nexus_spawn_service(nexus_service_t* service) {
    if (!service || service->state != SERVICE_STATE_STOPPED) {
        return -1;
    }
    
    service->state = SERVICE_STATE_STARTING;
    
    // Fork process
    pid_t pid = fork();
    if (pid < 0) {
        service->state = SERVICE_STATE_FAILED;
        return -1;
    }
    
    if (pid == 0) {
        // Child process
        
        // Set resource limits
        nexus_apply_resource_limits(&service->limits);
        
        // Change working directory
        if (service->working_dir[0]) {
            chdir(service->working_dir);
        }
        
        // Set user/group
        if (service->group_id) {
            setgid(service->group_id);
        }
        if (service->user_id) {
            setuid(service->user_id);
        }
        
        // Setup environment
        for (uint32_t i = 0; i < service->env_count; i++) {
            setenv(service->env_vars[i].name, service->env_vars[i].value, 1);
        }
        
        // Redirect output
        if (service->log_file[0]) {
            nexus_redirect_output(service);
        }
        
        // Setup socket activation
        if (service->listen_fd_count > 0) {
            nexus_setup_socket_activation(service);
        }
        
        // Execute service
        execve(service->exec_path, service->args, environ);
        
        // If we get here, exec failed
        exit(1);
    }
    
    // Parent process
    service->pid = pid;
    service->start_time = temporal_get_time();
    service->state = SERVICE_STATE_RUNNING;
    
    nexus_log(service, "Started service %s (PID %d)", service->name, pid);
    
    // Call start callback
    if (service->on_start) {
        service->on_start(service);
    }
    
    // Emit start event
    nexus_emit_event(EVENT_SERVICE_STARTED, service, NULL);
    
    g_manager.services_started++;
    
    return 0;
}

int nexus_terminate_service(nexus_service_t* service) {
    if (!service || service->state != SERVICE_STATE_RUNNING) {
        return -1;
    }
    
    service->state = SERVICE_STATE_STOPPING;
    
    nexus_log(service, "Stopping service %s (PID %d)", service->name, service->pid);
    
    // Send SIGTERM
    kill(service->pid, SIGTERM);
    
    // Wait for graceful shutdown
    uint64_t timeout = temporal_get_time() + service->stop_timeout * 1000000;
    while (temporal_get_time() < timeout) {
        int status;
        pid_t result = waitpid(service->pid, &status, WNOHANG);
        
        if (result == service->pid) {
            // Process exited
            service->exit_code = WEXITSTATUS(status);
            service->pid = 0;
            service->state = SERVICE_STATE_STOPPED;
            service->stop_time = temporal_get_time();
            
            // Call stop callback
            if (service->on_stop) {
                service->on_stop(service);
            }
            
            g_manager.services_stopped++;
            return 0;
        }
        
        temporal_sleep(100000);  // 100ms
    }
    
    // Timeout - send SIGKILL
    nexus_log(service, "Service %s didn't stop gracefully, forcing", service->name);
    kill(service->pid, SIGKILL);
    waitpid(service->pid, NULL, 0);
    
    service->pid = 0;
    service->state = SERVICE_STATE_STOPPED;
    service->stop_time = temporal_get_time();
    
    return 0;
}

void nexus_handle_service_exit(pid_t pid, int exit_code) {
    spinlock_acquire(&g_nexus_lock);
    
    // Find service by PID
    nexus_service_t* service = g_manager.services;
    while (service) {
        if (service->pid == pid) {
            break;
        }
        service = service->next;
    }
    
    spinlock_release(&g_nexus_lock);
    
    if (!service) {
        return;
    }
    
    nexus_log(service, "Service %s exited with code %d", service->name, exit_code);
    
    service->pid = 0;
    service->exit_code = exit_code;
    service->stop_time = temporal_get_time();
    
    if (service->state == SERVICE_STATE_STOPPING) {
        service->state = SERVICE_STATE_STOPPED;
    } else {
        // Unexpected exit
        service->state = SERVICE_STATE_FAILED;
        g_manager.services_failed++;
        
        // Call failure callback
        if (service->on_failure) {
            service->on_failure(service);
        }
        
        // Check restart policy
        if (service->flags & SERVICE_FLAG_RESTART) {
            if (service->restart_count < service->max_restarts) {
                nexus_log(service, "Restarting service %s (attempt %d/%d)",
                         service->name, service->restart_count + 1, service->max_restarts);
                
                service->restart_count++;
                g_manager.total_restarts++;
                
                // Wait before restart
                temporal_sleep(service->restart_delay * 1000000);
                
                // Reset state and restart
                service->state = SERVICE_STATE_STOPPED;
                nexus_start_service(service->name);
            } else {
                nexus_log(service, "Service %s exceeded max restarts", service->name);
                
                if (service->flags & SERVICE_FLAG_ESSENTIAL) {
                    // Essential service failed - system panic
                    nexus_log(NULL, "Essential service %s failed!", service->name);
                    nexus_emergency_shutdown();
                }
            }
        }
    }
}

// =============================================================================
// Service Control
// =============================================================================

int nexus_start_service(const char* name) {
    nexus_service_t* service = nexus_find_service(name);
    if (!service) {
        return -1;
    }
    
    if (service->state != SERVICE_STATE_STOPPED) {
        return -1;
    }
    
    // Check dependencies
    if (!nexus_check_dependencies(service)) {
        nexus_log(service, "Dependencies not satisfied for %s", name);
        service->state = SERVICE_STATE_WAITING;
        return -1;
    }
    
    return nexus_spawn_service(service);
}

int nexus_stop_service(const char* name) {
    nexus_service_t* service = nexus_find_service(name);
    if (!service) {
        return -1;
    }
    
    if (service->state != SERVICE_STATE_RUNNING) {
        return -1;
    }
    
    // Check if any services depend on this one
    nexus_service_t* other = g_manager.services;
    while (other) {
        if (other->state == SERVICE_STATE_RUNNING) {
            for (uint32_t i = 0; i < other->dependency_count; i++) {
                if (strcmp(other->dependencies[i].name, name) == 0 &&
                    other->dependencies[i].required) {
                    nexus_log(service, "Cannot stop %s: required by %s",
                             name, other->name);
                    return -1;
                }
            }
        }
        other = other->next;
    }
    
    return nexus_terminate_service(service);
}

int nexus_restart_service(const char* name) {
    int result = nexus_stop_service(name);
    if (result != 0) {
        return result;
    }
    
    // Wait for service to stop
    nexus_wait_for_service_state(name, SERVICE_STATE_STOPPED, 30);
    
    return nexus_start_service(name);
}

// =============================================================================
// Dependency Management
// =============================================================================

int nexus_add_dependency(nexus_service_t* service, const char* dependency,
                        bool required, bool before) {
    if (!service || !dependency) {
        return -1;
    }
    
    if (service->dependency_count >= NEXUS_MAX_DEPENDENCIES) {
        return -1;
    }
    
    service_dependency_t* dep = &service->dependencies[service->dependency_count];
    strncpy(dep->name, dependency, NEXUS_MAX_NAME_LEN - 1);
    dep->required = required;
    dep->before = before;
    
    service->dependency_count++;
    
    return 0;
}

bool nexus_check_dependencies(nexus_service_t* service) {
    for (uint32_t i = 0; i < service->dependency_count; i++) {
        service_dependency_t* dep = &service->dependencies[i];
        
        if (dep->before) {
            continue;  // "Before" dependencies don't block start
        }
        
        nexus_service_t* dep_service = nexus_find_service(dep->name);
        if (!dep_service) {
            if (dep->required) {
                return false;
            }
            continue;
        }
        
        if (dep_service->state != SERVICE_STATE_RUNNING) {
            if (dep->required) {
                return false;
            }
        }
    }
    
    return true;
}

void nexus_build_dependency_graph(void) {
    // Topological sort of services based on dependencies
    nexus_service_t* sorted[NEXUS_MAX_SERVICES];
    uint32_t sorted_count = 0;
    
    bool visited[NEXUS_MAX_SERVICES] = {false};
    bool in_stack[NEXUS_MAX_SERVICES] = {false};
    
    nexus_service_t* service = g_manager.services;
    uint32_t index = 0;
    while (service) {
        if (!visited[index]) {
            nexus_topological_sort_dfs(service, index, visited, in_stack,
                                      sorted, &sorted_count);
        }
        service = service->next;
        index++;
    }
    
    // Rebuild service list in dependency order
    g_manager.services = NULL;
    for (int i = sorted_count - 1; i >= 0; i--) {
        sorted[i]->next = g_manager.services;
        g_manager.services = sorted[i];
    }
}

// =============================================================================
// Runlevel Management
// =============================================================================

int nexus_change_runlevel(uint8_t runlevel) {
    if (runlevel > RUNLEVEL_REBOOT) {
        return -1;
    }
    
    nexus_log(NULL, "Changing runlevel from %d to %d",
             g_manager.current_runlevel, runlevel);
    
    g_manager.target_runlevel = runlevel;
    
    // Stop services not needed in new runlevel
    if (runlevel < g_manager.current_runlevel) {
        nexus_stop_runlevel_services(runlevel);
    }
    
    // Start services for new runlevel
    if (runlevel > g_manager.current_runlevel) {
        nexus_start_runlevel_services(runlevel);
    }
    
    g_manager.current_runlevel = runlevel;
    
    // Handle special runlevels
    switch (runlevel) {
        case RUNLEVEL_HALT:
            nexus_perform_shutdown();
            break;
            
        case RUNLEVEL_REBOOT:
            nexus_perform_reboot();
            break;
            
        case RUNLEVEL_SINGLE:
            nexus_enter_single_user();
            break;
            
        case RUNLEVEL_GRAPHICAL:
            nexus_start_graphical_session();
            break;
    }
    
    return 0;
}

int nexus_start_runlevel_services(uint8_t runlevel) {
    nexus_service_t* service = g_manager.services;
    
    while (service) {
        if (service->runlevel <= runlevel && 
            service->state == SERVICE_STATE_STOPPED &&
            !(service->state & SERVICE_STATE_DISABLED)) {
            nexus_start_service(service->name);
        }
        service = service->next;
    }
    
    return 0;
}

int nexus_stop_runlevel_services(uint8_t runlevel) {
    nexus_service_t* service = g_manager.services;
    
    while (service) {
        if (service->runlevel > runlevel && 
            service->state == SERVICE_STATE_RUNNING) {
            nexus_stop_service(service->name);
        }
        service = service->next;
    }
    
    return 0;
}

// =============================================================================
// Health Monitoring
// =============================================================================

void nexus_check_service_health(nexus_service_t* service) {
    if (!service || service->state != SERVICE_STATE_RUNNING) {
        return;
    }
    
    bool healthy = true;
    
    // Check if process is still alive
    if (kill(service->pid, 0) != 0) {
        healthy = false;
    }
    
    // Run custom health check if defined
    if (healthy && service->health_check) {
        service->health_check(service);
        healthy = service->healthy;
    }
    
    if (!healthy && service->healthy) {
        // Service became unhealthy
        nexus_log(service, "Service %s is unhealthy", service->name);
        service->healthy = false;
        nexus_handle_unhealthy_service(service);
    } else if (healthy && !service->healthy) {
        // Service recovered
        nexus_log(service, "Service %s recovered", service->name);
        service->healthy = true;
    }
    
    service->last_health_check = temporal_get_time();
}

void nexus_monitor_all_services(void) {
    uint64_t now = temporal_get_time();
    nexus_service_t* service = g_manager.services;
    
    while (service) {
        if (service->health_check_interval > 0) {
            if (now - service->last_health_check >= service->health_check_interval * 1000000) {
                nexus_check_service_health(service);
            }
        }
        service = service->next;
    }
}

// =============================================================================
// Main Service Manager Loop
// =============================================================================

static void nexus_main_loop(void* arg) {
    while (g_nexus_running) {
        // Process events
        nexus_process_events();
        
        // Monitor services
        nexus_monitor_all_services();
        
        // Handle pending starts (waiting for dependencies)
        nexus_service_t* service = g_manager.services;
        while (service) {
            if (service->state == SERVICE_STATE_WAITING) {
                if (nexus_check_dependencies(service)) {
                    nexus_spawn_service(service);
                }
            }
            service = service->next;
        }
        
        // Reap zombie processes
        pid_t pid;
        int status;
        while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
            nexus_handle_service_exit(pid, WEXITSTATUS(status));
        }
        
        temporal_sleep(100000);  // 100ms
    }
}

// =============================================================================
// Initialization
// =============================================================================

int nexus_init(void) {
    memset(&g_manager, 0, sizeof(g_manager));
    
    // Set initial runlevel
    g_manager.current_runlevel = RUNLEVEL_SINGLE;
    
    // Load system services configuration
    nexus_load_config("/etc/nexus/services.conf");
    
    // Register essential services
    nexus_register_essential_services();
    
    // Build dependency graph
    nexus_build_dependency_graph();
    
    // Start service manager thread
    g_nexus_running = true;
    thread_t* manager_thread = temporal_create_thread(nexus_main_loop, NULL,
                                                     THREAD_PRIORITY_HIGH);
    if (!manager_thread) {
        return -1;
    }
    
    // Start boot services
    nexus_change_runlevel(RUNLEVEL_MULTI_USER);
    
    return 0;
}

void nexus_shutdown(void) {
    nexus_log(NULL, "Shutting down Nexus service manager");
    
    // Change to halt runlevel
    nexus_change_runlevel(RUNLEVEL_HALT);
    
    // Stop all services
    nexus_service_t* service = g_manager.services;
    while (service) {
        if (service->state == SERVICE_STATE_RUNNING) {
            nexus_terminate_service(service);
        }
        service = service->next;
    }
    
    g_nexus_running = false;
    
    // Wait for manager thread to exit
    temporal_sleep(200000);
}

// =============================================================================
// Essential Services Registration
// =============================================================================

static void nexus_register_essential_services(void) {
    // Device manager
    nexus_service_t* devmgr = nexus_create_service("devmgr", "/sbin/devmgr");
    devmgr->flags |= SERVICE_FLAG_ESSENTIAL;
    devmgr->runlevel = RUNLEVEL_SINGLE;
    nexus_register_service(devmgr);
    
    // Network manager
    nexus_service_t* netmgr = nexus_create_service("netmgr", "/sbin/netmgr");
    netmgr->flags |= SERVICE_FLAG_NETWORK;
    netmgr->runlevel = RUNLEVEL_MULTI_USER;
    nexus_add_dependency(netmgr, "devmgr", true, false);
    nexus_register_service(netmgr);
    
    // Display manager
    nexus_service_t* display = nexus_create_service("prism", "/usr/bin/prism");
    display->flags |= SERVICE_FLAG_GRAPHICS;
    display->runlevel = RUNLEVEL_GRAPHICAL;
    nexus_add_dependency(display, "devmgr", true, false);
    nexus_register_service(display);
    
    // Package manager daemon
    nexus_service_t* pkgd = nexus_create_service("infinityd", "/usr/bin/infinityd");
    pkgd->runlevel = RUNLEVEL_MULTI_USER;
    nexus_add_dependency(pkgd, "netmgr", false, false);
    nexus_register_service(pkgd);
}
