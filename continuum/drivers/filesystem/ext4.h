/*
 * EXT4 Filesystem Driver Header
 * Fourth Extended Filesystem definitions
 */

#ifndef EXT4_H
#define EXT4_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "../resonance.h"

// =============================================================================
// EXT4 Constants
// =============================================================================

#define MAX_EXT4_FILESYSTEMS    16
#define EXT4_SUPER_MAGIC       0xEF53
#define EXT4_ROOT_INO          2
#define EXT4_EXTENT_MAGIC      0xF30A

// Filesystem Features
#define EXT4_FEATURE_INCOMPAT_COMPRESSION   0x0001
#define EXT4_FEATURE_INCOMPAT_FILETYPE      0x0002
#define EXT4_FEATURE_INCOMPAT_RECOVER       0x0004
#define EXT4_FEATURE_INCOMPAT_JOURNAL_DEV   0x0008
#define EXT4_FEATURE_INCOMPAT_META_BG       0x0010
#define EXT4_FEATURE_INCOMPAT_EXTENTS       0x0040
#define EXT4_FEATURE_INCOMPAT_64BIT         0x0080
#define EXT4_FEATURE_INCOMPAT_MMP           0x0100
#define EXT4_FEATURE_INCOMPAT_FLEX_BG       0x0200
#define EXT4_FEATURE_INCOMPAT_EA_INODE      0x0400
#define EXT4_FEATURE_INCOMPAT_DIRDATA       0x1000
#define EXT4_FEATURE_INCOMPAT_CSUM_SEED     0x2000
#define EXT4_FEATURE_INCOMPAT_LARGEDIR      0x4000
#define EXT4_FEATURE_INCOMPAT_INLINE_DATA   0x8000
#define EXT4_FEATURE_INCOMPAT_ENCRYPT       0x10000

#define EXT4_FEATURE_RO_COMPAT_SPARSE_SUPER 0x0001
#define EXT4_FEATURE_RO_COMPAT_LARGE_FILE   0x0002
#define EXT4_FEATURE_RO_COMPAT_BTREE_DIR    0x0004
#define EXT4_FEATURE_RO_COMPAT_HUGE_FILE    0x0008
#define EXT4_FEATURE_RO_COMPAT_GDT_CSUM     0x0010
#define EXT4_FEATURE_RO_COMPAT_DIR_NLINK    0x0020
#define EXT4_FEATURE_RO_COMPAT_EXTRA_ISIZE  0x0040
#define EXT4_FEATURE_RO_COMPAT_QUOTA        0x0100
#define EXT4_FEATURE_RO_COMPAT_BIGALLOC     0x0200
#define EXT4_FEATURE_RO_COMPAT_METADATA_CSUM 0x0400
#define EXT4_FEATURE_RO_COMPAT_READONLY     0x1000
#define EXT4_FEATURE_RO_COMPAT_PROJECT      0x2000

// File Types
#define EXT4_FT_UNKNOWN        0
#define EXT4_FT_REG_FILE       1
#define EXT4_FT_DIR            2
#define EXT4_FT_CHRDEV         3
#define EXT4_FT_BLKDEV         4
#define EXT4_FT_FIFO           5
#define EXT4_FT_SOCK           6
#define EXT4_FT_SYMLINK        7

// Inode Modes
#define EXT4_S_IFMT            0xF000
#define EXT4_S_IFSOCK          0xC000
#define EXT4_S_IFLNK           0xA000
#define EXT4_S_IFREG           0x8000
#define EXT4_S_IFBLK           0x6000
#define EXT4_S_IFDIR           0x4000
#define EXT4_S_IFCHR           0x2000
#define EXT4_S_IFIFO           0x1000

// Inode Flags
#define EXT4_SECRM_FL          0x00000001
#define EXT4_UNRM_FL           0x00000002
#define EXT4_COMPR_FL          0x00000004
#define EXT4_SYNC_FL           0x00000008
#define EXT4_IMMUTABLE_FL      0x00000010
#define EXT4_APPEND_FL         0x00000020
#define EXT4_NODUMP_FL         0x00000040
#define EXT4_NOATIME_FL        0x00000080
#define EXT4_DIRTY_FL          0x00000100
#define EXT4_COMPRBLK_FL       0x00000200
#define EXT4_NOCOMPR_FL        0x00000400
#define EXT4_ENCRYPT_FL        0x00000800
#define EXT4_INDEX_FL          0x00001000
#define EXT4_IMAGIC_FL         0x00002000
#define EXT4_JOURNAL_DATA_FL   0x00004000
#define EXT4_NOTAIL_FL         0x00008000
#define EXT4_DIRSYNC_FL        0x00010000
#define EXT4_TOPDIR_FL         0x00020000
#define EXT4_HUGE_FILE_FL      0x00040000
#define EXT4_EXTENTS_FL        0x00080000
#define EXT4_EA_INODE_FL       0x00200000
#define EXT4_EOFBLOCKS_FL      0x00400000
#define EXT4_INLINE_DATA_FL    0x10000000
#define EXT4_PROJINHERIT_FL    0x20000000

// =============================================================================
// EXT4 Data Structures
// =============================================================================

// Superblock
typedef struct __attribute__((packed)) {
    uint32_t s_inodes_count;
    uint32_t s_blocks_count_lo;
    uint32_t s_r_blocks_count_lo;
    uint32_t s_free_blocks_count_lo;
    uint32_t s_free_inodes_count;
    uint32_t s_first_data_block;
    uint32_t s_log_block_size;
    uint32_t s_log_cluster_size;
    uint32_t s_blocks_per_group;
    uint32_t s_clusters_per_group;
    uint32_t s_inodes_per_group;
    uint32_t s_mtime;
    uint32_t s_wtime;
    uint16_t s_mnt_count;
    uint16_t s_max_mnt_count;
    uint16_t s_magic;
    uint16_t s_state;
    uint16_t s_errors;
    uint16_t s_minor_rev_level;
    uint32_t s_lastcheck;
    uint32_t s_checkinterval;
    uint32_t s_creator_os;
    uint32_t s_rev_level;
    uint16_t s_def_resuid;
    uint16_t s_def_resgid;
    uint32_t s_first_ino;
    uint16_t s_inode_size;
    uint16_t s_block_group_nr;
    uint32_t s_feature_compat;
    uint32_t s_feature_incompat;
    uint32_t s_feature_ro_compat;
    uint8_t  s_uuid[16];
    char     s_volume_name[16];
    char     s_last_mounted[64];
    uint32_t s_algorithm_usage_bitmap;
    uint8_t  s_prealloc_blocks;
    uint8_t  s_prealloc_dir_blocks;
    uint16_t s_reserved_gdt_blocks;
    uint8_t  s_journal_uuid[16];
    uint32_t s_journal_inum;
    uint32_t s_journal_dev;
    uint32_t s_last_orphan;
    uint32_t s_hash_seed[4];
    uint8_t  s_def_hash_version;
    uint8_t  s_jnl_backup_type;
    uint16_t s_desc_size;
    uint32_t s_default_mount_opts;
    uint32_t s_first_meta_bg;
    uint32_t s_mkfs_time;
    uint32_t s_jnl_blocks[17];
    uint32_t s_blocks_count_hi;
    uint32_t s_r_blocks_count_hi;
    uint32_t s_free_blocks_count_hi;
    uint16_t s_min_extra_isize;
    uint16_t s_want_extra_isize;
    uint32_t s_flags;
    uint16_t s_raid_stride;
    uint16_t s_mmp_interval;
    uint64_t s_mmp_block;
    uint32_t s_raid_stripe_width;
    uint8_t  s_log_groups_per_flex;
    uint8_t  s_checksum_type;
    uint16_t s_reserved_pad;
    uint64_t s_kbytes_written;
    uint32_t s_snapshot_inum;
    uint32_t s_snapshot_id;
    uint64_t s_snapshot_r_blocks_count;
    uint32_t s_snapshot_list;
    uint32_t s_error_count;
    uint32_t s_first_error_time;
    uint32_t s_first_error_ino;
    uint64_t s_first_error_block;
    uint8_t  s_first_error_func[32];
    uint32_t s_first_error_line;
    uint32_t s_last_error_time;
    uint32_t s_last_error_ino;
    uint32_t s_last_error_line;
    uint64_t s_last_error_block;
    uint8_t  s_last_error_func[32];
    uint8_t  s_mount_opts[64];
    uint32_t s_usr_quota_inum;
    uint32_t s_grp_quota_inum;
    uint32_t s_overhead_blocks;
    uint32_t s_backup_bgs[2];
    uint8_t  s_encrypt_algos[4];
    uint8_t  s_encrypt_pw_salt[16];
    uint32_t s_lpf_ino;
    uint32_t s_prj_quota_inum;
    uint32_t s_checksum_seed;
    uint32_t s_reserved[98];
    uint32_t s_checksum;
} ext4_superblock_t;

// Block Group Descriptor
typedef struct __attribute__((packed)) {
    uint32_t bg_block_bitmap_lo;
    uint32_t bg_inode_bitmap_lo;
    uint32_t bg_inode_table_lo;
    uint16_t bg_free_blocks_count_lo;
    uint16_t bg_free_inodes_count_lo;
    uint16_t bg_used_dirs_count_lo;
    uint16_t bg_flags;
    uint32_t bg_exclude_bitmap_lo;
    uint16_t bg_block_bitmap_csum_lo;
    uint16_t bg_inode_bitmap_csum_lo;
    uint16_t bg_itable_unused_lo;
    uint16_t bg_checksum;
    uint32_t bg_block_bitmap_hi;
    uint32_t bg_inode_bitmap_hi;
    uint32_t bg_inode_table_hi;
    uint16_t bg_free_blocks_count_hi;
    uint16_t bg_free_inodes_count_hi;
    uint16_t bg_used_dirs_count_hi;
    uint16_t bg_itable_unused_hi;
    uint32_t bg_exclude_bitmap_hi;
    uint16_t bg_block_bitmap_csum_hi;
    uint16_t bg_inode_bitmap_csum_hi;
    uint32_t bg_reserved;
} ext4_group_desc_t;

// Inode
typedef struct __attribute__((packed)) {
    uint16_t i_mode;
    uint16_t i_uid;
    uint32_t i_size_lo;
    uint32_t i_atime;
    uint32_t i_ctime;
    uint32_t i_mtime;
    uint32_t i_dtime;
    uint16_t i_gid;
    uint16_t i_links_count;
    uint32_t i_blocks_lo;
    uint32_t i_flags;
    uint32_t i_osd1;
    uint32_t i_block[15];
    uint32_t i_generation;
    uint32_t i_file_acl_lo;
    uint32_t i_size_high;
    uint32_t i_obso_faddr;
    uint32_t i_osd2[3];
    uint16_t i_extra_isize;
    uint16_t i_checksum_hi;
    uint32_t i_ctime_extra;
    uint32_t i_mtime_extra;
    uint32_t i_atime_extra;
    uint32_t i_crtime;
    uint32_t i_crtime_extra;
    uint32_t i_version_hi;
    uint32_t i_projid;
} ext4_inode_t;

// Directory Entry
typedef struct __attribute__((packed)) {
    uint32_t inode;
    uint16_t rec_len;
    uint8_t  name_len;
    uint8_t  file_type;
    char     name[];
} ext4_dir_entry_t;

// Extent Header
typedef struct __attribute__((packed)) {
    uint16_t eh_magic;
    uint16_t eh_entries;
    uint16_t eh_max;
    uint16_t eh_depth;
    uint32_t eh_generation;
} ext4_extent_header_t;

// Extent
typedef struct __attribute__((packed)) {
    uint32_t ee_block;
    uint16_t ee_len;
    uint16_t ee_start_hi;
    uint32_t ee_start_lo;
} ext4_extent_t;

// Extent Index
typedef struct __attribute__((packed)) {
    uint32_t ei_block;
    uint32_t ei_leaf_lo;
    uint16_t ei_leaf_hi;
    uint16_t ei_unused;
} ext4_extent_idx_t;

// Block Device Interface
typedef struct {
    int (*read)(void* device, uint64_t lba, uint32_t sectors, void* buffer);
    int (*write)(void* device, uint64_t lba, uint32_t sectors, void* buffer);
    void* device_data;
} block_device_t;

// Directory List Entry
typedef struct {
    uint32_t inode;
    uint8_t type;
    uint8_t name_len;
    char name[256];
} ext4_dir_list_t;

// EXT4 Filesystem
typedef struct {
    block_device_t* block_device;
    uint64_t partition_start;
    bool readonly;
    
    // Superblock
    ext4_superblock_t superblock;
    
    // Filesystem parameters
    uint32_t block_size;
    uint32_t blocks_per_group;
    uint32_t inodes_per_group;
    uint32_t inode_size;
    uint32_t group_count;
    
    // Features
    bool has_64bit;
    bool has_extents;
    bool has_huge_files;
    
    // Group descriptors
    ext4_group_desc_t* group_descs;
    
    // Cache (simplified)
    spinlock_t lock;
} ext4_filesystem_t;

// =============================================================================
// Function Prototypes
// =============================================================================

ext4_filesystem_t* ext4_mount(block_device_t* device, uint64_t partition_start,
                              bool readonly);
void ext4_unmount(ext4_filesystem_t* fs);

int ext4_read_file(ext4_filesystem_t* fs, const char* path, void* buffer,
                  size_t offset, size_t length);
int ext4_write_file(ext4_filesystem_t* fs, const char* path, void* buffer,
                   size_t offset, size_t length);

int ext4_list_directory(ext4_filesystem_t* fs, const char* path,
                       ext4_dir_list_t* list, size_t max_entries);
int ext4_create_file(ext4_filesystem_t* fs, const char* path, uint16_t mode);
int ext4_create_directory(ext4_filesystem_t* fs, const char* path);
int ext4_delete_file(ext4_filesystem_t* fs, const char* path);
int ext4_rename(ext4_filesystem_t* fs, const char* old_path, const char* new_path);

int ext4_get_file_info(ext4_filesystem_t* fs, const char* path, ext4_inode_t* inode);
int ext4_set_file_attributes(ext4_filesystem_t* fs, const char* path, uint32_t flags);

// Helper functions
size_t strlen(const char* str);
int strcmp(const char* s1, const char* s2);
int memcmp(const void* s1, const void* s2, size_t n);
char* strcpy(char* dest, const char* src);
char* strtok(char* str, const char* delim);

#endif /* EXT4_H */
