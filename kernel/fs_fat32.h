#pragma once
#include <stdint.h>

enum fat_fs_type {
    FAT_FS_FAT32 = 0,
    FAT_FS_EXFAT = 1,
};

typedef struct {
    int      fs_type;      // FAT_FS_* selector
    // from BPB
    uint16_t bytes_per_sec;
    uint8_t  sec_per_clus;
    uint16_t rsvd_sec_cnt;
    uint8_t  num_fats;
    uint32_t fat_sz32;
    uint32_t root_clus;
    uint32_t first_data_lba;
    uint32_t fat_lba;
    uint32_t tot_sec32;
    // device LBA start of this partition
    uint32_t part_lba_start;
    // exFAT fields (valid when fs_type == FAT_FS_EXFAT)
    uint32_t exfat_fat_length;
    uint32_t exfat_heap_offset;
    uint32_t exfat_cluster_count;
    uint32_t exfat_bitmap_clus;
    uint64_t exfat_bitmap_size;
    uint8_t  exfat_bps_shift;
    uint8_t  exfat_spc_shift;
} fat32_vol_t;

typedef int (*disk_read_fn)(uint32_t lba, uint8_t count, void* buf);
typedef int (*disk_write_fn)(uint32_t lba, uint8_t count, const void* buf);

int fat32_mount(fat32_vol_t* vol, disk_read_fn rd, uint32_t part_lba_start);
int fat32_list_root(fat32_vol_t* vol, disk_read_fn rd);
int fat32_read_file(fat32_vol_t* vol, disk_read_fn rd, const char* name83, void* out, uint32_t max_bytes, uint32_t* out_bytes);
int fat32_create_file_root(fat32_vol_t* vol, disk_read_fn rd, disk_write_fn wr,
                           const char* name83, const void* data, uint32_t bytes);
// Overwrite if exists, otherwise create; single-cluster write.
int fat32_write_file_root(fat32_vol_t* vol, disk_read_fn rd, disk_write_fn wr,
                          const char* name83, const void* data, uint32_t bytes);

// Minimal 8.3 entries snapshot for desktop rendering
typedef struct {
    // 8.3 name plus optional dot fits in 12 chars; keep room for terminator.
    char     name83[13];
    uint8_t  attr;       // ATTR_* flags
    uint32_t size;
} fat32_dirent_t;

int fat32_list_root_array(fat32_vol_t* vol, disk_read_fn rd,
                          fat32_dirent_t* out, int max_items, int* out_count);

// Basic path-aware helpers (8.3 components, '/' separated, uppercase-insensitive).
int fat32_read_file_path(fat32_vol_t* vol, disk_read_fn rd, const char* path,
                         void* out, uint32_t max_bytes, uint32_t* out_bytes);
int fat32_write_file_path(fat32_vol_t* vol, disk_read_fn rd, disk_write_fn wr,
                          const char* path, const void* data, uint32_t bytes);
int fat32_list_dir_path(fat32_vol_t* vol, disk_read_fn rd, const char* path,
                        fat32_dirent_t* out, int max_items, int* out_count);
int fat32_ensure_dir_path(fat32_vol_t* vol, disk_read_fn rd, disk_write_fn wr,
                          const char* path);
