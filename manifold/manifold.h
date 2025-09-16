/*
 * Manifold Virtual Filesystem
 * Unified filesystem interface for Limitless OS
 */

#ifndef MANIFOLD_H
#define MANIFOLD_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <time.h>

// =============================================================================
// VFS Constants
// =============================================================================

#define MANIFOLD_MAX_PATH       4096
#define MANIFOLD_MAX_NAME       255
#define MANIFOLD_MAX_MOUNTS     128
#define MANIFOLD_MAX_FILES      65536
#define MANIFOLD_MAX_SYMLINKS   40

// File types
#define VFS_TYPE_REGULAR        0x01
#define VFS_TYPE_DIRECTORY      0x02
#define VFS_TYPE_SYMLINK        0x03
#define VFS_TYPE_DEVICE_CHAR    0x04
#define VFS_TYPE_DEVICE_BLOCK   0x05
#define VFS_TYPE_FIFO           0x06
#define VFS_TYPE_SOCKET         0x07

// File permissions (POSIX-like)
#define VFS_PERM_USER_READ      0400
#define VFS_PERM_USER_WRITE     0200
#define VFS_PERM_USER_EXEC      0100
#define VFS_PERM_GROUP_READ     0040
#define VFS_PERM_GROUP_WRITE    0020
#define VFS_PERM_GROUP_EXEC     0010
#define VFS_PERM_OTHER_READ     0004
#define VFS_PERM_OTHER_WRITE    0002
#define VFS_PERM_OTHER_EXEC     0001

// Special permission bits
#define VFS_PERM_SETUID         04000
#define VFS_PERM_SETGID         02000
#define VFS_PERM_STICKY         01000

// Open flags
#define VFS_O_RDONLY            0x0000
#define VFS_O_WRONLY            0x0001
#define VFS_O_RDWR              0x0002
#define VFS_O_APPEND            0x0008
#define VFS_O_CREAT             0x0040
#define VFS_O_EXCL              0x0080
#define VFS_O_TRUNC             0x0200
#define VFS_O_NONBLOCK          0x0800
#define VFS_O_SYNC              0x1000
#define VFS_O_DIRECTORY         0x10000
#define VFS_O_CLOEXEC           0x80000

// Seek operations
#define VFS_SEEK_SET            0
#define VFS_SEEK_CUR            1
#define VFS_SEEK_END            2

// Mount flags
#define VFS_MNT_RDONLY          0x01
#define VFS_MNT_NOSUID          0x02
#define VFS_MNT_NODEV           0x04
#define VFS_MNT_NOEXEC          0x08
#define VFS_MNT_SYNCHRONOUS     0x10
#define VFS_MNT_REMOUNT         0x20
#define VFS_MNT_NOATIME         0x40
#define VFS_MNT_RELATIME        0x80

// =============================================================================
// Data Structures
// =============================================================================

// Forward declarations
typedef struct vfs_node vfs_node_t;
typedef struct vfs_mount vfs_mount_t;
typedef struct vfs_filesystem vfs_filesystem_t;
typedef struct vfs_file vfs_file_t;
typedef struct vfs_dentry vfs_dentry_t;

// File statistics
typedef struct {
    uint64_t dev;           // Device ID
    uint64_t ino;           // Inode number
    uint32_t mode;          // File mode (type + permissions)
    uint32_t nlink;         // Number of hard links
    uint32_t uid;           // User ID
    uint32_t gid;           // Group ID
    uint64_t rdev;          // Device ID (if special file)
    uint64_t size;          // File size
    uint64_t blksize;       // Block size
    uint64_t blocks;        // Number of blocks
    time_t atime;           // Access time
    time_t mtime;           // Modification time
    time_t ctime;           // Change time
} vfs_stat_t;

// Directory entry
typedef struct {
    uint64_t ino;           // Inode number
    uint8_t type;           // File type
    char name[MANIFOLD_MAX_NAME + 1];
} vfs_dirent_t;

// Filesystem operations
typedef struct {
    // Superblock operations
    int (*mount)(vfs_mount_t* mount, void* data);
    int (*unmount)(vfs_mount_t* mount);
    int (*sync)(vfs_mount_t* mount);
    int (*statfs)(vfs_mount_t* mount, void* buf);
    
    // Inode operations
    vfs_node_t* (*lookup)(vfs_node_t* parent, const char* name);
    int (*create)(vfs_node_t* parent, const char* name, uint32_t mode, vfs_node_t** node);
    int (*mkdir)(vfs_node_t* parent, const char* name, uint32_t mode);
    int (*rmdir)(vfs_node_t* parent, const char* name);
    int (*unlink)(vfs_node_t* parent, const char* name);
    int (*rename)(vfs_node_t* old_parent, const char* old_name,
                  vfs_node_t* new_parent, const char* new_name);
    int (*link)(vfs_node_t* parent, const char* name, vfs_node_t* target);
    int (*symlink)(vfs_node_t* parent, const char* name, const char* target);
    int (*readlink)(vfs_node_t* node, char* buffer, size_t size);
    
    // File operations
    int (*open)(vfs_file_t* file, vfs_node_t* node, uint32_t flags);
    int (*close)(vfs_file_t* file);
    ssize_t (*read)(vfs_file_t* file, void* buffer, size_t size);
    ssize_t (*write)(vfs_file_t* file, const void* buffer, size_t size);
    off_t (*lseek)(vfs_file_t* file, off_t offset, int whence);
    int (*ioctl)(vfs_file_t* file, uint32_t cmd, void* arg);
    int (*mmap)(vfs_file_t* file, void* addr, size_t length, int prot, int flags);
    
    // Directory operations
    int (*readdir)(vfs_file_t* file, vfs_dirent_t* dirent);
    
    // Attribute operations
    int (*getattr)(vfs_node_t* node, vfs_stat_t* stat);
    int (*setattr)(vfs_node_t* node, vfs_stat_t* stat);
    int (*chmod)(vfs_node_t* node, uint32_t mode);
    int (*chown)(vfs_node_t* node, uint32_t uid, uint32_t gid);
    
    // Extended attributes
    int (*getxattr)(vfs_node_t* node, const char* name, void* value, size_t size);
    int (*setxattr)(vfs_node_t* node, const char* name, const void* value, 
                    size_t size, int flags);
    int (*listxattr)(vfs_node_t* node, char* list, size_t size);
    int (*removexattr)(vfs_node_t* node, const char* name);
} vfs_operations_t;

// VFS node (inode)
struct vfs_node {
    uint64_t ino;               // Inode number
    uint8_t type;               // File type
    uint32_t mode;              // Permissions
    uint32_t uid;               // Owner user ID
    uint32_t gid;               // Owner group ID
    uint64_t size;              // File size
    uint32_t nlink;             // Link count
    
    time_t atime;               // Access time
    time_t mtime;               // Modification time
    time_t ctime;               // Change time
    
    vfs_mount_t* mount;         // Mount point this node belongs to
    vfs_operations_t* ops;      // Operations table
    
    void* fs_data;              // Filesystem-specific data
    
    // Reference counting
    uint32_t ref_count;
    spinlock_t lock;
    
    // Cache management
    vfs_node_t* hash_next;      // Hash table chain
    vfs_node_t* lru_next;       // LRU list
    vfs_node_t* lru_prev;
    
    // Children (for directories)
    vfs_dentry_t* dentries;
};

// Directory entry cache
struct vfs_dentry {
    char name[MANIFOLD_MAX_NAME + 1];
    vfs_node_t* node;
    vfs_node_t* parent;
    
    uint32_t hash;
    time_t timestamp;
    
    vfs_dentry_t* next;
    vfs_dentry_t* hash_next;
};

// Open file descriptor
struct vfs_file {
    vfs_node_t* node;           // Associated inode
    uint32_t flags;             // Open flags
    off_t offset;               // Current position
    uint32_t ref_count;         // Reference count
    
    void* private_data;         // Filesystem-specific data
    
    // File descriptor table entry
    uint32_t fd;
    struct process* owner;      // Owning process
    
    vfs_file_t* next;
};

// Mount point
struct vfs_mount {
    char source[MANIFOLD_MAX_PATH];     // Device or source
    char target[MANIFOLD_MAX_PATH];     // Mount point path
    char fstype[32];                    // Filesystem type
    uint32_t flags;                     // Mount flags
    
    vfs_filesystem_t* fs;               // Filesystem driver
    vfs_node_t* root;                   // Root node of mounted FS
    vfs_node_t* mount_point;            // Node where mounted
    
    void* fs_data;                      // Filesystem-specific data
    void* device;                       // Block device
    
    // Statistics
    uint64_t total_blocks;
    uint64_t free_blocks;
    uint64_t total_inodes;
    uint64_t free_inodes;
    
    vfs_mount_t* next;
};

// Filesystem type
struct vfs_filesystem {
    char name[32];
    uint32_t flags;
    vfs_operations_t* ops;
    
    int (*init)(void);
    void (*cleanup)(void);
    
    vfs_filesystem_t* next;
};

// Path lookup context
typedef struct {
    const char* path;
    vfs_node_t* root;
    vfs_node_t* cwd;
    uint32_t flags;
    uint32_t uid;
    uint32_t gid;
    int symlink_depth;
} path_context_t;

// =============================================================================
// Global VFS State
// =============================================================================

typedef struct {
    vfs_mount_t* mounts;
    vfs_filesystem_t* filesystems;
    vfs_node_t* root_node;
    
    // Node cache
    vfs_node_t* node_cache[1024];
    vfs_node_t* lru_head;
    vfs_node_t* lru_tail;
    uint32_t cached_nodes;
    
    // Dentry cache
    vfs_dentry_t* dentry_cache[1024];
    uint32_t cached_dentries;
    
    // Statistics
    uint64_t lookups;
    uint64_t cache_hits;
    uint64_t cache_misses;
} vfs_state_t;

// =============================================================================
// Function Prototypes
// =============================================================================

// Initialization
int manifold_init(void);
void manifold_shutdown(void);

// Filesystem registration
int manifold_register_filesystem(vfs_filesystem_t* fs);
int manifold_unregister_filesystem(const char* name);
vfs_filesystem_t* manifold_find_filesystem(const char* name);

// Mount operations
int manifold_mount(const char* source, const char* target, const char* fstype,
                  uint32_t flags, void* data);
int manifold_unmount(const char* target);
int manifold_remount(const char* target, uint32_t flags);
vfs_mount_t* manifold_find_mount(const char* path);

// Path operations
vfs_node_t* manifold_lookup(const char* path);
vfs_node_t* manifold_lookup_parent(const char* path, char* basename);
int manifold_resolve_path(const char* path, char* resolved, size_t size);

// File operations
int manifold_open(const char* path, uint32_t flags, uint32_t mode);
int manifold_close(int fd);
ssize_t manifold_read(int fd, void* buffer, size_t size);
ssize_t manifold_write(int fd, const void* buffer, size_t size);
off_t manifold_lseek(int fd, off_t offset, int whence);
int manifold_ioctl(int fd, uint32_t cmd, void* arg);

// Directory operations
int manifold_mkdir(const char* path, uint32_t mode);
int manifold_rmdir(const char* path);
int manifold_readdir(int fd, vfs_dirent_t* dirent);

// File management
int manifold_create(const char* path, uint32_t mode);
int manifold_unlink(const char* path);
int manifold_rename(const char* old_path, const char* new_path);
int manifold_link(const char* target, const char* link_path);
int manifold_symlink(const char* target, const char* link_path);
int manifold_readlink(const char* path, char* buffer, size_t size);

// Attribute operations
int manifold_stat(const char* path, vfs_stat_t* stat);
int manifold_fstat(int fd, vfs_stat_t* stat);
int manifold_lstat(const char* path, vfs_stat_t* stat);
int manifold_chmod(const char* path, uint32_t mode);
int manifold_fchmod(int fd, uint32_t mode);
int manifold_chown(const char* path, uint32_t uid, uint32_t gid);
int manifold_fchown(int fd, uint32_t uid, uint32_t gid);

// Extended attributes
int manifold_getxattr(const char* path, const char* name, void* value, size_t size);
int manifold_setxattr(const char* path, const char* name, const void* value,
                     size_t size, int flags);
int manifold_listxattr(const char* path, char* list, size_t size);
int manifold_removexattr(const char* path, const char* name);

// Node management
vfs_node_t* manifold_alloc_node(void);
void manifold_free_node(vfs_node_t* node);
void manifold_ref_node(vfs_node_t* node);
void manifold_unref_node(vfs_node_t* node);

// Cache management
vfs_node_t* manifold_cache_lookup(vfs_mount_t* mount, uint64_t ino);
void manifold_cache_insert(vfs_node_t* node);
void manifold_cache_remove(vfs_node_t* node);
void manifold_cache_evict(uint32_t count);

// Dentry cache
vfs_dentry_t* manifold_dentry_lookup(vfs_node_t* parent, const char* name);
void manifold_dentry_add(vfs_node_t* parent, const char* name, vfs_node_t* node);
void manifold_dentry_remove(vfs_node_t* parent, const char* name);
void manifold_dentry_invalidate(vfs_node_t* parent);

// Permission checking
bool manifold_check_permission(vfs_node_t* node, uint32_t uid, uint32_t gid,
                              uint32_t permission);
bool manifold_can_read(vfs_node_t* node, uint32_t uid, uint32_t gid);
bool manifold_can_write(vfs_node_t* node, uint32_t uid, uint32_t gid);
bool manifold_can_execute(vfs_node_t* node, uint32_t uid, uint32_t gid);

// Helper functions
uint32_t manifold_hash_path(const char* path);
uint32_t manifold_hash_name(const char* name);
int manifold_split_path(const char* path, char* parent, char* name);
const char* manifold_basename(const char* path);
const char* manifold_dirname(const char* path);

#endif /* MANIFOLD_H */
