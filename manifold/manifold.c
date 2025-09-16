/*
 * Manifold Virtual Filesystem
 * Core VFS implementation
 */

#include "manifold.h"
#include "../continuum/flux_memory.h"
#include "../continuum/temporal_scheduler.h"
#include <string.h>
#include <errno.h>

// =============================================================================
// Global VFS State
// =============================================================================

static vfs_state_t g_vfs;
static spinlock_t g_vfs_lock = SPINLOCK_INIT;

// =============================================================================
// Path Resolution
// =============================================================================

vfs_node_t* manifold_lookup(const char* path) {
    if (!path || path[0] != '/') {
        return NULL;
    }
    
    // Start from root
    vfs_node_t* current = g_vfs.root_node;
    if (!current) {
        return NULL;
    }
    
    manifold_ref_node(current);
    
    // Handle root path
    if (path[1] == '\0') {
        return current;
    }
    
    char component[MANIFOLD_MAX_NAME + 1];
    const char* p = path + 1;  // Skip initial '/'
    
    while (*p) {
        // Extract next path component
        const char* slash = strchr(p, '/');
        size_t len;
        
        if (slash) {
            len = slash - p;
            if (len == 0) {
                p++;
                continue;  // Skip multiple slashes
            }
        } else {
            len = strlen(p);
        }
        
        if (len > MANIFOLD_MAX_NAME) {
            manifold_unref_node(current);
            return NULL;
        }
        
        memcpy(component, p, len);
        component[len] = '\0';
        
        // Handle special components
        if (strcmp(component, ".") == 0) {
            // Current directory - no change
        } else if (strcmp(component, "..") == 0) {
            // Parent directory
            vfs_node_t* parent = manifold_get_parent(current);
            if (parent) {
                manifold_unref_node(current);
                current = parent;
            }
        } else {
            // Check if current node is a directory
            if (current->type != VFS_TYPE_DIRECTORY) {
                manifold_unref_node(current);
                return NULL;
            }
            
            // Check dentry cache first
            vfs_dentry_t* dentry = manifold_dentry_lookup(current, component);
            vfs_node_t* next = NULL;
            
            if (dentry) {
                next = dentry->node;
                manifold_ref_node(next);
                g_vfs.cache_hits++;
            } else {
                // Call filesystem lookup
                if (current->ops && current->ops->lookup) {
                    next = current->ops->lookup(current, component);
                    if (next) {
                        // Add to dentry cache
                        manifold_dentry_add(current, component, next);
                    }
                }
                g_vfs.cache_misses++;
            }
            
            if (!next) {
                manifold_unref_node(current);
                return NULL;
            }
            
            // Handle symbolic links
            if (next->type == VFS_TYPE_SYMLINK) {
                char target[MANIFOLD_MAX_PATH];
                if (current->ops && current->ops->readlink) {
                    ssize_t len = current->ops->readlink(next, target, sizeof(target) - 1);
                    if (len > 0) {
                        target[len] = '\0';
                        // Recursive lookup for symlink target
                        vfs_node_t* resolved = manifold_lookup(target);
                        manifold_unref_node(next);
                        next = resolved;
                    }
                }
            }
            
            manifold_unref_node(current);
            current = next;
        }
        
        // Move to next component
        if (slash) {
            p = slash + 1;
        } else {
            break;
        }
    }
    
    g_vfs.lookups++;
    return current;
}

// =============================================================================
// File Operations
// =============================================================================

int manifold_open(const char* path, uint32_t flags, uint32_t mode) {
    vfs_node_t* node = NULL;
    
    if (flags & VFS_O_CREAT) {
        // Check if file exists
        node = manifold_lookup(path);
        if (node) {
            if (flags & VFS_O_EXCL) {
                manifold_unref_node(node);
                return -EEXIST;
            }
        } else {
            // Create new file
            char parent_path[MANIFOLD_MAX_PATH];
            char name[MANIFOLD_MAX_NAME + 1];
            
            if (manifold_split_path(path, parent_path, name) != 0) {
                return -EINVAL;
            }
            
            vfs_node_t* parent = manifold_lookup(parent_path);
            if (!parent) {
                return -ENOENT;
            }
            
            if (parent->ops && parent->ops->create) {
                int result = parent->ops->create(parent, name, mode, &node);
                manifold_unref_node(parent);
                if (result != 0) {
                    return result;
                }
            } else {
                manifold_unref_node(parent);
                return -ENOSYS;
            }
        }
    } else {
        node = manifold_lookup(path);
        if (!node) {
            return -ENOENT;
        }
    }
    
    // Check permissions
    uint32_t uid = temporal_get_current_uid();
    uint32_t gid = temporal_get_current_gid();
    
    if (flags & VFS_O_RDWR) {
        if (!manifold_can_read(node, uid, gid) || !manifold_can_write(node, uid, gid)) {
            manifold_unref_node(node);
            return -EACCES;
        }
    } else if (flags & VFS_O_WRONLY) {
        if (!manifold_can_write(node, uid, gid)) {
            manifold_unref_node(node);
            return -EACCES;
        }
    } else {
        if (!manifold_can_read(node, uid, gid)) {
            manifold_unref_node(node);
            return -EACCES;
        }
    }
    
    // Allocate file descriptor
    vfs_file_t* file = flux_allocate(NULL, sizeof(vfs_file_t),
                                    FLUX_ALLOC_KERNEL | FLUX_ALLOC_ZERO);
    if (!file) {
        manifold_unref_node(node);
        return -ENOMEM;
    }
    
    file->node = node;
    file->flags = flags;
    file->offset = 0;
    file->ref_count = 1;
    
    // Call filesystem open
    if (node->ops && node->ops->open) {
        int result = node->ops->open(file, node, flags);
        if (result != 0) {
            flux_free(file);
            manifold_unref_node(node);
            return result;
        }
    }
    
    // Truncate if requested
    if (flags & VFS_O_TRUNC) {
        if (node->ops && node->ops->setattr) {
            vfs_stat_t stat = {0};
            stat.size = 0;
            node->ops->setattr(node, &stat);
        }
    }
    
    // Add to process file descriptor table
    int fd = process_allocate_fd(temporal_get_current_process(), file);
    if (fd < 0) {
        if (node->ops && node->ops->close) {
            node->ops->close(file);
        }
        flux_free(file);
        manifold_unref_node(node);
        return fd;
    }
    
    file->fd = fd;
    return fd;
}

ssize_t manifold_read(int fd, void* buffer, size_t size) {
    vfs_file_t* file = process_get_file(temporal_get_current_process(), fd);
    if (!file) {
        return -EBADF;
    }
    
    if (!(file->flags & (VFS_O_RDONLY | VFS_O_RDWR))) {
        return -EBADF;
    }
    
    if (!file->node->ops || !file->node->ops->read) {
        return -ENOSYS;
    }
    
    ssize_t result = file->node->ops->read(file, buffer, size);
    if (result > 0) {
        file->offset += result;
        file->node->atime = time(NULL);
    }
    
    return result;
}

ssize_t manifold_write(int fd, const void* buffer, size_t size) {
    vfs_file_t* file = process_get_file(temporal_get_current_process(), fd);
    if (!file) {
        return -EBADF;
    }
    
    if (!(file->flags & (VFS_O_WRONLY | VFS_O_RDWR))) {
        return -EBADF;
    }
    
    if (!file->node->ops || !file->node->ops->write) {
        return -ENOSYS;
    }
    
    // Handle append mode
    if (file->flags & VFS_O_APPEND) {
        file->offset = file->node->size;
    }
    
    ssize_t result = file->node->ops->write(file, buffer, size);
    if (result > 0) {
        file->offset += result;
        file->node->mtime = time(NULL);
        file->node->ctime = time(NULL);
    }
    
    return result;
}

int manifold_close(int fd) {
    vfs_file_t* file = process_remove_file(temporal_get_current_process(), fd);
    if (!file) {
        return -EBADF;
    }
    
    file->ref_count--;
    if (file->ref_count == 0) {
        if (file->node->ops && file->node->ops->close) {
            file->node->ops->close(file);
        }
        
        manifold_unref_node(file->node);
        flux_free(file);
    }
    
    return 0;
}

// =============================================================================
// Mount Operations
// =============================================================================

int manifold_mount(const char* source, const char* target, const char* fstype,
                  uint32_t flags, void* data) {
    // Find filesystem type
    vfs_filesystem_t* fs = manifold_find_filesystem(fstype);
    if (!fs) {
        return -ENODEV;
    }
    
    // Lookup target directory
    vfs_node_t* mount_point = manifold_lookup(target);
    if (!mount_point) {
        return -ENOENT;
    }
    
    if (mount_point->type != VFS_TYPE_DIRECTORY) {
        manifold_unref_node(mount_point);
        return -ENOTDIR;
    }
    
    // Check if already mounted
    spinlock_acquire(&g_vfs_lock);
    vfs_mount_t* existing = g_vfs.mounts;
    while (existing) {
        if (strcmp(existing->target, target) == 0) {
            spinlock_release(&g_vfs_lock);
            manifold_unref_node(mount_point);
            return -EBUSY;
        }
        existing = existing->next;
    }
    spinlock_release(&g_vfs_lock);
    
    // Create mount structure
    vfs_mount_t* mount = flux_allocate(NULL, sizeof(vfs_mount_t),
                                      FLUX_ALLOC_KERNEL | FLUX_ALLOC_ZERO);
    if (!mount) {
        manifold_unref_node(mount_point);
        return -ENOMEM;
    }
    
    strncpy(mount->source, source, MANIFOLD_MAX_PATH - 1);
    strncpy(mount->target, target, MANIFOLD_MAX_PATH - 1);
    strncpy(mount->fstype, fstype, 31);
    mount->flags = flags;
    mount->fs = fs;
    mount->mount_point = mount_point;
    
    // Call filesystem mount
    if (fs->ops && fs->ops->mount) {
        int result = fs->ops->mount(mount, data);
        if (result != 0) {
            flux_free(mount);
            manifold_unref_node(mount_point);
            return result;
        }
    }
    
    // Add to mount list
    spinlock_acquire(&g_vfs_lock);
    mount->next = g_vfs.mounts;
    g_vfs.mounts = mount;
    spinlock_release(&g_vfs_lock);
    
    return 0;
}

int manifold_unmount(const char* target) {
    spinlock_acquire(&g_vfs_lock);
    
    vfs_mount_t** prev = &g_vfs.mounts;
    vfs_mount_t* mount = g_vfs.mounts;
    
    while (mount) {
        if (strcmp(mount->target, target) == 0) {
            *prev = mount->next;
            spinlock_release(&g_vfs_lock);
            
            // Call filesystem unmount
            if (mount->fs->ops && mount->fs->ops->unmount) {
                mount->fs->ops->unmount(mount);
            }
            
            // Invalidate dentry cache for this mount
            manifold_dentry_invalidate(mount->root);
            
            // Release mount point
            manifold_unref_node(mount->mount_point);
            
            flux_free(mount);
            return 0;
        }
        prev = &mount->next;
        mount = mount->next;
    }
    
    spinlock_release(&g_vfs_lock);
    return -EINVAL;
}

// =============================================================================
// Directory Operations
// =============================================================================

int manifold_mkdir(const char* path, uint32_t mode) {
    char parent_path[MANIFOLD_MAX_PATH];
    char name[MANIFOLD_MAX_NAME + 1];
    
    if (manifold_split_path(path, parent_path, name) != 0) {
        return -EINVAL;
    }
    
    vfs_node_t* parent = manifold_lookup(parent_path);
    if (!parent) {
        return -ENOENT;
    }
    
    if (parent->type != VFS_TYPE_DIRECTORY) {
        manifold_unref_node(parent);
        return -ENOTDIR;
    }
    
    // Check write permission on parent
    uint32_t uid = temporal_get_current_uid();
    uint32_t gid = temporal_get_current_gid();
    
    if (!manifold_can_write(parent, uid, gid)) {
        manifold_unref_node(parent);
        return -EACCES;
    }
    
    // Check if already exists
    vfs_dentry_t* existing = manifold_dentry_lookup(parent, name);
    if (existing) {
        manifold_unref_node(parent);
        return -EEXIST;
    }
    
    // Call filesystem mkdir
    int result = -ENOSYS;
    if (parent->ops && parent->ops->mkdir) {
        result = parent->ops->mkdir(parent, name, mode);
    }
    
    manifold_unref_node(parent);
    return result;
}

int manifold_rmdir(const char* path) {
    char parent_path[MANIFOLD_MAX_PATH];
    char name[MANIFOLD_MAX_NAME + 1];
    
    if (manifold_split_path(path, parent_path, name) != 0) {
        return -EINVAL;
    }
    
    vfs_node_t* parent = manifold_lookup(parent_path);
    if (!parent) {
        return -ENOENT;
    }
    
    // Check write permission on parent
    uint32_t uid = temporal_get_current_uid();
    uint32_t gid = temporal_get_current_gid();
    
    if (!manifold_can_write(parent, uid, gid)) {
        manifold_unref_node(parent);
        return -EACCES;
    }
    
    // Check if directory exists and is empty
    vfs_node_t* dir = manifold_lookup(path);
    if (!dir) {
        manifold_unref_node(parent);
        return -ENOENT;
    }
    
    if (dir->type != VFS_TYPE_DIRECTORY) {
        manifold_unref_node(dir);
        manifold_unref_node(parent);
        return -ENOTDIR;
    }
    
    // Check if directory is empty (implementation specific)
    manifold_unref_node(dir);
    
    // Call filesystem rmdir
    int result = -ENOSYS;
    if (parent->ops && parent->ops->rmdir) {
        result = parent->ops->rmdir(parent, name);
        if (result == 0) {
            // Remove from dentry cache
            manifold_dentry_remove(parent, name);
        }
    }
    
    manifold_unref_node(parent);
    return result;
}

// =============================================================================
// Cache Management
// =============================================================================

vfs_node_t* manifold_cache_lookup(vfs_mount_t* mount, uint64_t ino) {
    uint32_t hash = (uint32_t)(ino ^ (uint64_t)mount) % 1024;
    
    spinlock_acquire(&g_vfs_lock);
    
    vfs_node_t* node = g_vfs.node_cache[hash];
    while (node) {
        if (node->mount == mount && node->ino == ino) {
            // Move to front of LRU
            if (node != g_vfs.lru_head) {
                if (node->lru_prev) {
                    node->lru_prev->lru_next = node->lru_next;
                }
                if (node->lru_next) {
                    node->lru_next->lru_prev = node->lru_prev;
                } else {
                    g_vfs.lru_tail = node->lru_prev;
                }
                
                node->lru_prev = NULL;
                node->lru_next = g_vfs.lru_head;
                g_vfs.lru_head->lru_prev = node;
                g_vfs.lru_head = node;
            }
            
            manifold_ref_node(node);
            spinlock_release(&g_vfs_lock);
            return node;
        }
        node = node->hash_next;
    }
    
    spinlock_release(&g_vfs_lock);
    return NULL;
}

void manifold_cache_insert(vfs_node_t* node) {
    uint32_t hash = (uint32_t)(node->ino ^ (uint64_t)node->mount) % 1024;
    
    spinlock_acquire(&g_vfs_lock);
    
    // Add to hash table
    node->hash_next = g_vfs.node_cache[hash];
    g_vfs.node_cache[hash] = node;
    
    // Add to LRU head
    node->lru_prev = NULL;
    node->lru_next = g_vfs.lru_head;
    if (g_vfs.lru_head) {
        g_vfs.lru_head->lru_prev = node;
    } else {
        g_vfs.lru_tail = node;
    }
    g_vfs.lru_head = node;
    
    g_vfs.cached_nodes++;
    
    // Evict old nodes if cache is full
    if (g_vfs.cached_nodes > 1000) {
        manifold_cache_evict(100);
    }
    
    spinlock_release(&g_vfs_lock);
}

// =============================================================================
// Permission Checking
// =============================================================================

bool manifold_check_permission(vfs_node_t* node, uint32_t uid, uint32_t gid,
                              uint32_t permission) {
    if (uid == 0) {
        return true;  // Root has all permissions
    }
    
    uint32_t mode = node->mode;
    
    if (uid == node->uid) {
        // Owner permissions
        return (mode & (permission << 6)) != 0;
    } else if (gid == node->gid) {
        // Group permissions
        return (mode & (permission << 3)) != 0;
    } else {
        // Other permissions
        return (mode & permission) != 0;
    }
}

bool manifold_can_read(vfs_node_t* node, uint32_t uid, uint32_t gid) {
    return manifold_check_permission(node, uid, gid, 04);
}

bool manifold_can_write(vfs_node_t* node, uint32_t uid, uint32_t gid) {
    return manifold_check_permission(node, uid, gid, 02);
}

bool manifold_can_execute(vfs_node_t* node, uint32_t uid, uint32_t gid) {
    return manifold_check_permission(node, uid, gid, 01);
}

// =============================================================================
// Initialization
// =============================================================================

int manifold_init(void) {
    memset(&g_vfs, 0, sizeof(g_vfs));
    
    // Create root node
    g_vfs.root_node = manifold_alloc_node();
    if (!g_vfs.root_node) {
        return -ENOMEM;
    }
    
    g_vfs.root_node->type = VFS_TYPE_DIRECTORY;
    g_vfs.root_node->mode = 0755;
    g_vfs.root_node->uid = 0;
    g_vfs.root_node->gid = 0;
    
    // Register built-in filesystems
    manifold_register_tmpfs();
    manifold_register_devfs();
    manifold_register_procfs();
    manifold_register_sysfs();
    
    // Mount root filesystem (tmpfs for now)
    int result = manifold_mount("none", "/", "tmpfs", 0, NULL);
    if (result != 0) {
        return result;
    }
    
    // Create essential directories
    manifold_mkdir("/dev", 0755);
    manifold_mkdir("/proc", 0755);
    manifold_mkdir("/sys", 0755);
    manifold_mkdir("/tmp", 01777);
    manifold_mkdir("/mnt", 0755);
    manifold_mkdir("/etc", 0755);
    manifold_mkdir("/usr", 0755);
    manifold_mkdir("/var", 0755);
    
    // Mount essential filesystems
    manifold_mount("none", "/dev", "devfs", 0, NULL);
    manifold_mount("none", "/proc", "procfs", 0, NULL);
    manifold_mount("none", "/sys", "sysfs", 0, NULL);
    
    return 0;
}

void manifold_shutdown(void) {
    // Unmount all filesystems
    while (g_vfs.mounts) {
        manifold_unmount(g_vfs.mounts->target);
    }
    
    // Free cache
    manifold_cache_evict(g_vfs.cached_nodes);
    
    // Free root node
    if (g_vfs.root_node) {
        manifold_free_node(g_vfs.root_node);
    }
}
