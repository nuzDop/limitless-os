/*
 * Forge Build System
 * Core build automation implementation
 */

#include "forge.h"
#include "parser.h"
#include "../manifold/manifold.h"
#include "../continuum/flux_memory.h"
#include "../continuum/temporal_scheduler.h"
#include <stdio.h>
#include <string.h>
#include <sys/wait.h>

// =============================================================================
// Global State
// =============================================================================

static build_context_t* g_current_context;
static spinlock_t g_forge_lock = SPINLOCK_INIT;

// =============================================================================
// Build Execution
// =============================================================================

int forge_build(build_context_t* ctx, const char* target_name) {
    if (!ctx || !target_name) {
        return -1;
    }
    
    g_current_context = ctx;
    
    // Find target
    build_target_t* target = forge_find_target(ctx, target_name);
    if (!target) {
        // Try to create target from pattern rules
        target = forge_create_from_rules(ctx, target_name);
        if (!target) {
            forge_error("No rule to make target '%s'", target_name);
            return -1;
        }
    }
    
    // Check if rebuild needed
    if (!ctx->force_rebuild && !forge_target_needs_rebuild(target)) {
        if (ctx->verbose) {
            forge_info("'%s' is up to date", target_name);
        }
        target->state = BUILD_STATE_SKIPPED;
        return 0;
    }
    
    // Build dependency graph
    if (forge_analyze_dependencies(ctx) != 0) {
        forge_error("Failed to analyze dependencies");
        return -1;
    }
    
    // Get build order
    uint32_t order_count;
    build_target_t** build_order = forge_get_build_order(ctx, &order_count);
    if (!build_order) {
        forge_error("Circular dependency detected");
        return -1;
    }
    
    // Execute build
    int result = 0;
    
    if (ctx->max_jobs > 1) {
        // Parallel build
        result = forge_build_parallel(ctx, build_order, order_count);
    } else {
        // Sequential build
        result = forge_build_sequential(ctx, build_order, order_count);
    }
    
    // Clean up
    flux_free(build_order);
    
    // Print summary
    forge_print_summary(ctx);
    
    return result;
}

static int forge_build_sequential(build_context_t* ctx, build_target_t** targets,
                                 uint32_t count) {
    for (uint32_t i = 0; i < count; i++) {
        build_target_t* target = targets[i];
        
        if (target->state != BUILD_STATE_PENDING) {
            continue;
        }
        
        // Check dependencies
        bool deps_ready = true;
        build_dep_t* dep = target->dependencies;
        while (dep) {
            build_target_t* dep_target = forge_find_target(ctx, dep->name);
            if (dep_target && dep_target->state == BUILD_STATE_FAILED) {
                deps_ready = false;
                break;
            }
            dep = dep->next;
        }
        
        if (!deps_ready) {
            target->state = BUILD_STATE_SKIPPED;
            ctx->targets_skipped++;
            continue;
        }
        
        // Execute target
        if (forge_execute_target(ctx, target) != 0) {
            target->state = BUILD_STATE_FAILED;
            ctx->targets_failed++;
            
            if (!ctx->keep_going) {
                return -1;
            }
        } else {
            target->state = BUILD_STATE_SUCCESS;
            ctx->targets_built++;
        }
    }
    
    return (ctx->targets_failed > 0) ? -1 : 0;
}

static int forge_build_parallel(build_context_t* ctx, build_target_t** targets,
                               uint32_t count) {
    // Initialize job queue
    build_job_t* job_pool[FORGE_MAX_JOBS];
    uint32_t job_count = 0;
    
    // Build all targets
    uint32_t completed = 0;
    uint32_t target_index = 0;
    
    while (completed < count) {
        // Start new jobs up to max_jobs limit
        while (ctx->active_jobs < ctx->max_jobs && target_index < count) {
            build_target_t* target = targets[target_index++];
            
            if (target->state != BUILD_STATE_PENDING) {
                completed++;
                continue;
            }
            
            // Check if dependencies are ready
            if (!forge_dependencies_ready(target)) {
                continue;
            }
            
            // Create and start job
            build_job_t* job = forge_create_job(target);
            if (job) {
                if (forge_start_job(job, ctx) == 0) {
                    job_pool[job_count++] = job;
                    ctx->active_jobs++;
                    target->state = BUILD_STATE_RUNNING;
                } else {
                    forge_free_job(job);
                    target->state = BUILD_STATE_FAILED;
                    ctx->targets_failed++;
                    
                    if (!ctx->keep_going) {
                        forge_cancel_all_jobs(job_pool, job_count);
                        return -1;
                    }
                }
            }
        }
        
        // Wait for job completion
        if (ctx->active_jobs > 0) {
            int status;
            pid_t pid = wait(&status);
            
            // Find completed job
            for (uint32_t i = 0; i < job_count; i++) {
                build_job_t* job = job_pool[i];
                if (job && job->pid == pid) {
                    // Process job completion
                    job->exit_code = WEXITSTATUS(status);
                    job->end_time = time(NULL);
                    job->state = (job->exit_code == 0) ? 
                                BUILD_STATE_SUCCESS : BUILD_STATE_FAILED;
                    
                    // Update target state
                    job->target->state = job->state;
                    if (job->state == BUILD_STATE_SUCCESS) {
                        ctx->targets_built++;
                    } else {
                        ctx->targets_failed++;
                        
                        if (!ctx->keep_going) {
                            forge_cancel_all_jobs(job_pool, job_count);
                            return -1;
                        }
                    }
                    
                    // Print output if verbose
                    if (ctx->verbose && job->stdout_buffer) {
                        printf("%s", job->stdout_buffer);
                    }
                    if (job->stderr_buffer) {
                        fprintf(stderr, "%s", job->stderr_buffer);
                    }
                    
                    ctx->active_jobs--;
                    completed++;
                    forge_free_job(job);
                    job_pool[i] = NULL;
                    break;
                }
            }
        }
        
        // Small delay to prevent busy waiting
        if (ctx->active_jobs >= ctx->max_jobs) {
            temporal_sleep(1000);  // 1ms
        }
    }
    
    return (ctx->targets_failed > 0) ? -1 : 0;
}

static int forge_execute_target(build_context_t* ctx, build_target_t* target) {
    if (!ctx || !target) {
        return -1;
    }
    
    time_t start_time = time(NULL);
    
    if (!ctx->dry_run) {
        forge_info("Building %s", target->name);
    }
    
    // Execute commands
    build_cmd_t* cmd = target->commands;
    while (cmd) {
        char* expanded = forge_expand_variables(ctx, cmd->command);
        
        if (ctx->dry_run) {
            printf("%s\n", expanded);
        } else {
            if (!(cmd->flags & RULE_FLAG_SILENT) && ctx->verbose) {
                printf("%s\n", expanded);
            }
            
            int result = system(expanded);
            
            if (result != 0 && !(cmd->flags & RULE_FLAG_IGNORE_ERROR)) {
                forge_error("Command failed: %s", expanded);
                flux_free(expanded);
                return -1;
            }
        }
        
        flux_free(expanded);
        cmd = cmd->next;
    }
    
    // Update target info
    target->build_time = time(NULL);
    target->build_duration = target->build_time - start_time;
    target->build_count++;
    
    // Update mtime for file targets
    if (target->type == TARGET_TYPE_FILE) {
        target->mtime = forge_get_mtime(target->name);
    }
    
    return 0;
}

// =============================================================================
// Dependency Analysis
// =============================================================================

bool forge_target_needs_rebuild(build_target_t* target) {
    if (!target) {
        return false;
    }
    
    // Phony targets always need rebuild
    if (target->type == TARGET_TYPE_PHONY) {
        return true;
    }
    
    // Check if target exists
    if (!forge_file_exists(target->name)) {
        return true;
    }
    
    time_t target_mtime = forge_get_mtime(target->name);
    
    // Check dependencies
    build_dep_t* dep = target->dependencies;
    while (dep) {
        if (dep->is_target) {
            build_target_t* dep_target = forge_find_target(g_current_context, dep->name);
            if (dep_target && forge_target_needs_rebuild(dep_target)) {
                return true;
            }
        } else {
            // File dependency
            if (forge_file_exists(dep->name)) {
                time_t dep_mtime = forge_get_mtime(dep->name);
                if (dep_mtime > target_mtime) {
                    return true;
                }
            }
        }
        dep = dep->next;
    }
    
    return false;
}

int forge_analyze_dependencies(build_context_t* ctx) {
    // Build dependency graph
    if (forge_build_dependency_graph(ctx) != 0) {
        return -1;
    }
    
    // Perform topological sort
    if (forge_topological_sort(ctx) != 0) {
        forge_error("Circular dependency detected");
        return -1;
    }
    
    // Mark targets that need rebuilding
    build_target_t* target = ctx->graph.targets;
    while (target) {
        target->needs_rebuild = forge_target_needs_rebuild(target);
        target->state = target->needs_rebuild ? 
                       BUILD_STATE_PENDING : BUILD_STATE_SKIPPED;
        target = target->next;
    }
    
    return 0;
}

// =============================================================================
// Variable Management
// =============================================================================

int forge_set_variable(build_context_t* ctx, const char* name, const char* value) {
    if (!ctx || !name) {
        return -1;
    }
    
    // Check if variable exists
    build_var_t* var = ctx->graph.variables;
    while (var) {
        if (strcmp(var->name, name) == 0) {
            // Update existing variable
            if (var->value) {
                flux_free(var->value);
            }
            var->value = value ? strdup(value) : NULL;
            return 0;
        }
        var = var->next;
    }
    
    // Create new variable
    var = flux_allocate(NULL, sizeof(build_var_t), FLUX_ALLOC_KERNEL | FLUX_ALLOC_ZERO);
    if (!var) {
        return -1;
    }
    
    strncpy(var->name, name, FORGE_MAX_NAME_LEN - 1);
    var->value = value ? strdup(value) : NULL;
    
    // Add to list
    var->next = ctx->graph.variables;
    ctx->graph.variables = var;
    ctx->graph.var_count++;
    
    return 0;
}

char* forge_expand_variables(build_context_t* ctx, const char* str) {
    if (!ctx || !str) {
        return NULL;
    }
    
    size_t result_size = strlen(str) * 2 + 1;
    char* result = flux_allocate(NULL, result_size, FLUX_ALLOC_KERNEL);
    if (!result) {
        return NULL;
    }
    
    size_t result_pos = 0;
    const char* p = str;
    
    while (*p) {
        if (*p == '$') {
            p++;
            
            if (*p == '$') {
                // Escaped $
                result[result_pos++] = '$';
                p++;
            } else if (*p == '(') {
                // Variable reference $(VAR)
                p++;
                const char* var_start = p;
                int paren_count = 1;
                
                while (*p && paren_count > 0) {
                    if (*p == '(') paren_count++;
                    else if (*p == ')') paren_count--;
                    if (paren_count > 0) p++;
                }
                
                if (paren_count == 0) {
                    size_t var_len = p - var_start;
                    char var_name[FORGE_MAX_NAME_LEN];
                    
                    if (var_len < FORGE_MAX_NAME_LEN) {
                        memcpy(var_name, var_start, var_len);
                        var_name[var_len] = '\0';
                        
                        // Get variable value
                        const char* value = forge_get_variable(ctx, var_name);
                        if (value) {
                            size_t value_len = strlen(value);
                            
                            // Resize result if needed
                            if (result_pos + value_len >= result_size) {
                                result_size = result_pos + value_len + 1024;
                                result = flux_reallocate(result, result_size);
                            }
                            
                            strcpy(&result[result_pos], value);
                            result_pos += value_len;
                        }
                    }
                    p++;
                }
            } else if (*p == '{') {
                // Variable reference ${VAR}
                p++;
                const char* var_start = p;
                
                while (*p && *p != '}') {
                    p++;
                }
                
                if (*p == '}') {
                    size_t var_len = p - var_start;
                    char var_name[FORGE_MAX_NAME_LEN];
                    
                    if (var_len < FORGE_MAX_NAME_LEN) {
                        memcpy(var_name, var_start, var_len);
                        var_name[var_len] = '\0';
                        
                        const char* value = forge_get_variable(ctx, var_name);
                        if (value) {
                            size_t value_len = strlen(value);
                            
                            if (result_pos + value_len >= result_size) {
                                result_size = result_pos + value_len + 1024;
                                result = flux_reallocate(result, result_size);
                            }
                            
                            strcpy(&result[result_pos], value);
                            result_pos += value_len;
                        }
                    }
                    p++;
                }
            } else {
                // Simple variable reference $VAR
                const char* var_start = p;
                
                while (*p && (isalnum(*p) || *p == '_')) {
                    p++;
                }
                
                size_t var_len = p - var_start;
                if (var_len > 0) {
                    char var_name[FORGE_MAX_NAME_LEN];
                    
                    if (var_len < FORGE_MAX_NAME_LEN) {
                        memcpy(var_name, var_start, var_len);
                        var_name[var_len] = '\0';
                        
                        const char* value = forge_get_variable(ctx, var_name);
                        if (value) {
                            size_t value_len = strlen(value);
                            
                            if (result_pos + value_len >= result_size) {
                                result_size = result_pos + value_len + 1024;
                                result = flux_reallocate(result, result_size);
                            }
                            
                            strcpy(&result[result_pos], value);
                            result_pos += value_len;
                        }
                    }
                }
            }
        } else {
            // Regular character
            if (result_pos >= result_size - 1) {
                result_size += 1024;
                result = flux_reallocate(result, result_size);
            }
            
            result[result_pos++] = *p++;
        }
    }
    
    result[result_pos] = '\0';
    return result;
}

// =============================================================================
// Initialization
// =============================================================================

int forge_init(void) {
    // Initialize parser
    forge_parser_init();
    
    // Setup default variables
    build_context_t* ctx = forge_create_context();
    if (!ctx) {
        return -1;
    }
    
    // Set built-in variables
    forge_set_variable(ctx, "CC", "gcc");
    forge_set_variable(ctx, "CXX", "g++");
    forge_set_variable(ctx, "AS", "as");
    forge_set_variable(ctx, "LD", "ld");
    forge_set_variable(ctx, "AR", "ar");
    forge_set_variable(ctx, "MAKE", "forge");
    
    // Detect toolchain
    forge_detect_toolchain(&ctx->toolchain);
    
    // Initialize cache
    forge_cache_init(ctx);
    
    g_current_context = ctx;
    
    return 0;
}

void forge_cleanup(void) {
    if (g_current_context) {
        // Clean up build graph
        forge_free_graph(&g_current_context->graph);
        
        // Clean up cache
        forge_cache_cleanup(g_current_context);
        
        // Free context
        flux_free(g_current_context);
        g_current_context = NULL;
    }
}
