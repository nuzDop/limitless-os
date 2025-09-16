/*
 * EXT4 Filesystem Driver for Continuum Kernel
 * Fourth Extended Filesystem implementation
 */

#include "ext4.h"
#include "../resonance.h"
#include "../../flux_memory.h"

// =============================================================================
// Global EXT4 State
// =============================================================================

static ext4_filesystem_t* g_ext4_filesystems[MAX_EXT4_FILESYSTEMS];
static uint32_t g_ext4_fs_count = 0;
static spinlock_t g_ext4_lock = SPINLOCK_INIT;

// =============================================================================
// Block I/O Operations
// =============================================================================

static int ext4_read_block(ext4_filesystem_t* fs, uint64_t block_num, void* buffer) {
    uint64_t lba = fs->partition_start + (block_num * fs->block_size / 512);
    uint32_t sectors = fs->block_size / 512;
    
    return fs->block_device->read(fs->block_device, lba, sectors, buffer);
}

static int ext4_write_block(ext4_filesystem_t* fs, uint64_t block_num, void* buffer) {
    if (fs->readonly) {
        return -1;
    }
    
    uint64_t lba = fs->partition_start + (block_num * fs->block_size / 512);
    uint32_t sectors = fs->block_size / 512;
    
    return fs->block_device->write(fs->block_device, lba, sectors, buffer);
}

// =============================================================================
// Superblock Operations
// =============================================================================

static int ext4_read_superblock(ext4_filesystem_t* fs) {
    uint8_t buffer[1024];
    
    // Read superblock at offset 1024
    uint64_t lba = fs->partition_start + 2;  // 1024 bytes = 2 sectors
    if (fs->block_device->read(fs->block_device, lba, 2, buffer) != 0) {
        return -1;
    }
    
    ext4_superblock_t* sb = (ext4_superblock_t*)buffer;
    
    // Verify magic number
    if (sb->s_magic != EXT4_SUPER_MAGIC) {
        return -1;
    }
    
    // Copy superblock
    memcpy(&fs->superblock, sb, sizeof(ext4_superblock_t));
    
    // Calculate filesystem parameters
    fs->block_size = 1024 << sb->s_log_block_size;
    fs->blocks_per_group = sb->s_blocks_per_group;
    fs->inodes_per_group = sb->s_inodes_per_group;
    fs->inode_size = (sb->s_rev_level >= 1) ? sb->s_inode_size : 128;
    
    // Calculate group count
    uint64_t total_blocks = ((uint64_t)sb->s_blocks_count_hi << 32) | sb->s_blocks_count_lo;
    fs->group_count = (total_blocks + fs->blocks_per_group - 1) / fs->blocks_per_group;
    
    // Check features
    if (sb->s_feature_incompat & EXT4_FEATURE_INCOMPAT_64BIT) {
        fs->has_64bit = true;
    }
    
    if (sb->s_feature_incompat & EXT4_FEATURE_INCOMPAT_EXTENTS) {
        fs->has_extents = true;
    }
    
    if (sb->s_feature_ro_compat & EXT4_FEATURE_RO_COMPAT_HUGE_FILE) {
        fs->has_huge_files = true;
    }
    
    return 0;
}

// =============================================================================
// Block Group Descriptor Operations
// =============================================================================

static int ext4_read_group_descriptors(ext4_filesystem_t* fs) {
    // Calculate size of group descriptor table
    size_t gdt_size = fs->group_count * sizeof(ext4_group_desc_t);
    
    // Allocate memory for group descriptors
    fs->group_descs = flux_allocate(NULL, gdt_size, FLUX_ALLOC_KERNEL);
    if (!fs->group_descs) {
        return -1;
    }
    
    // Group descriptors start at block after superblock
    uint64_t gdt_block = (fs->block_size == 1024) ? 2 : 1;
    
    // Read group descriptors
    size_t blocks_to_read = (gdt_size + fs->block_size - 1) / fs->block_size;
    uint8_t* buffer = flux_allocate(NULL, blocks_to_read * fs->block_size, FLUX_ALLOC_KERNEL);
    if (!buffer) {
        flux_free(fs->group_descs);
        return -1;
    }
    
    for (size_t i = 0; i < blocks_to_read; i++) {
        if (ext4_read_block(fs, gdt_block + i, buffer + i * fs->block_size) != 0) {
            flux_free(buffer);
            flux_free(fs->group_descs);
            return -1;
        }
    }
    
    memcpy(fs->group_descs, buffer, gdt_size);
    flux_free(buffer);
    
    return 0;
}

// =============================================================================
// Inode Operations
// =============================================================================

static int ext4_read_inode(ext4_filesystem_t* fs, uint32_t inode_num, ext4_inode_t* inode) {
    if (inode_num == 0 || inode_num > fs->superblock.s_inodes_count) {
        return -1;
    }
    
    // Calculate group and index
    uint32_t group = (inode_num - 1) / fs->inodes_per_group;
    uint32_t index = (inode_num - 1) % fs->inodes_per_group;
    
    // Get inode table block
    ext4_group_desc_t* gd = &fs->group_descs[group];
    uint64_t inode_table = ((uint64_t)gd->bg_inode_table_hi << 32) | gd->bg_inode_table_lo;
    
    // Calculate block and offset
    uint32_t block_offset = (index * fs->inode_size) / fs->block_size;
    uint32_t block_index = (index * fs->inode_size) % fs->block_size;
    
    // Read inode block
    uint8_t* buffer = flux_allocate(NULL, fs->block_size, FLUX_ALLOC_KERNEL);
    if (!buffer) {
        return -1;
    }
    
    if (ext4_read_block(fs, inode_table + block_offset, buffer) != 0) {
        flux_free(buffer);
        return -1;
    }
    
    memcpy(inode, buffer + block_index, sizeof(ext4_inode_t));
    flux_free(buffer);
    
    return 0;
}

// =============================================================================
// Extent Tree Operations
// =============================================================================

static uint64_t ext4_extent_to_block(ext4_filesystem_t* fs, ext4_extent_t* extent,
                                     uint32_t logical_block) {
    if (logical_block < extent->ee_block ||
        logical_block >= extent->ee_block + extent->ee_len) {
        return 0;
    }
    
    uint64_t physical = ((uint64_t)extent->ee_start_hi << 32) | extent->ee_start_lo;
    return physical + (logical_block - extent->ee_block);
}

static uint64_t ext4_get_block_from_extent(ext4_filesystem_t* fs, ext4_inode_t* inode,
                                          uint32_t logical_block) {
    ext4_extent_header_t* header = (ext4_extent_header_t*)inode->i_block;
    
    if (header->eh_magic != EXT4_EXTENT_MAGIC) {
        return 0;
    }
    
    if (header->eh_depth == 0) {
        // Leaf node
        ext4_extent_t* extent = (ext4_extent_t*)(header + 1);
        for (int i = 0; i < header->eh_entries; i++) {
            if (logical_block >= extent[i].ee_block &&
                logical_block < extent[i].ee_block + extent[i].ee_len) {
                return ext4_extent_to_block(fs, &extent[i], logical_block);
            }
        }
    } else {
        // Index node - need to traverse tree
        // Simplified - full implementation would traverse index nodes
    }
    
    return 0;
}

// =============================================================================
// Directory Operations
// =============================================================================

static int ext4_read_dir_block(ext4_filesystem_t* fs, ext4_inode_t* dir_inode,
                              uint32_t block_num, void* buffer) {
    uint64_t physical_block;
    
    if (fs->has_extents && (dir_inode->i_flags & EXT4_EXTENTS_FL)) {
        physical_block = ext4_get_block_from_extent(fs, dir_inode, block_num);
    } else {
        // Traditional block mapping
        if (block_num < 12) {
            physical_block = dir_inode->i_block[block_num];
        } else {
            // Indirect blocks - simplified
            return -1;
        }
    }
    
    if (physical_block == 0) {
        return -1;
    }
    
    return ext4_read_block(fs, physical_block, buffer);
}

static ext4_dir_entry_t* ext4_find_dir_entry(ext4_filesystem_t* fs, ext4_inode_t* dir_inode,
                                            const char* name) {
    uint8_t* buffer = flux_allocate(NULL, fs->block_size, FLUX_ALLOC_KERNEL);
    if (!buffer) {
        return NULL;
    }
    
    uint32_t size = dir_inode->i_size_lo;
    uint32_t blocks = (size + fs->block_size - 1) / fs->block_size;
    
    for (uint32_t i = 0; i < blocks; i++) {
        if (ext4_read_dir_block(fs, dir_inode, i, buffer) != 0) {
            continue;
        }
        
        uint32_t offset = 0;
        while (offset < fs->block_size) {
            ext4_dir_entry_t* entry = (ext4_dir_entry_t*)(buffer + offset);
            
            if (entry->rec_len == 0) {
                break;
            }
            
            if (entry->name_len == strlen(name) &&
                memcmp(entry->name, name, entry->name_len) == 0) {
                // Found entry - allocate and return copy
                ext4_dir_entry_t* result = flux_allocate(NULL, entry->rec_len,
                                                        FLUX_ALLOC_KERNEL);
                if (result) {
                    memcpy(result, entry, entry->rec_len);
                }
                flux_free(buffer);
                return result;
            }
            
            offset += entry->rec_len;
        }
    }
    
    flux_free(buffer);
    return NULL;
}

// =============================================================================
// Path Resolution
// =============================================================================

static uint32_t ext4_path_to_inode(ext4_filesystem_t* fs, const char* path) {
    if (!path || path[0] != '/') {
        return 0;
    }
    
    uint32_t current_inode = EXT4_ROOT_INO;
    
    if (path[1] == '\0') {
        return current_inode;  // Root directory
    }
    
    char* path_copy = flux_allocate(NULL, strlen(path) + 1, FLUX_ALLOC_KERNEL);
    if (!path_copy) {
        return 0;
    }
    strcpy(path_copy, path);
    
    char* token = strtok(path_copy + 1, "/");
    
    while (token) {
        // Read current inode
        ext4_inode_t inode;
        if (ext4_read_inode(fs, current_inode, &inode) != 0) {
            flux_free(path_copy);
            return 0;
        }
        
        // Check if directory
        if ((inode.i_mode & EXT4_S_IFMT) != EXT4_S_IFDIR) {
            flux_free(path_copy);
            return 0;
        }
        
        // Find entry in directory
        ext4_dir_entry_t* entry = ext4_find_dir_entry(fs, &inode, token);
        if (!entry) {
            flux_free(path_copy);
            return 0;
        }
        
        current_inode = entry->inode;
        flux_free(entry);
        
        token = strtok(NULL, "/");
    }
    
    flux_free(path_copy);
    return current_inode;
}

// =============================================================================
// File Operations
// =============================================================================

int ext4_read_file(ext4_filesystem_t* fs, const char* path, void* buffer, 
                  size_t offset, size_t length) {
    // Get inode number
    uint32_t inode_num = ext4_path_to_inode(fs, path);
    if (inode_num == 0) {
        return -1;
    }
    
    // Read inode
    ext4_inode_t inode;
    if (ext4_read_inode(fs, inode_num, &inode) != 0) {
        return -1;
    }
    
    // Check if regular file
    if ((inode.i_mode & EXT4_S_IFMT) != EXT4_S_IFREG) {
        return -1;
    }
    
    // Get file size
    uint64_t file_size = inode.i_size_lo;
    if (fs->has_huge_files) {
        file_size |= ((uint64_t)inode.i_size_high << 32);
    }
    
    if (offset >= file_size) {
        return 0;
    }
    
    if (offset + length > file_size) {
        length = file_size - offset;
    }
    
    // Read data blocks
    uint32_t start_block = offset / fs->block_size;
    uint32_t end_block = (offset + length - 1) / fs->block_size;
    uint32_t block_offset = offset % fs->block_size;
    
    uint8_t* block_buffer = flux_allocate(NULL, fs->block_size, FLUX_ALLOC_KERNEL);
    if (!block_buffer) {
        return -1;
    }
    
    size_t bytes_read = 0;
    uint8_t* dest = (uint8_t*)buffer;
    
    for (uint32_t block = start_block; block <= end_block; block++) {
        uint64_t physical_block;
        
        if (fs->has_extents && (inode.i_flags & EXT4_EXTENTS_FL)) {
            physical_block = ext4_get_block_from_extent(fs, &inode, block);
        } else {
            if (block < 12) {
                physical_block = inode.i_block[block];
            } else {
                // Indirect blocks - simplified
                flux_free(block_buffer);
                return bytes_read;
            }
        }
        
        if (physical_block == 0) {
            // Sparse file
            memset(block_buffer, 0, fs->block_size);
        } else {
            if (ext4_read_block(fs, physical_block, block_buffer) != 0) {
                flux_free(block_buffer);
                return -1;
            }
        }
        
        // Copy data
        size_t copy_offset = (block == start_block) ? block_offset : 0;
        size_t copy_size = fs->block_size - copy_offset;
        
        if (bytes_read + copy_size > length) {
            copy_size = length - bytes_read;
        }
        
        memcpy(dest + bytes_read, block_buffer + copy_offset, copy_size);
        bytes_read += copy_size;
    }
    
    flux_free(block_buffer);
    return bytes_read;
}

// =============================================================================
// Directory Listing
// =============================================================================

int ext4_list_directory(ext4_filesystem_t* fs, const char* path,
                       ext4_dir_list_t* list, size_t max_entries) {
    // Get directory inode
    uint32_t inode_num = ext4_path_to_inode(fs, path);
    if (inode_num == 0) {
        return -1;
    }
    
    ext4_inode_t inode;
    if (ext4_read_inode(fs, inode_num, &inode) != 0) {
        return -1;
    }
    
    // Check if directory
    if ((inode.i_mode & EXT4_S_IFMT) != EXT4_S_IFDIR) {
        return -1;
    }
    
    uint8_t* buffer = flux_allocate(NULL, fs->block_size, FLUX_ALLOC_KERNEL);
    if (!buffer) {
        return -1;
    }
    
    uint32_t size = inode.i_size_lo;
    uint32_t blocks = (size + fs->block_size - 1) / fs->block_size;
    size_t entry_count = 0;
    
    for (uint32_t i = 0; i < blocks && entry_count < max_entries; i++) {
        if (ext4_read_dir_block(fs, &inode, i, buffer) != 0) {
            continue;
        }
        
        uint32_t offset = 0;
        while (offset < fs->block_size && entry_count < max_entries) {
            ext4_dir_entry_t* entry = (ext4_dir_entry_t*)(buffer + offset);
            
            if (entry->rec_len == 0) {
                break;
            }
            
            if (entry->inode != 0) {
                // Copy entry info
                list[entry_count].inode = entry->inode;
                list[entry_count].type = entry->file_type;
                list[entry_count].name_len = entry->name_len;
                memcpy(list[entry_count].name, entry->name, entry->name_len);
                list[entry_count].name[entry->name_len] = '\0';
                entry_count++;
            }
            
            offset += entry->rec_len;
        }
    }
    
    flux_free(buffer);
    return entry_count;
}

// =============================================================================
// Filesystem Mount
// =============================================================================

ext4_filesystem_t* ext4_mount(block_device_t* device, uint64_t partition_start,
                             bool readonly) {
    ext4_filesystem_t* fs = flux_allocate(NULL, sizeof(ext4_filesystem_t),
                                         FLUX_ALLOC_KERNEL | FLUX_ALLOC_ZERO);
    if (!fs) {
        return NULL;
    }
    
    fs->block_device = device;
    fs->partition_start = partition_start;
    fs->readonly = readonly;
    spinlock_init(&fs->lock);
    
    // Read superblock
    if (ext4_read_superblock(fs) != 0) {
        flux_free(fs);
        return NULL;
    }
    
    // Read group descriptors
    if (ext4_read_group_descriptors(fs) != 0) {
        flux_free(fs);
        return NULL;
    }
    
    // Add to global list
    spinlock_acquire(&g_ext4_lock);
    g_ext4_filesystems[g_ext4_fs_count++] = fs;
    spinlock_release(&g_ext4_lock);
    
    return fs;
}

void ext4_unmount(ext4_filesystem_t* fs) {
    if (!fs) {
        return;
    }
    
    // Remove from global list
    spinlock_acquire(&g_ext4_lock);
    for (uint32_t i = 0; i < g_ext4_fs_count; i++) {
        if (g_ext4_filesystems[i] == fs) {
            g_ext4_filesystems[i] = g_ext4_filesystems[--g_ext4_fs_count];
            break;
        }
    }
    spinlock_release(&g_ext4_lock);
    
    // Free resources
    if (fs->group_descs) {
        flux_free(fs->group_descs);
    }
    
    flux_free(fs);
}
