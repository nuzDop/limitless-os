/*
 * FAT32 Filesystem Driver Header
 * File Allocation Table filesystem definitions
 */

#ifndef FAT32_H
#define FAT32_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "../resonance.h"

// =============================================================================
// FAT32 Constants
// =============================================================================

#define MAX_FAT32_FILESYSTEMS   16

// FAT32 Cluster Values
#define FAT32_CLUSTER_FREE      0x00000000
#define FAT32_CLUSTER_RESERVED  0x0FFFFFF0
#define FAT32_CLUSTER_BAD       0x0FFFFFF7
#define FAT32_CLUSTER_EOC       0x0FFFFFF8  // End of chain marker

// Directory Entry Attributes
#define FAT32_ATTR_READ_ONLY    0x01
#define FAT32_ATTR_HIDDEN       0x02
#define FAT32_ATTR_SYSTEM       0x04
#define FAT32_ATTR_VOLUME_ID    0x08
#define FAT32_ATTR_DIRECTORY    0x10
#define FAT32_ATTR_ARCHIVE      0x20
#define FAT32_ATTR_LONG_NAME    (FAT32_ATTR_READ_ONLY | FAT32_ATTR_HIDDEN | \
                                FAT32_ATTR_SYSTEM | FAT32_ATTR_VOLUME_ID)

// Long filename constants
#define FAT32_LFN_SEQ_MASK      0x1F
#define FAT32_LFN_LAST          0x40
#define FAT32_LFN_CHARS_PER_ENTRY 13

// =============================================================================
// FAT32 Data Structures
// =============================================================================

// Boot Sector / BPB (BIOS Parameter Block)
typedef struct __attribute__((packed)) {
    uint8_t  jump_boot[3];
    char     oem_name[8];
    uint16_t bytes_per_sector;
    uint8_t  sectors_per_cluster;
    uint16_t reserved_sectors;
    uint8_t  num_fats;
    uint16_t root_entries;
    uint16_t total_sectors_16;
    uint8_t  media_descriptor;
    uint16_t fat_size_16;
    uint16_t sectors_per_track;
    uint16_t num_heads;
    uint32_t hidden_sectors;
    uint32_t total_sectors_32;
    
    // FAT32 specific
    uint32_t fat_size_32;
    uint16_t ext_flags;
    uint16_t fs_version;
    uint32_t root_cluster;
    uint16_t fs_info_sector;
    uint16_t backup_boot_sector;
    uint8_t  reserved[12];
    uint8_t  drive_number;
    uint8_t  reserved1;
    uint8_t  boot_sig;
    uint32_t volume_id;
    char     volume_label[11];
    char     fs_type[8];
    uint8_t  boot_code[420];
    uint16_t boot_signature;  // 0xAA55
} fat32_boot_sector_t;

// FSInfo Structure
typedef struct __attribute__((packed)) {
    uint32_t signature1;       // 0x41615252
    uint8_t  reserved1[480];
    uint32_t signature2;       // 0x61417272
    uint32_t free_clusters;
    uint32_t next_free_cluster;
    uint8_t  reserved2[12];
    uint32_t signature3;       // 0xAA550000
} fat32_fsinfo_t;

// Directory Entry (Short Name)
typedef struct __attribute__((packed)) {
    char     name[11];         // 8.3 format
    uint8_t  attr;            // Attributes
    uint8_t  nt_reserved;     // Reserved for NT
    uint8_t  create_time_tenth;
    uint16_t create_time;
    uint16_t create_date;
    uint16_t last_access_date;
    uint16_t cluster_high;     // High 16 bits of cluster
    uint16_t write_time;
    uint16_t write_date;
    uint16_t cluster_low;      // Low 16 bits of cluster
    uint32_t file_size;
} fat32_dir_entry_t;

// Long Filename Entry
typedef struct __attribute__((packed)) {
    uint8_t  sequence;         // Sequence number
    uint16_t name_chars[5];    // Characters 1-5
    uint8_t  attr;            // Always 0x0F
    uint8_t  type;            // Always 0x00
    uint8_t  checksum;        // Checksum of short name
    uint16_t name_chars2[6];   // Characters 6-11
    uint16_t cluster;         // Always 0x0000
    uint16_t name_chars3[2];   // Characters 12-13
} fat32_lfn_entry_t;

// Time and Date Format
typedef struct {
    uint16_t second : 5;      // 0-29 (2-second resolution)
    uint16_t minute : 6;      // 0-59
    uint16_t hour   : 5;      // 0-23
} fat32_time_t;

typedef struct {
    uint16_t day    : 5;      // 1-31
    uint16_t month  : 4;      // 1-12
    uint16_t year   : 7;      // 0-127 (1980-2107)
} fat32_date_t;

// Block Device Interface
typedef struct {
    int (*read)(void* device, uint64_t lba, uint32_t sectors, void* buffer);
    int (*write)(void* device, uint64_t lba, uint32_t sectors, void* buffer);
    void* device_data;
} block_device_t;

// FAT32 Filesystem
typedef struct {
    block_device_t* block_device;
    uint64_t partition_start;
    bool readonly;
    
    // Boot sector
    fat32_boot_sector_t boot_sector;
    
    // Filesystem parameters
    uint32_t bytes_per_sector;
    uint32_t sectors_per_cluster;
    uint32_t reserved_sectors;
    uint32_t num_fats;
    uint32_t total_clusters;
    uint32_t root_cluster;
    
    // FAT and data region
    uint64_t fat_start_lba;
    uint64_t data_start_lba;
    
    // FSInfo
    uint32_t free_clusters;
    uint32_t next_free_cluster;
    
    spinlock_t lock;
} fat32_filesystem_t;

// Directory List Entry
typedef struct {
    char name[256];
    uint32_t size;
    uint8_t attr;
    uint16_t create_date;
    uint16_t create_time;
    uint16_t modify_date;
    uint16_t modify_time;
    uint32_t cluster;
} fat32_dir_list_t;

// =============================================================================
// Function Prototypes
// =============================================================================

fat32_filesystem_t* fat32_mount(block_device_t* device, uint64_t partition_start,
                                bool readonly);
void fat32_unmount(fat32_filesystem_t* fs);

int fat32_read_file(fat32_filesystem_t* fs, const char* path, void* buffer,
                   size_t offset, size_t length);
int fat32_write_file(fat32_filesystem_t* fs, const char* path, void* buffer,
                    size_t offset, size_t length);

int fat32_list_directory(fat32_filesystem_t* fs, const char* path,
                        fat32_dir_list_t* list, size_t max_entries);
int fat32_create_file(fat32_filesystem_t* fs, const char* path);
int fat32_create_directory(fat32_filesystem_t* fs, const char* path);
int fat32_delete_file(fat32_filesystem_t* fs, const char* path);
int fat32_rename(fat32_filesystem_t* fs, const char* old_path, const char* new_path);

int fat32_get_file_info(fat32_filesystem_t* fs, const char* path,
                       fat32_dir_entry_t* entry);
int fat32_set_file_attributes(fat32_filesystem_t* fs, const char* path, uint8_t attr);

uint32_t fat32_get_free_space(fat32_filesystem_t* fs);
int fat32_format(block_device_t* device, uint64_t partition_start,
                uint64_t partition_size, const char* label);

#endif /* FAT32_H */
