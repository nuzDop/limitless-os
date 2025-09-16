/*
 * FAT32 Filesystem Driver for Continuum Kernel
 * File Allocation Table filesystem implementation
 */

#include "fat32.h"
#include "../resonance.h"
#include "../../flux_memory.h"

// =============================================================================
// Global FAT32 State
// =============================================================================

static fat32_filesystem_t* g_fat32_filesystems[MAX_FAT32_FILESYSTEMS];
static uint32_t g_fat32_fs_count = 0;
static spinlock_t g_fat32_lock = SPINLOCK_INIT;

// =============================================================================
// Cluster Operations
// =============================================================================

static uint32_t fat32_get_next_cluster(fat32_filesystem_t* fs, uint32_t cluster) {
    uint32_t fat_offset = cluster * 4;
    uint32_t fat_sector = fs->fat_start_lba + (fat_offset / fs->bytes_per_sector);
    uint32_t fat_entry_offset = fat_offset % fs->bytes_per_sector;
    
    uint8_t* buffer = flux_allocate(NULL, fs->bytes_per_sector, FLUX_ALLOC_KERNEL);
    if (!buffer) {
        return 0;
    }
    
    if (fs->block_device->read(fs->block_device, fat_sector, 1, buffer) != 0) {
        flux_free(buffer);
        return 0;
    }
    
    uint32_t next_cluster = *(uint32_t*)(buffer + fat_entry_offset) & 0x0FFFFFFF;
    flux_free(buffer);
    
    return next_cluster;
}

static int fat32_set_next_cluster(fat32_filesystem_t* fs, uint32_t cluster,
                                  uint32_t next_cluster) {
    if (fs->readonly) {
        return -1;
    }
    
    uint32_t fat_offset = cluster * 4;
    uint32_t fat_sector = fs->fat_start_lba + (fat_offset / fs->bytes_per_sector);
    uint32_t fat_entry_offset = fat_offset % fs->bytes_per_sector;
    
    uint8_t* buffer = flux_allocate(NULL, fs->bytes_per_sector, FLUX_ALLOC_KERNEL);
    if (!buffer) {
        return -1;
    }
    
    if (fs->block_device->read(fs->block_device, fat_sector, 1, buffer) != 0) {
        flux_free(buffer);
        return -1;
    }
    
    uint32_t* entry = (uint32_t*)(buffer + fat_entry_offset);
    *entry = (*entry & 0xF0000000) | (next_cluster & 0x0FFFFFFF);
    
    int result = fs->block_device->write(fs->block_device, fat_sector, 1, buffer);
    flux_free(buffer);
    
    return result;
}

static uint32_t fat32_find_free_cluster(fat32_filesystem_t* fs) {
    uint8_t* buffer = flux_allocate(NULL, fs->bytes_per_sector, FLUX_ALLOC_KERNEL);
    if (!buffer) {
        return 0;
    }
    
    uint32_t total_clusters = fs->total_clusters;
    
    for (uint32_t cluster = 2; cluster < total_clusters; cluster++) {
        uint32_t fat_offset = cluster * 4;
        uint32_t fat_sector = fs->fat_start_lba + (fat_offset / fs->bytes_per_sector);
        uint32_t fat_entry_offset = fat_offset % fs->bytes_per_sector;
        
        if (fs->block_device->read(fs->block_device, fat_sector, 1, buffer) != 0) {
            continue;
        }
        
        uint32_t entry = *(uint32_t*)(buffer + fat_entry_offset) & 0x0FFFFFFF;
        if (entry == 0) {
            flux_free(buffer);
            return cluster;
        }
    }
    
    flux_free(buffer);
    return 0;
}

// =============================================================================
// Cluster Chain Operations
// =============================================================================

static uint64_t fat32_cluster_to_lba(fat32_filesystem_t* fs, uint32_t cluster) {
    return fs->data_start_lba + ((cluster - 2) * fs->sectors_per_cluster);
}

static int fat32_read_cluster(fat32_filesystem_t* fs, uint32_t cluster, void* buffer) {
    if (cluster < 2 || cluster >= 0x0FFFFFF7) {
        return -1;
    }
    
    uint64_t lba = fat32_cluster_to_lba(fs, cluster);
    return fs->block_device->read(fs->block_device, lba, fs->sectors_per_cluster, buffer);
}

static int fat32_write_cluster(fat32_filesystem_t* fs, uint32_t cluster, void* buffer) {
    if (fs->readonly) {
        return -1;
    }
    
    if (cluster < 2 || cluster >= 0x0FFFFFF7) {
        return -1;
    }
    
    uint64_t lba = fat32_cluster_to_lba(fs, cluster);
    return fs->block_device->write(fs->block_device, lba, fs->sectors_per_cluster, buffer);
}

// =============================================================================
// Directory Entry Operations
// =============================================================================

static void fat32_get_short_name(const char* long_name, char* short_name) {
    // Generate 8.3 short name from long name
    int i, j;
    
    // Clear short name
    for (i = 0; i < 11; i++) {
        short_name[i] = ' ';
    }
    
    // Copy base name
    for (i = 0, j = 0; i < 8 && long_name[j] && long_name[j] != '.'; j++) {
        char c = long_name[j];
        if (c >= 'a' && c <= 'z') {
            c = c - 'a' + 'A';  // Convert to uppercase
        }
        if ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')) {
            short_name[i++] = c;
        }
    }
    
    // Find extension
    const char* ext = NULL;
    for (int k = strlen(long_name) - 1; k >= 0; k--) {
        if (long_name[k] == '.') {
            ext = &long_name[k + 1];
            break;
        }
    }
    
    // Copy extension
    if (ext) {
        for (i = 0; i < 3 && ext[i]; i++) {
            char c = ext[i];
            if (c >= 'a' && c <= 'z') {
                c = c - 'a' + 'A';
            }
            short_name[8 + i] = c;
        }
    }
}

static bool fat32_compare_filename(fat32_dir_entry_t* entry, const char* name) {
    char filename[13];
    int i, j;
    
    // Build filename from 8.3 format
    for (i = 0, j = 0; i < 8 && entry->name[i] != ' '; i++) {
        filename[j++] = entry->name[i];
    }
    
    if (entry->name[8] != ' ') {
        filename[j++] = '.';
        for (i = 8; i < 11 && entry->name[i] != ' '; i++) {
            filename[j++] = entry->name[i];
        }
    }
    
    filename[j] = '\0';
    
    // Case-insensitive comparison
    for (i = 0; filename[i] && name[i]; i++) {
        char c1 = filename[i];
        char c2 = name[i];
        
        if (c1 >= 'A' && c1 <= 'Z') c1 = c1 - 'A' + 'a';
        if (c2 >= 'A' && c2 <= 'Z') c2 = c2 - 'A' + 'a';
        
        if (c1 != c2) {
            return false;
        }
    }
    
    return filename[i] == '\0' && name[i] == '\0';
}

// =============================================================================
// Directory Operations
// =============================================================================

static fat32_dir_entry_t* fat32_find_entry_in_directory(fat32_filesystem_t* fs,
                                                        uint32_t dir_cluster,
                                                        const char* name) {
    uint32_t cluster_size = fs->sectors_per_cluster * fs->bytes_per_sector;
    uint8_t* buffer = flux_allocate(NULL, cluster_size, FLUX_ALLOC_KERNEL);
    if (!buffer) {
        return NULL;
    }
    
    uint32_t current_cluster = dir_cluster;
    
    while (current_cluster >= 2 && current_cluster < 0x0FFFFFF7) {
        if (fat32_read_cluster(fs, current_cluster, buffer) != 0) {
            current_cluster = fat32_get_next_cluster(fs, current_cluster);
            continue;
        }
        
        fat32_dir_entry_t* entries = (fat32_dir_entry_t*)buffer;
        uint32_t entries_per_cluster = cluster_size / sizeof(fat32_dir_entry_t);
        
        for (uint32_t i = 0; i < entries_per_cluster; i++) {
            if (entries[i].name[0] == 0x00) {
                // End of directory
                flux_free(buffer);
                return NULL;
            }
            
            if (entries[i].name[0] == 0xE5) {
                // Deleted entry
                continue;
            }
            
            if (entries[i].attr == FAT32_ATTR_LONG_NAME) {
                // Long filename entry - skip for now
                continue;
            }
            
            if (fat32_compare_filename(&entries[i], name)) {
                // Found entry - allocate and return copy
                fat32_dir_entry_t* result = flux_allocate(NULL, sizeof(fat32_dir_entry_t),
                                                         FLUX_ALLOC_KERNEL);
                if (result) {
                    memcpy(result, &entries[i], sizeof(fat32_dir_entry_t));
                }
                flux_free(buffer);
                return result;
            }
        }
        
        current_cluster = fat32_get_next_cluster(fs, current_cluster);
    }
    
    flux_free(buffer);
    return NULL;
}

// =============================================================================
// Path Resolution
// =============================================================================

static uint32_t fat32_path_to_cluster(fat32_filesystem_t* fs, const char* path) {
    if (!path || path[0] != '/') {
        return 0;
    }
    
    uint32_t current_cluster = fs->root_cluster;
    
    if (path[1] == '\0') {
        return current_cluster;  // Root directory
    }
    
    char* path_copy = flux_allocate(NULL, strlen(path) + 1, FLUX_ALLOC_KERNEL);
    if (!path_copy) {
        return 0;
    }
    strcpy(path_copy, path);
    
    char* token = strtok(path_copy + 1, "/");
    
    while (token) {
        fat32_dir_entry_t* entry = fat32_find_entry_in_directory(fs, current_cluster, token);
        if (!entry) {
            flux_free(path_copy);
            return 0;
        }
        
        if (!(entry->attr & FAT32_ATTR_DIRECTORY)) {
            // Not a directory
            flux_free(entry);
            flux_free(path_copy);
            return 0;
        }
        
        current_cluster = ((uint32_t)entry->cluster_high << 16) | entry->cluster_low;
        flux_free(entry);
        
        token = strtok(NULL, "/");
    }
    
    flux_free(path_copy);
    return current_cluster;
}

// =============================================================================
// File Operations
// =============================================================================

int fat32_read_file(fat32_filesystem_t* fs, const char* path, void* buffer,
                   size_t offset, size_t length) {
    // Extract directory and filename
    char* path_copy = flux_allocate(NULL, strlen(path) + 1, FLUX_ALLOC_KERNEL);
    if (!path_copy) {
        return -1;
    }
    strcpy(path_copy, path);
    
    char* last_slash = NULL;
    for (char* p = path_copy; *p; p++) {
        if (*p == '/') {
            last_slash = p;
        }
    }
    
    uint32_t dir_cluster;
    char* filename;
    
    if (last_slash) {
        *last_slash = '\0';
        dir_cluster = fat32_path_to_cluster(fs, path_copy);
        filename = last_slash + 1;
    } else {
        flux_free(path_copy);
        return -1;
    }
    
    if (dir_cluster == 0) {
        flux_free(path_copy);
        return -1;
    }
    
    // Find file entry
    fat32_dir_entry_t* entry = fat32_find_entry_in_directory(fs, dir_cluster, filename);
    flux_free(path_copy);
    
    if (!entry) {
        return -1;
    }
    
    if (entry->attr & FAT32_ATTR_DIRECTORY) {
        flux_free(entry);
        return -1;
    }
    
    uint32_t file_size = entry->file_size;
    uint32_t first_cluster = ((uint32_t)entry->cluster_high << 16) | entry->cluster_low;
    flux_free(entry);
    
    if (offset >= file_size) {
        return 0;
    }
    
    if (offset + length > file_size) {
        length = file_size - offset;
    }
    
    // Read file data
    uint32_t cluster_size = fs->sectors_per_cluster * fs->bytes_per_sector;
    uint8_t* cluster_buffer = flux_allocate(NULL, cluster_size, FLUX_ALLOC_KERNEL);
    if (!cluster_buffer) {
        return -1;
    }
    
    uint32_t current_cluster = first_cluster;
    uint32_t cluster_offset = offset / cluster_size;
    uint32_t byte_offset = offset % cluster_size;
    size_t bytes_read = 0;
    uint8_t* dest = (uint8_t*)buffer;
    
    // Skip to starting cluster
    for (uint32_t i = 0; i < cluster_offset && current_cluster < 0x0FFFFFF7; i++) {
        current_cluster = fat32_get_next_cluster(fs, current_cluster);
    }
    
    // Read data
    while (bytes_read < length && current_cluster >= 2 && current_cluster < 0x0FFFFFF7) {
        if (fat32_read_cluster(fs, current_cluster, cluster_buffer) != 0) {
            flux_free(cluster_buffer);
            return bytes_read;
        }
        
        size_t copy_offset = (bytes_read == 0) ? byte_offset : 0;
        size_t copy_size = cluster_size - copy_offset;
        
        if (bytes_read + copy_size > length) {
            copy_size = length - bytes_read;
        }
        
        memcpy(dest + bytes_read, cluster_buffer + copy_offset, copy_size);
        bytes_read += copy_size;
        
        current_cluster = fat32_get_next_cluster(fs, current_cluster);
    }
    
    flux_free(cluster_buffer);
    return bytes_read;
}

// =============================================================================
// Boot Sector Operations
// =============================================================================

static int fat32_read_boot_sector(fat32_filesystem_t* fs) {
    uint8_t buffer[512];
    
    // Read boot sector
    if (fs->block_device->read(fs->block_device, fs->partition_start, 1, buffer) != 0) {
        return -1;
    }
    
    fat32_boot_sector_t* boot = (fat32_boot_sector_t*)buffer;
    
    // Verify signature
    if (boot->boot_signature != 0xAA55) {
        return -1;
    }
    
    // Copy boot sector
    memcpy(&fs->boot_sector, boot, sizeof(fat32_boot_sector_t));
    
    // Calculate filesystem parameters
    fs->bytes_per_sector = boot->bytes_per_sector;
    fs->sectors_per_cluster = boot->sectors_per_cluster;
    fs->reserved_sectors = boot->reserved_sectors;
    fs->num_fats = boot->num_fats;
    
    uint32_t root_dir_sectors = ((boot->root_entries * 32) + 
                                 (boot->bytes_per_sector - 1)) / boot->bytes_per_sector;
    
    uint32_t fat_size = (boot->fat_size_16 != 0) ? boot->fat_size_16 : boot->fat_size_32;
    uint32_t total_sectors = (boot->total_sectors_16 != 0) ? 
                             boot->total_sectors_16 : boot->total_sectors_32;
    
    uint32_t data_sectors = total_sectors - (boot->reserved_sectors + 
                                             (boot->num_fats * fat_size) + root_dir_sectors);
    
    fs->total_clusters = data_sectors / boot->sectors_per_cluster;
    fs->fat_start_lba = fs->partition_start + boot->reserved_sectors;
    fs->data_start_lba = fs->fat_start_lba + (boot->num_fats * fat_size) + root_dir_sectors;
    fs->root_cluster = boot->root_cluster;
    
    return 0;
}

// =============================================================================
// Filesystem Mount
// =============================================================================

fat32_filesystem_t* fat32_mount(block_device_t* device, uint64_t partition_start,
                               bool readonly) {
    fat32_filesystem_t* fs = flux_allocate(NULL, sizeof(fat32_filesystem_t),
                                          FLUX_ALLOC_KERNEL | FLUX_ALLOC_ZERO);
    if (!fs) {
        return NULL;
    }
    
    fs->block_device = device;
    fs->partition_start = partition_start;
    fs->readonly = readonly;
    spinlock_init(&fs->lock);
    
    // Read boot sector
    if (fat32_read_boot_sector(fs) != 0) {
        flux_free(fs);
        return NULL;
    }
    
    // Verify FAT32
    if (fs->total_clusters < 65525) {
        // Not FAT32 (FAT12 or FAT16)
        flux_free(fs);
        return NULL;
    }
    
    // Add to global list
    spinlock_acquire(&g_fat32_lock);
    g_fat32_filesystems[g_fat32_fs_count++] = fs;
    spinlock_release(&g_fat32_lock);
    
    return fs;
}

void fat32_unmount(fat32_filesystem_t* fs) {
    if (!fs) {
        return;
    }
    
    // Remove from global list
    spinlock_acquire(&g_fat32_lock);
    for (uint32_t i = 0; i < g_fat32_fs_count; i++) {
        if (g_fat32_filesystems[i] == fs) {
            g_fat32_filesystems[i] = g_fat32_filesystems[--g_fat32_fs_count];
            break;
        }
    }
    spinlock_release(&g_fat32_lock);
    
    flux_free(fs);
}
