#include "fs_fat32.h"
#include <string.h>
#include "serial.h"

#define MAX_SECTOR_SIZE 4096

// forward declarations for helpers used by exFAT helpers
static const char *skip_drive_prefix(const char *path);
static int get_component(const char **ppath, char *out, int outsz);
static int path_has_more(const char *p);
static int read_file_from_cluster(fat32_vol_t *v, disk_read_fn rd, uint32_t start_clus,
                                  uint32_t file_size, void *out, uint32_t max_bytes, uint32_t *out_bytes);

typedef struct __attribute__((packed)) {
    uint8_t  jmp[3];
    char     oem[8];
    uint16_t bytes_per_sec;
    uint8_t  sec_per_clus;
    uint16_t rsvd_sec_cnt;
    uint8_t  num_fats;
    uint16_t root_ent_cnt;
    uint16_t tot_sec16;
    uint8_t  media;
    uint16_t fat_sz16;
    uint16_t sec_per_trk;
    uint16_t num_heads;
    uint32_t hidd_sec;
    uint32_t tot_sec32;
    uint32_t fat_sz32;
    uint16_t ext_flags;
    uint16_t fs_ver;
    uint32_t root_clus;
    uint16_t fs_info;
    uint16_t bk_boot_sec;
    uint8_t  reserved[12];
    uint8_t  drv_num;
    uint8_t  reserved1;
    uint8_t  boot_sig;
    uint32_t vol_id;
    char     vol_lbl[11];
    char     fs_type[8];
} bpb_fat32_t;

typedef struct __attribute__((packed)) {
    uint8_t  jmp[3];
    char     fsname[8]; // "EXFAT   "
    uint8_t  reserved1[53];
    uint64_t part_offset;
    uint64_t vol_length;
    uint32_t fat_offset;
    uint32_t fat_length;
    uint32_t cluster_heap_offset;
    uint32_t cluster_count;
    uint32_t root_dir_clus;
    uint32_t vol_serial;
    uint16_t fs_revision;
    uint16_t vol_flags;
    uint8_t  bytes_per_sec_shift;
    uint8_t  sec_per_clus_shift;
    uint8_t  num_fats;
    uint8_t  drive_select;
    uint8_t  percent_in_use;
    uint8_t  reserved2[7];
} bpb_exfat_t;

typedef struct __attribute__((packed)) {
    char     name[11];
    uint8_t  attr;
    uint8_t  ntres;
    uint8_t  crt_time_tenth;
    uint16_t crt_time;
    uint16_t crt_date;
    uint16_t lst_acc_date;
    uint16_t fst_clus_hi;
    uint16_t wrt_time;
    uint16_t wrt_date;
    uint16_t fst_clus_lo;
    uint32_t file_size;
} dirent_t;

#define ATTR_LONG_NAME 0x0F
#define ATTR_DIR       0x10
#define ATTR_ARCHIVE   0x20

#define EXFAT_ENTRY_BITMAP 0x81
#define EXFAT_ENTRY_UPCASE 0x82
#define EXFAT_ENTRY_VOLUME 0x83
#define EXFAT_ENTRY_FILE   0x85
#define EXFAT_ENTRY_STREAM 0xC0
#define EXFAT_ENTRY_NAME   0xC1

typedef struct {
    uint32_t cluster;
    uint64_t size;
    int      is_dir;
    uint32_t dir_cluster;
    uint32_t dir_byte_off;
    uint32_t file_entry_lba;
    uint16_t file_entry_off;
    uint32_t stream_entry_lba;
    uint16_t stream_entry_off;
    uint8_t  name_len;
    uint8_t  secondary_count;
    uint16_t attr;
} exfat_entry_info_t;

static uint32_t clus_to_lba(const fat32_vol_t* v, uint32_t clus)
{
    return v->part_lba_start + v->first_data_lba + (clus - 2) * v->sec_per_clus;
}

static int match_name11(const char *ent11, const char *want11)
{
    for (int i = 0; i < 11; ++i)
    {
        if (ent11[i] != want11[i])
            return 0;
    }
    return 1;
}

int fat32_mount(fat32_vol_t* v, disk_read_fn rd, uint32_t part_lba_start)
{
    if (!v || !rd) return -1;
    uint8_t sec[512];
    if (rd(part_lba_start + 0, 1, sec)) return -2;
    // exFAT detection
    if (memcmp(sec + 3, "EXFAT   ", 8) == 0) {
        const bpb_exfat_t* ebpb = (const bpb_exfat_t*)sec;
        v->fs_type        = FAT_FS_EXFAT;
        v->exfat_bps_shift= ebpb->bytes_per_sec_shift;
        v->exfat_spc_shift= ebpb->sec_per_clus_shift;
        v->bytes_per_sec  = 1u << ebpb->bytes_per_sec_shift;
        v->sec_per_clus   = 1u << ebpb->sec_per_clus_shift;
        v->num_fats       = ebpb->num_fats;
        v->fat_sz32       = ebpb->fat_length;
        v->exfat_fat_length = ebpb->fat_length;
        v->exfat_heap_offset = ebpb->cluster_heap_offset;
        v->exfat_cluster_count = ebpb->cluster_count;
        v->root_clus      = ebpb->root_dir_clus;
        v->part_lba_start = part_lba_start;
        v->fat_lba        = part_lba_start + ebpb->fat_offset;
        v->first_data_lba = ebpb->cluster_heap_offset;
        v->tot_sec32      = (uint32_t)(ebpb->vol_length & 0xFFFFFFFFu);
        serial_printf("[exFAT] bps=%u spc=%u fatsz=%u heap_off=%u heap_cnt=%u rootcl=%u\n",
                      v->bytes_per_sec, v->sec_per_clus, v->exfat_fat_length,
                      v->exfat_heap_offset, v->exfat_cluster_count, v->root_clus);
        // allocation bitmap discovery happens lazily in exFAT helpers
        if (v->bytes_per_sec == 0 || v->bytes_per_sec > MAX_SECTOR_SIZE)
            return -4;
        return 0;
    }
    // Check boot sector signature
    if (!(sec[510] == 0x55 && sec[511] == 0xAA)) {
        serial_printf("[FAT32] boot signature missing at LBA %u\n", part_lba_start);
        // still try, some images may not set it properly
    }
    const bpb_fat32_t* bpb = (const bpb_fat32_t*)sec;
    v->fs_type = FAT_FS_FAT32;
    serial_printf("[FAT32] bps=%u spc=%u rsvd=%u fats=%u fatsz=%u rootcl=%u\n",
                  bpb->bytes_per_sec, bpb->sec_per_clus, bpb->rsvd_sec_cnt,
                  bpb->num_fats, bpb->fat_sz32, bpb->root_clus);
    v->bytes_per_sec = bpb->bytes_per_sec;
    v->sec_per_clus  = bpb->sec_per_clus;
    v->rsvd_sec_cnt  = bpb->rsvd_sec_cnt;
    v->num_fats      = bpb->num_fats;
    v->fat_sz32      = bpb->fat_sz32;
    v->root_clus     = bpb->root_clus;
    v->tot_sec32     = bpb->tot_sec32 ? bpb->tot_sec32 : bpb->tot_sec16;
    v->fat_lba       = part_lba_start + v->rsvd_sec_cnt;
    v->first_data_lba= v->rsvd_sec_cnt + v->num_fats * v->fat_sz32;
    v->part_lba_start= part_lba_start;

    if (v->sec_per_clus == 0 || v->bytes_per_sec == 0 || v->bytes_per_sec > MAX_SECTOR_SIZE)
        return -3;
    return 0;
}

static uint32_t fat_read_fat_entry(const fat32_vol_t* v, disk_read_fn rd, uint32_t clus)
{
    uint32_t fat_offset = clus * 4;
    uint32_t fat_sector = v->fat_lba + (fat_offset / v->bytes_per_sec);
    uint32_t ent_off    = fat_offset % v->bytes_per_sec;
    uint8_t sec[MAX_SECTOR_SIZE];
    if (rd(fat_sector, 1, sec)) return 0x0FFFFFFF; // error -> EOC
    uint32_t val = *(uint32_t*)(sec + ent_off);
    return val & 0x0FFFFFFF;
}

static int is_end_cluster(uint32_t cl)
{
    return cl >= 0x0FFFFFF8u;
}

static int fat_write_fat_entry(const fat32_vol_t* v, disk_read_fn rd, disk_write_fn wr, uint32_t clus, uint32_t val)
{
    uint32_t fat_offset = clus * 4;
    uint32_t fat_sector = v->fat_lba + (fat_offset / v->bytes_per_sec);
    uint32_t ent_off    = fat_offset % v->bytes_per_sec;
    uint8_t sec[MAX_SECTOR_SIZE];
    if (rd(fat_sector, 1, sec)) return -1;
    uint32_t old = *(uint32_t*)(sec + ent_off);
    (void)old;
    *(uint32_t*)(sec + ent_off) = (val & 0x0FFFFFFF) | (old & 0xF0000000);
    if (wr(fat_sector, 1, sec)) return -2;
    // Mirror other FATs is ignored for simplicity
    return 0;
}

static int dir_find_entry(const fat32_vol_t *v, disk_read_fn rd, uint32_t dir_cl,
                          const char want11[11], dirent_t *out,
                          uint32_t *out_lba, uint8_t *out_s, int *out_off)
{
    uint8_t sec[MAX_SECTOR_SIZE];
    while (!is_end_cluster(dir_cl))
    {
        uint32_t lba = clus_to_lba(v, dir_cl);
        for (uint8_t s = 0; s < v->sec_per_clus; ++s)
        {
            if (rd(lba + s, 1, sec))
                return -1;
            for (int off = 0; off < v->bytes_per_sec; off += 32)
            {
                dirent_t *de = (dirent_t *)(sec + off);
                if (de->name[0] == 0x00)
                    return -2; // end
                if ((uint8_t)de->name[0] == 0xE5)
                    continue;
                if (de->attr == ATTR_LONG_NAME)
                    continue;
                if (match_name11(de->name, want11))
                {
                    if (out)
                        memcpy(out, de, sizeof(dirent_t));
                    if (out_lba)
                        *out_lba = lba;
                    if (out_s)
                        *out_s = s;
                    if (out_off)
                        *out_off = off;
                    return 0;
                }
            }
        }
        dir_cl = fat_read_fat_entry(v, rd, dir_cl);
    }
    return -3; // not found
}

static int dir_find_free_slot(const fat32_vol_t *v, disk_read_fn rd, disk_write_fn wr,
                              uint32_t dir_cl, uint32_t *out_lba, uint8_t *out_s, int *out_off,
                              uint8_t sec_out[512])
{
    (void)wr; // FAT32 path only reads sectors here
    while (!is_end_cluster(dir_cl))
    {
        uint32_t lba = clus_to_lba(v, dir_cl);
        for (uint8_t s = 0; s < v->sec_per_clus; ++s)
        {
            if (rd(lba + s, 1, sec_out))
                return -1;
            for (int off = 0; off < v->bytes_per_sec; off += 32)
            {
                uint8_t *de = sec_out + off;
                if (de[0] == 0x00 || de[0] == 0xE5)
                {
                    if (out_lba)
                        *out_lba = lba;
                    if (out_s)
                        *out_s = s;
                    if (out_off)
                        *out_off = off;
                    return 0;
                }
            }
        }
        dir_cl = fat_read_fat_entry(v, rd, dir_cl);
    }
    return -1;
}

static uint32_t fat_alloc_free_cluster(const fat32_vol_t* v, disk_read_fn rd, disk_write_fn wr)
{
    // Simple scan for free cluster from 2 up to some limit
    uint32_t max_clusters = (v->tot_sec32 - v->first_data_lba) / v->sec_per_clus;
    if (max_clusters > 1024*1024) max_clusters = 1024*1024;
    uint8_t sec[MAX_SECTOR_SIZE];
    uint32_t cl = 2;
    for (; cl < max_clusters; ++cl)
    {
        uint32_t off = cl * 4;
        uint32_t fat_sector = v->fat_lba + (off / v->bytes_per_sec);
        uint32_t ent_off    = off % v->bytes_per_sec;
        if (rd(fat_sector, 1, sec)) return 0;
        uint32_t val = *(uint32_t*)(sec + ent_off) & 0x0FFFFFFF;
        if (val == 0)
        {
            // mark EOC
            if (fat_write_fat_entry(v, rd, wr, cl, 0x0FFFFFFF) == 0)
                return cl;
            return 0;
        }
    }
    return 0;
}

// --- exFAT helpers ---

static uint32_t exfat_walk_chain(const fat32_vol_t *v, disk_read_fn rd, uint32_t start, uint32_t step)
{
    uint32_t cl = start;
    for (uint32_t i = 0; i < step && !is_end_cluster(cl); ++i)
        cl = fat_read_fat_entry(v, rd, cl);
    return cl;
}

static int exfat_mark_bitmap(const fat32_vol_t *v, disk_read_fn rd, disk_write_fn wr, uint32_t cl, int used)
{
    if (v->exfat_bitmap_clus == 0)
        return 0; // bitmap not found, skip
    uint32_t bit = cl - 2;
    uint32_t byte_off = bit / 8;
    uint32_t bit_in_byte = bit % 8;
    if (v->exfat_bitmap_size == 0)
        return 0;
    if (byte_off >= v->exfat_bitmap_size)
        return -1;
    uint32_t bytes_per_clus = v->bytes_per_sec * v->sec_per_clus;
    uint32_t clus_step = byte_off / bytes_per_clus;
    uint32_t target_clus = exfat_walk_chain(v, rd, v->exfat_bitmap_clus, clus_step);
    uint32_t offset_in_clus = byte_off % bytes_per_clus;
    uint32_t sec_idx = offset_in_clus / v->bytes_per_sec;
    uint32_t off_in_sec = offset_in_clus % v->bytes_per_sec;
    uint8_t sec[MAX_SECTOR_SIZE];
    uint32_t lba = clus_to_lba(v, target_clus) + sec_idx;
    if (rd(lba, 1, sec)) return -1;
    if (used)
        sec[off_in_sec] |= (1u << bit_in_byte);
    else
        sec[off_in_sec] &= ~(1u << bit_in_byte);
    if (wr(lba, 1, sec)) return -1;
    return 0;
}

static uint32_t exfat_alloc_cluster(const fat32_vol_t* v, disk_read_fn rd, disk_write_fn wr)
{
    uint32_t max_clusters = v->exfat_cluster_count ? v->exfat_cluster_count : (uint32_t)(v->tot_sec32 / v->sec_per_clus);
    if (max_clusters > 1024*1024) max_clusters = 1024*1024;
    uint8_t sec[MAX_SECTOR_SIZE];
    for (uint32_t cl = 2; cl < max_clusters; ++cl)
    {
        uint32_t off = cl * 4;
        uint32_t fat_sector = v->fat_lba + (off / v->bytes_per_sec);
        uint32_t ent_off    = off % v->bytes_per_sec;
        if (rd(fat_sector, 1, sec)) return 0;
        uint32_t val = *(uint32_t*)(sec + ent_off) & 0x0FFFFFFF;
        if (val == 0)
        {
            if (fat_write_fat_entry(v, rd, wr, cl, 0x0FFFFFFF) != 0)
                return 0;
            exfat_mark_bitmap(v, rd, wr, cl, 1);
            return cl;
        }
    }
    return 0;
}

static int exfat_free_chain(const fat32_vol_t *v, disk_read_fn rd, disk_write_fn wr, uint32_t start)
{
    uint32_t cl = start;
    while (!is_end_cluster(cl) && cl != 0)
    {
        uint32_t next = fat_read_fat_entry(v, rd, cl);
        fat_write_fat_entry(v, rd, wr, cl, 0);
        exfat_mark_bitmap(v, rd, wr, cl, 0);
        cl = next;
    }
    return 0;
}

static int exfat_collect_entries(const fat32_vol_t *v, disk_read_fn rd,
                                 uint32_t dir_cl, uint32_t start_byte,
                                 uint8_t total_entries, uint8_t *out)
{
    uint32_t cluster_bytes = v->bytes_per_sec * v->sec_per_clus;
    if ((uint64_t)start_byte + (uint64_t)total_entries * 32ull > cluster_bytes)
        return -1;
    uint32_t lba_base = clus_to_lba(v, dir_cl);
    uint32_t last_sec = 0xFFFFFFFF;
    uint8_t sec[MAX_SECTOR_SIZE];
    for (uint8_t i = 0; i < total_entries; ++i)
    {
        uint32_t byte_off = start_byte + i * 32;
        uint32_t sec_idx = byte_off / v->bytes_per_sec;
        uint32_t off_in_sec = byte_off % v->bytes_per_sec;
        if (sec_idx != last_sec)
        {
            if (rd(lba_base + sec_idx, 1, sec))
                return -1;
            last_sec = sec_idx;
        }
        memcpy(out + i * 32, sec + off_in_sec, 32);
    }
    return 0;
}

static int exfat_parse_file_set(const uint8_t *set, uint8_t secondary_count,
                                char *name_buf, int name_buf_sz,
                                exfat_entry_info_t *info)
{
    if (secondary_count == 0)
        return -1;
    const uint8_t *file_entry = set;
    const uint8_t *stream_entry = set + 32;
    if (stream_entry[0] != EXFAT_ENTRY_STREAM)
        return -1;
    uint8_t name_len = stream_entry[3];
    if (name_len >= name_buf_sz)
        name_len = (uint8_t)(name_buf_sz - 1);
    uint32_t name_chars_collected = 0;
    for (uint8_t i = 2; i <= secondary_count && name_chars_collected < name_len; ++i)
    {
        const uint8_t *ne = set + i * 32;
        if (ne[0] != EXFAT_ENTRY_NAME)
            continue;
        const uint16_t *chars = (const uint16_t *)(ne + 2);
        for (int c = 0; c < 15 && name_chars_collected < name_len; ++c)
        {
            uint16_t ch = chars[c];
            name_buf[name_chars_collected++] = (char)(ch & 0xFF);
        }
    }
    name_buf[name_chars_collected] = 0;
    uint16_t attr = *(const uint16_t *)(file_entry + 4);
    uint32_t first_cluster = *(const uint32_t *)(stream_entry + 20);
    uint64_t data_len = *(const uint64_t *)(stream_entry + 24);
    if (info)
    {
        info->cluster = first_cluster;
        info->size = data_len;
        info->is_dir = (attr & ATTR_DIR) ? 1 : 0;
        info->name_len = name_len;
        info->secondary_count = secondary_count;
        info->attr = attr;
    }
    return 0;
}

static int exfat_find_in_dir(fat32_vol_t* v, disk_read_fn rd, uint32_t dir_cl,
                             const char* name, exfat_entry_info_t* out)
{
    char want_upper[256];
    int want_len = 0;
    if (name)
    {
        want_len = (int)strlen(name);
        if (want_len >= (int)sizeof(want_upper)) want_len = sizeof(want_upper) - 1;
        for (int i = 0; i < want_len; ++i)
        {
            char c = name[i];
            if (c >= 'a' && c <= 'z') c -= 32;
            want_upper[i] = c;
        }
        want_upper[want_len] = 0;
    }

    uint32_t cluster = dir_cl;
    uint8_t sec[MAX_SECTOR_SIZE];
    while (!is_end_cluster(cluster))
    {
        uint32_t lba_base = clus_to_lba(v, cluster);
        uint32_t cluster_bytes = v->bytes_per_sec * v->sec_per_clus;
        for (uint32_t byte_off = 0; byte_off < cluster_bytes; byte_off += 32)
        {
            uint32_t sec_idx = byte_off / v->bytes_per_sec;
            uint32_t off_in_sec = byte_off % v->bytes_per_sec;
            if (rd(lba_base + sec_idx, 1, sec))
                return -1;
            uint8_t et = sec[off_in_sec];
            if (et == 0x00)
                return -2; // end of directory
            if (et == EXFAT_ENTRY_BITMAP && v->exfat_bitmap_clus == 0)
            {
                uint32_t first_cluster = *(uint32_t *)(sec + off_in_sec + 20);
                uint64_t data_len = *(uint64_t *)(sec + off_in_sec + 24);
                v->exfat_bitmap_clus = first_cluster;
                v->exfat_bitmap_size = data_len;
            }
            if (et != EXFAT_ENTRY_FILE)
                continue;
            uint8_t secondary = sec[off_in_sec + 1];
            if (secondary == 0)
                continue;
            if (secondary > 40)
                continue; // too long, skip
            uint8_t setbuf[32 * 32];
            if (exfat_collect_entries(v, rd, cluster, byte_off, (uint8_t)(secondary + 1), setbuf) != 0)
                continue;
            char entry_name[256];
            exfat_entry_info_t info = {0};
            if (exfat_parse_file_set(setbuf, secondary, entry_name, sizeof(entry_name), &info) != 0)
                continue;
            // case-insensitive compare
            if (name)
            {
                int eq = 1;
                for (int i = 0; i < want_len; ++i)
                {
                    char c = entry_name[i];
                    if (c >= 'a' && c <= 'z') c -= 32;
                    if (c != want_upper[i]) { eq = 0; break; }
                }
                if (!eq || entry_name[want_len] != 0)
                    continue;
            }
            if (out)
            {
                *out = info;
                out->dir_cluster = cluster;
                out->dir_byte_off = byte_off;
                out->file_entry_lba = lba_base + sec_idx;
                out->file_entry_off = (uint16_t)off_in_sec;
                uint32_t stream_byte = byte_off + 32;
                uint32_t stream_sec = stream_byte / v->bytes_per_sec;
                out->stream_entry_lba = lba_base + stream_sec;
                out->stream_entry_off = (uint16_t)(stream_byte % v->bytes_per_sec);
            }
            return 0;
        }
        cluster = fat_read_fat_entry(v, rd, cluster);
    }
    return -3; // not found
}

static int exfat_load_bitmap_info(fat32_vol_t* v, disk_read_fn rd)
{
    if (v->exfat_bitmap_clus)
        return 0;
    uint32_t cluster = v->root_clus;
    uint8_t sec[MAX_SECTOR_SIZE];
    while (!is_end_cluster(cluster))
    {
        uint32_t lba_base = clus_to_lba(v, cluster);
        uint32_t cluster_bytes = v->bytes_per_sec * v->sec_per_clus;
        for (uint32_t byte_off = 0; byte_off < cluster_bytes; byte_off += 32)
        {
            uint32_t sec_idx = byte_off / v->bytes_per_sec;
            uint32_t off_in_sec = byte_off % v->bytes_per_sec;
            if (rd(lba_base + sec_idx, 1, sec))
                return -1;
            uint8_t et = sec[off_in_sec];
            if (et == 0x00)
                return -1;
            if (et == EXFAT_ENTRY_BITMAP)
            {
                v->exfat_bitmap_clus = *(uint32_t *)(sec + off_in_sec + 20);
                v->exfat_bitmap_size = *(uint64_t *)(sec + off_in_sec + 24);
                return 0;
            }
        }
        cluster = fat_read_fat_entry(v, rd, cluster);
    }
    return -1;
}

static int exfat_zero_cluster(const fat32_vol_t *v, disk_write_fn wr, uint32_t clus)
{
    uint8_t sec[MAX_SECTOR_SIZE];
    memset(sec, 0, v->bytes_per_sec);
    uint32_t lba = clus_to_lba(v, clus);
    for (uint8_t s = 0; s < v->sec_per_clus; ++s)
    {
        if (wr(lba + s, 1, sec))
            return -1;
    }
    return 0;
}

static uint16_t exfat_checksum_entries(const uint8_t *set, uint8_t total_entries)
{
    uint16_t sum = 0;
    for (uint8_t i = 0; i < total_entries; ++i)
    {
        for (int b = 0; b < 32; ++b)
        {
            uint8_t v = set[i * 32 + b];
            if (i == 0 && b == 0)
                v = 0;
            sum = (uint16_t)(((sum >> 1) | (sum << 15)) + v);
        }
    }
    return sum;
}

static int exfat_write_entry(const fat32_vol_t *v, disk_read_fn rd, disk_write_fn wr,
                             uint32_t dir_cl, uint32_t byte_off, const uint8_t data[32])
{
    uint32_t lba_base = clus_to_lba(v, dir_cl);
    uint32_t sec_idx = byte_off / v->bytes_per_sec;
    uint32_t off_in_sec = byte_off % v->bytes_per_sec;
    uint8_t sec[MAX_SECTOR_SIZE];
    if (rd(lba_base + sec_idx, 1, sec))
        return -1;
    memcpy(sec + off_in_sec, data, 32);
    if (wr(lba_base + sec_idx, 1, sec))
        return -1;
    return 0;
}

static int exfat_find_free_set(fat32_vol_t *v, disk_read_fn rd, disk_write_fn wr, uint32_t dir_cl,
                               uint8_t total_entries, uint32_t *out_cl, uint32_t *out_byte)
{
    uint8_t sec[MAX_SECTOR_SIZE];
    uint32_t cluster = dir_cl;
    uint32_t prev = dir_cl;
    while (!is_end_cluster(cluster))
    {
        uint32_t lba_base = clus_to_lba(v, cluster);
        uint32_t cluster_bytes = v->bytes_per_sec * v->sec_per_clus;
        uint32_t run = 0;
        uint32_t run_start = 0;
        for (uint32_t byte_off = 0; byte_off < cluster_bytes; byte_off += 32)
        {
            uint32_t sec_idx = byte_off / v->bytes_per_sec;
            uint32_t off_in_sec = byte_off % v->bytes_per_sec;
            if (rd(lba_base + sec_idx, 1, sec))
                return -1;
            uint8_t et = sec[off_in_sec];
            if (et == 0x00)
            {
                uint32_t remaining_entries = (cluster_bytes - byte_off) / 32;
                if (remaining_entries >= total_entries)
                {
                    if (out_cl) *out_cl = cluster;
                    if (out_byte) *out_byte = byte_off;
                    return 0;
                }
            }
            if (et == 0x00 || et == 0xFF)
            {
                if (run == 0)
                    run_start = byte_off;
                run++;
                if (run >= total_entries)
                {
                    if (out_cl) *out_cl = cluster;
                    if (out_byte) *out_byte = run_start;
                    return 0;
                }
            }
            else
            {
                run = 0;
            }
        }
        prev = cluster;
        cluster = fat_read_fat_entry(v, rd, cluster);
    }

    // no space, extend directory
    uint32_t new_cl = exfat_alloc_cluster(v, rd, wr);
    if (!new_cl)
        return -1;
    fat_write_fat_entry(v, rd, wr, prev, new_cl);
    fat_write_fat_entry(v, rd, wr, new_cl, 0x0FFFFFFF);
    exfat_zero_cluster(v, wr, new_cl);
    if (out_cl) *out_cl = new_cl;
    if (out_byte) *out_byte = 0;
    return 0;
}

static int exfat_write_entry_set(fat32_vol_t *v, disk_read_fn rd, disk_write_fn wr,
                                 uint32_t dir_cl, uint32_t start_byte,
                                 const uint8_t *set, uint8_t total_entries)
{
    uint16_t csum = exfat_checksum_entries(set, total_entries);
    uint8_t setbuf[32 * 40];
    if (total_entries > 40)
        return -1;
    memcpy(setbuf, set, total_entries * 32);
    setbuf[2] = (uint8_t)(csum & 0xFF);
    setbuf[3] = (uint8_t)(csum >> 8);
    for (uint8_t i = 0; i < total_entries; ++i)
    {
        if (exfat_write_entry(v, rd, wr, dir_cl, start_byte + i * 32, setbuf + i * 32) != 0)
            return -1;
    }
    return 0;
}

static uint32_t exfat_alloc_chain_for_size(fat32_vol_t *v, disk_read_fn rd, disk_write_fn wr, uint32_t bytes)
{
    if (bytes == 0)
        return 0;
    uint32_t cluster_bytes = v->bytes_per_sec * v->sec_per_clus;
    uint32_t need = (bytes + cluster_bytes - 1) / cluster_bytes;
    uint32_t first = 0, prev = 0;
    for (uint32_t i = 0; i < need; ++i)
    {
        uint32_t cl = exfat_alloc_cluster(v, rd, wr);
        if (!cl)
        {
            if (first)
                exfat_free_chain(v, rd, wr, first);
            return 0;
        }
        if (!first)
            first = cl;
        if (prev)
            fat_write_fat_entry(v, rd, wr, prev, cl);
        prev = cl;
    }
    fat_write_fat_entry(v, rd, wr, prev, 0x0FFFFFFF);
    return first;
}

static int exfat_write_chain(fat32_vol_t *v, disk_read_fn rd, disk_write_fn wr,
                             uint32_t start, const void *data, uint32_t bytes)
{
    const uint8_t *src = (const uint8_t *)data;
    uint32_t remaining = bytes;
    uint8_t sec[MAX_SECTOR_SIZE];
    uint32_t cl = start;
    while (!is_end_cluster(cl) && remaining > 0)
    {
        uint32_t lba = clus_to_lba(v, cl);
        for (uint8_t s = 0; s < v->sec_per_clus; ++s)
        {
            uint32_t take = (remaining > v->bytes_per_sec) ? v->bytes_per_sec : remaining;
            if (take)
            {
                memcpy(sec, src, take);
                if (take < v->bytes_per_sec)
                    memset(sec + take, 0, v->bytes_per_sec - take);
                src += take;
                remaining -= take;
            }
            else
            {
                memset(sec, 0, v->bytes_per_sec);
            }
            if (wr(lba + s, 1, sec))
                return -1;
        }
        cl = fat_read_fat_entry(v, rd, cl);
    }
    return 0;
}

static int exfat_build_entry_set(fat32_vol_t *v, const char *name, uint32_t first_cluster,
                                 uint64_t size, int is_dir, uint8_t *out_set, uint8_t *out_total)
{
    (void)v;
    int name_len = (int)strlen(name);
    if (name_len > 255) name_len = 255;
    int name_entries = (name_len + 14) / 15;
    uint8_t total_entries = (uint8_t)(2 + name_entries);
    if (total_entries > 40)
        return -1;
    memset(out_set, 0, total_entries * 32);
    uint8_t *file_ent = out_set;
    uint8_t *stream_ent = out_set + 32;
    file_ent[0] = EXFAT_ENTRY_FILE;
    file_ent[1] = (uint8_t)(total_entries - 1);
    uint16_t attr = is_dir ? ATTR_DIR : ATTR_ARCHIVE;
    *(uint16_t *)(file_ent + 4) = attr;

    stream_ent[0] = EXFAT_ENTRY_STREAM;
    stream_ent[1] = 0x00; // use FAT chaining
    stream_ent[3] = (uint8_t)name_len;
    *(uint32_t *)(stream_ent + 20) = first_cluster;
    *(uint64_t *)(stream_ent + 24) = size;
    *(uint64_t *)(stream_ent + 8) = size;

    const char *p = name;
    for (int i = 0; i < name_entries; ++i)
    {
        uint8_t *ne = out_set + (2 + i) * 32;
        ne[0] = EXFAT_ENTRY_NAME;
        ne[1] = 0;
        uint16_t *chars = (uint16_t *)(ne + 2);
        for (int c = 0; c < 15; ++c)
        {
            if (*p)
                chars[c] = (uint8_t)(*p++);
            else
                chars[c] = 0;
        }
    }
    if (out_total)
        *out_total = total_entries;
    return 0;
}

static int exfat_create_entry(fat32_vol_t *v, disk_read_fn rd, disk_write_fn wr,
                              uint32_t dir_cl, const char *name, int is_dir,
                              const void *data, uint32_t bytes, exfat_entry_info_t *out_info)
{
    uint8_t setbuf[32 * 40];
    uint8_t total = 0;
    int r = exfat_build_entry_set(v, name, 0, bytes, is_dir, setbuf, &total);
    if (r != 0)
        return -1;

    // allocate cluster chain if needed (directories always get one cluster)
    uint32_t first_cluster = 0;
    uint32_t actual_bytes = bytes;
    if (is_dir)
    {
        actual_bytes = v->bytes_per_sec * v->sec_per_clus;
    }
    if (actual_bytes > 0)
    {
        first_cluster = exfat_alloc_chain_for_size(v, rd, wr, actual_bytes);
        if (!first_cluster)
            return -2;
        if (data && bytes > 0)
            exfat_write_chain(v, rd, wr, first_cluster, data, bytes);
        else
            exfat_zero_cluster(v, wr, first_cluster);
    }
    *(uint32_t *)(setbuf + 32 + 20) = first_cluster;
    *(uint64_t *)(setbuf + 32 + 24) = bytes;
    *(uint64_t *)(setbuf + 32 + 8)  = bytes;

    uint32_t start_cl = 0, start_byte = 0;
    if (exfat_find_free_set(v, rd, wr, dir_cl, total, &start_cl, &start_byte) != 0)
        return -3;
    if (exfat_write_entry_set(v, rd, wr, start_cl, start_byte, setbuf, total) != 0)
        return -4;

    if (out_info)
    {
        memset(out_info, 0, sizeof(*out_info));
        out_info->cluster = first_cluster;
        out_info->size = bytes;
        out_info->is_dir = is_dir;
        out_info->dir_cluster = start_cl;
        out_info->dir_byte_off = start_byte;
        out_info->file_entry_lba = clus_to_lba(v, start_cl) + (start_byte / v->bytes_per_sec);
        out_info->file_entry_off = (uint16_t)(start_byte % v->bytes_per_sec);
        out_info->stream_entry_lba = clus_to_lba(v, start_cl) + ((start_byte + 32) / v->bytes_per_sec);
        out_info->stream_entry_off = (uint16_t)((start_byte + 32) % v->bytes_per_sec);
    }
    return 0;
}

static int exfat_update_entry(fat32_vol_t *v, disk_read_fn rd, disk_write_fn wr,
                              const exfat_entry_info_t *loc, const char *name,
                              uint32_t first_cluster, uint32_t bytes, int is_dir)
{
    uint8_t setbuf[32 * 40];
    uint8_t total = 0;
    if (exfat_build_entry_set(v, name, first_cluster, bytes, is_dir, setbuf, &total) != 0)
        return -1;
    return exfat_write_entry_set(v, rd, wr, loc->dir_cluster, loc->dir_byte_off, setbuf, total);
}

static int exfat_follow_path(fat32_vol_t *v, disk_read_fn rd, const char *path,
                             uint32_t *out_dir, exfat_entry_info_t *out_info)
{
    const char *p = skip_drive_prefix(path);
    uint32_t dir = v->root_clus;
    char comp[256];
    while (get_component(&p, comp, sizeof(comp)))
    {
        int more = path_has_more(p);
        exfat_entry_info_t info;
        int r = exfat_find_in_dir(v, rd, dir, comp, &info);
        if (r != 0)
            return r;
        if (more)
        {
            if (!info.is_dir)
                return -3;
            dir = info.cluster;
        }
        else
        {
            if (out_dir)
                *out_dir = dir;
            if (out_info)
                *out_info = info;
            return 0;
        }
    }
    if (out_dir)
        *out_dir = dir;
    return 0;
}

static int exfat_read_file_path(fat32_vol_t* v, disk_read_fn rd, const char* path,
                                void* out, uint32_t max_bytes, uint32_t* out_bytes)
{
    exfat_entry_info_t info;
    int r = exfat_follow_path(v, rd, path, NULL, &info);
    if (r != 0) return r;
    if (info.is_dir) return -4;
    uint32_t size32 = (info.size > 0xFFFFFFFFu) ? 0xFFFFFFFFu : (uint32_t)info.size;
    return read_file_from_cluster(v, rd, info.cluster, size32, out, max_bytes, out_bytes);
}

static int exfat_write_file_path(fat32_vol_t* v, disk_read_fn rd, disk_write_fn wr,
                                 const char* path, const void* data, uint32_t bytes)
{
    exfat_load_bitmap_info(v, rd);
    const char *p = skip_drive_prefix(path);
    uint32_t dir = v->root_clus;
    char comp[256];
    while (get_component(&p, comp, sizeof(comp)))
    {
        int more = path_has_more(p);
        if (more)
        {
            exfat_entry_info_t info;
            int r = exfat_find_in_dir(v, rd, dir, comp, &info);
            if (r != 0)
            {
                exfat_entry_info_t created;
                if (exfat_create_entry(v, rd, wr, dir, comp, 1, NULL, 0, &created) != 0)
                    return -5;
                dir = created.cluster;
            }
            else
            {
                if (!info.is_dir)
                    return -6;
                dir = info.cluster;
            }
        }
        else
        {
            exfat_entry_info_t info;
            int r = exfat_find_in_dir(v, rd, dir, comp, &info);
            if (r == 0)
            {
                uint32_t first_cluster = 0;
                if (bytes > 0)
                {
                    first_cluster = exfat_alloc_chain_for_size(v, rd, wr, bytes);
                    if (!first_cluster)
                        return -7;
                    if (exfat_write_chain(v, rd, wr, first_cluster, data, bytes) != 0)
                    {
                        exfat_free_chain(v, rd, wr, first_cluster);
                        return -8;
                    }
                }
                if (info.cluster)
                    exfat_free_chain(v, rd, wr, info.cluster);
                return exfat_update_entry(v, rd, wr, &info, comp, first_cluster, bytes, 0);
            }
            else
            {
                return exfat_create_entry(v, rd, wr, dir, comp, 0, data, bytes, NULL);
            }
        }
    }
    return -1;
}

static int exfat_ensure_dir_path(fat32_vol_t *v, disk_read_fn rd, disk_write_fn wr,
                                 const char *path)
{
    const char *p = skip_drive_prefix(path);
    uint32_t dir = v->root_clus;
    char comp[256];
    while (get_component(&p, comp, sizeof(comp)))
    {
        exfat_entry_info_t info;
        int r = exfat_find_in_dir(v, rd, dir, comp, &info);
        if (r != 0)
        {
            exfat_entry_info_t created;
            if (exfat_create_entry(v, rd, wr, dir, comp, 1, NULL, 0, &created) != 0)
                return -1;
            dir = created.cluster;
        }
        else
        {
            if (!info.is_dir)
                return -2;
            dir = info.cluster;
        }
    }
    return 0;
}

static int exfat_list_dir(fat32_vol_t *v, disk_read_fn rd, uint32_t dir_cl,
                          fat32_dirent_t *out, int max_items, int *out_count)
{
    uint8_t sec[MAX_SECTOR_SIZE];
    int n = 0;
    uint32_t cluster = dir_cl;
    while (!is_end_cluster(cluster))
    {
        uint32_t lba_base = clus_to_lba(v, cluster);
        uint32_t cluster_bytes = v->bytes_per_sec * v->sec_per_clus;
        for (uint32_t byte_off = 0; byte_off < cluster_bytes && n < max_items; byte_off += 32)
        {
            uint32_t sec_idx = byte_off / v->bytes_per_sec;
            uint32_t off_in_sec = byte_off % v->bytes_per_sec;
            if (rd(lba_base + sec_idx, 1, sec))
                return -1;
            uint8_t et = sec[off_in_sec];
            if (et == 0x00)
            {
                if (out_count) *out_count = n;
                return 0;
            }
            if (et != EXFAT_ENTRY_FILE)
                continue;
            uint8_t secondary = sec[off_in_sec + 1];
            if (secondary == 0 || secondary > 40)
                continue;
            if ((uint64_t)(secondary + 1) * 32ull + byte_off > cluster_bytes)
                continue;
            uint8_t setbuf[32 * 32];
            if (exfat_collect_entries(v, rd, cluster, byte_off, (uint8_t)(secondary + 1), setbuf) != 0)
                continue;
            char namebuf[256];
            exfat_entry_info_t info;
            if (exfat_parse_file_set(setbuf, secondary, namebuf, sizeof(namebuf), &info) != 0)
                continue;
            fat32_dirent_t *e = &out[n++];
            memset(e, 0, sizeof(*e));
            strncpy(e->name83, namebuf, sizeof(e->name83) - 1);
            e->attr = info.is_dir ? ATTR_DIR : ATTR_ARCHIVE;
            e->size = (uint32_t)(info.size & 0xFFFFFFFFu);
        }
        cluster = fat_read_fat_entry(v, rd, cluster);
    }
    if (out_count) *out_count = n;
    return 0;
}

static int exfat_list_dir_path(fat32_vol_t* v, disk_read_fn rd, const char* path,
                               fat32_dirent_t* out, int max_items, int* out_count)
{
    uint32_t dir = v->root_clus;
    const char *p = skip_drive_prefix(path);
    char comp[256];
    while (get_component(&p, comp, sizeof(comp)))
    {
        exfat_entry_info_t info;
        int r = exfat_find_in_dir(v, rd, dir, comp, &info);
        if (r != 0)
            return r;
        if (!info.is_dir)
            return -2;
        dir = info.cluster;
    }
    return exfat_list_dir(v, rd, dir, out, max_items, out_count);
}

static void make_name83(char out11[11], const char* name83)
{
    // very simple 8.3 upper-case builder from input like NAME.TXT
    for (int i=0;i<11;i++) out11[i] = ' ';
    int i=0,j=0;
    while (name83[j] && name83[j] != '.' && i<8) {
        char c = name83[j++]; if (c>='a'&&c<='z') c-=32; out11[i++]=c;
    }
    if (name83[j]=='.') j++;
    int k=0; while (name83[j] && k<3) { char c=name83[j++]; if(c>='a'&&c<='z') c-=32; out11[8+k]=c; k++; }
}

int fat32_create_file_root(fat32_vol_t* v, disk_read_fn rd, disk_write_fn wr,
                           const char* name83, const void* data, uint32_t bytes)
{
    if (!v || !rd || !wr || !name83) return -1;
    // allocate one cluster
    uint32_t cl = fat_alloc_free_cluster(v, rd, wr);
    if (cl == 0) return -2;

    // write content (truncate to one cluster)
    uint32_t cluster_bytes = v->sec_per_clus * 512u;
    uint32_t to_write = (bytes < cluster_bytes) ? bytes : cluster_bytes;
    uint8_t sec[512];
    const uint8_t* src = (const uint8_t*)data;
    uint32_t lba0 = clus_to_lba(v, cl);
    uint32_t left = to_write;
    for (uint8_t s = 0; s < v->sec_per_clus; ++s)
    {
        uint32_t take = (left >= 512) ? 512 : left;
        if (take)
        {
            memcpy(sec, src, take);
            if (take < 512) memset(sec+take, 0, 512-take);
            src += take; left -= take;
        }
        else
        {
            memset(sec, 0, 512);
        }
        if (wr(lba0 + s, 1, sec)) return -3;
    }

    // find free dir entry in root and write
    uint32_t dir_cl = v->root_clus;
    char nm[11]; make_name83(nm, name83);
    while (!is_end_cluster(dir_cl))
    {
        uint32_t lba = clus_to_lba(v, dir_cl);
        for (uint8_t s = 0; s < v->sec_per_clus; ++s)
        {
            if (rd(lba + s, 1, sec)) return -4;
            for (int off = 0; off < 512; off += 32)
            {
                uint8_t* de = sec + off;
                if (de[0] == 0x00 || de[0] == 0xE5)
                {
                    // write short entry
                    memset(de, 0, 32);
                    memcpy(de+0, nm, 11);
                    de[11] = ATTR_ARCHIVE;
                    // timestamps left zero
                    uint16_t cl_lo = (uint16_t)(cl & 0xFFFF);
                    uint16_t cl_hi = (uint16_t)((cl >> 16) & 0xFFFF);
                    *(uint16_t*)(de+20) = cl_hi;
                    *(uint16_t*)(de+26) = cl_lo;
                    *(uint32_t*)(de+28) = to_write;
                    if (wr(lba + s, 1, sec)) return -5;
                    return 0;
                }
            }
        }
        dir_cl = fat_read_fat_entry(v, rd, dir_cl);
    }
    return -6; // no free dir entry (not extending root)
}

int fat32_write_file_root(fat32_vol_t* v, disk_read_fn rd, disk_write_fn wr,
                          const char* name83, const void* data, uint32_t bytes)
{
    if (!v || !rd || !wr || !name83) return -1;

    char nm[11]; make_name83(nm, name83);
    uint8_t sec[512];
    uint32_t dir_cl = v->root_clus;
    uint32_t cluster_bytes = v->sec_per_clus * 512u;
    uint32_t to_write = (bytes < cluster_bytes) ? bytes : cluster_bytes;
    uint32_t target_cl = 0;
    uint32_t dir_lba = 0, dir_s = 0, dir_off = 0;

    // Find existing entry
    while (!is_end_cluster(dir_cl))
    {
        uint32_t lba = clus_to_lba(v, dir_cl);
        for (uint8_t s = 0; s < v->sec_per_clus; ++s)
        {
            if (rd(lba + s, 1, sec)) return -2;
            for (int off = 0; off < 512; off += 32)
            {
                uint8_t* de = sec + off;
                if (de[0] == 0x00) goto create_new;
                if ((uint8_t)de[0] == 0xE5) continue;
                if (de[11] == ATTR_LONG_NAME) continue;
                if (memcmp(de, nm, 11) == 0)
                {
                    uint16_t cl_lo = *(uint16_t*)(de + 26);
                    uint16_t cl_hi = *(uint16_t*)(de + 20);
                    target_cl = ((uint32_t)cl_hi << 16) | cl_lo;
                    dir_lba = lba;
                    dir_s = s;
                    dir_off = off;
                    goto have_cluster;
                }
            }
        }
        dir_cl = fat_read_fat_entry(v, rd, dir_cl);
    }

create_new:
    return fat32_create_file_root(v, rd, wr, name83, data, bytes);

have_cluster:
    if (target_cl == 0)
    {
        target_cl = fat_alloc_free_cluster(v, rd, wr);
        if (target_cl == 0) return -3;
    }

    // write data into target cluster
    uint32_t lba0 = clus_to_lba(v, target_cl);
    const uint8_t *src = (const uint8_t *)data;
    uint32_t left = to_write;
    for (uint8_t s = 0; s < v->sec_per_clus; ++s)
    {
        uint32_t take = (left >= 512) ? 512 : left;
        if (take)
        {
            memcpy(sec, src, take);
            if (take < 512) memset(sec + take, 0, 512 - take);
            src += take;
            left -= take;
        }
        else
        {
            memset(sec, 0, 512);
        }
        if (wr(lba0 + s, 1, sec)) return -4;
    }

    // update dir entry (size + cluster)
    if (dir_lba)
    {
        if (rd(dir_lba + dir_s, 1, sec)) return -5;
        uint8_t *de = sec + dir_off;
        uint16_t cl_hi = (uint16_t)((target_cl >> 16) & 0xFFFF);
        uint16_t cl_lo = (uint16_t)(target_cl & 0xFFFF);
        *(uint16_t *)(de + 20) = cl_hi;
        *(uint16_t *)(de + 26) = cl_lo;
        *(uint32_t *)(de + 28) = to_write;
        if (wr(dir_lba + dir_s, 1, sec)) return -6;
    }

    return 0;
}

int fat32_list_root(fat32_vol_t* v, disk_read_fn rd)
{
    if (!v || !rd) return -1;
    if (v->fs_type == FAT_FS_EXFAT)
    {
        fat32_dirent_t tmp[64];
        int n = 0;
        if (exfat_list_dir(v, rd, v->root_clus, tmp, 64, &n) != 0)
            return -1;
        for (int i = 0; i < n; ++i)
        {
            serial_printf("[exFAT] %s %s %u bytes\n",
                          (tmp[i].attr & ATTR_DIR) ? "<DIR>" : "FILE",
                          tmp[i].name83, tmp[i].size);
        }
        return 0;
    }
    uint32_t cl = v->root_clus;
    uint8_t sec[MAX_SECTOR_SIZE];
    while (!is_end_cluster(cl))
    {
        uint32_t lba = clus_to_lba(v, cl);
        for (uint8_t s = 0; s < v->sec_per_clus; ++s)
        {
            if (rd(lba + s, 1, sec)) return -2;
            for (int off = 0; off < v->bytes_per_sec; off += 32)
            {
                dirent_t* de = (dirent_t*)(sec + off);
                if (de->name[0] == 0x00) return 0; // end
                if ((uint8_t)de->name[0] == 0xE5) continue; // deleted
                if (de->attr == ATTR_LONG_NAME) continue; // skip LFN for now
                char nm[12];
                memcpy(nm, de->name, 11); nm[11] = 0;
                for (int i = 10; i >= 0; --i) { if (nm[i] == ' ') nm[i] = 0; else break; }
                serial_printf("[fat32] %s %s %u bytes\n",
                              (de->attr & ATTR_DIR) ? "<DIR>" : "FILE",
                              nm, de->file_size);
            }
        }
        cl = fat_read_fat_entry(v, rd, cl);
    }
    return 0;
}

static int match_name83(const char* ent11, const char* q)
{
    // q should be like "FOO.TXT" upper-case 8.3
    char want[11];
    int i = 0, j = 0;
    // name part
    while (q[j] && q[j] != '.' && i < 8) want[i++] = q[j++];
    while (i < 8) want[i++] = ' ';
    if (q[j] == '.') { j++; }
    int k = 0; while (q[j] && k < 3) want[i++] = q[j++], k++;
    while (i < 11) want[i++] = ' ';
    return memcmp(ent11, want, 11) == 0;
}

int fat32_read_file(fat32_vol_t* v, disk_read_fn rd, const char* name83,
                    void* out, uint32_t max_bytes, uint32_t* out_bytes)
{
    if (v && v->fs_type == FAT_FS_EXFAT)
        return fat32_read_file_path(v, rd, name83, out, max_bytes, out_bytes);
    if (out_bytes) *out_bytes = 0;
    if (!v || !rd || !name83 || !out) return -1;
    uint32_t cl = v->root_clus;
    uint8_t sec[MAX_SECTOR_SIZE];
    uint32_t start_clus = 0;
    uint32_t file_size = 0;

    // find dir entry in root (8.3 only)
    while (!is_end_cluster(cl))
    {
        uint32_t lba = clus_to_lba(v, cl);
        for (uint8_t s = 0; s < v->sec_per_clus; ++s)
        {
            if (rd(lba + s, 1, sec)) return -2;
            for (int off = 0; off < v->bytes_per_sec; off += 32)
            {
                dirent_t* de = (dirent_t*)(sec + off);
                if (de->name[0] == 0x00) goto found_done;
                if ((uint8_t)de->name[0] == 0xE5) continue;
                if (de->attr == ATTR_LONG_NAME) continue;
                if (match_name83(de->name, name83))
                {
                    start_clus = ((uint32_t)de->fst_clus_hi << 16) | de->fst_clus_lo;
                    file_size = de->file_size;
                    goto found_done;
                }
            }
        }
        cl = fat_read_fat_entry(v, rd, cl);
    }
found_done:
    if (start_clus == 0) return -3; // not found

    // read file clusters
    uint8_t* dst = (uint8_t*)out;
    uint32_t left = (file_size < max_bytes) ? file_size : max_bytes;
    cl = start_clus;
    while (!is_end_cluster(cl) && left > 0)
    {
        uint32_t lba = clus_to_lba(v, cl);
        for (uint8_t s = 0; s < v->sec_per_clus && left > 0; ++s)
        {
            if (rd(lba + s, 1, sec)) return -4;
            uint32_t take = (left < v->bytes_per_sec) ? left : v->bytes_per_sec;
            memcpy(dst, sec, take);
            dst += take;
            left -= take;
        }
        cl = fat_read_fat_entry(v, rd, cl);
    }
    if (out_bytes) *out_bytes = (file_size < max_bytes) ? file_size : max_bytes;
    return 0;
}

int fat32_list_root_array(fat32_vol_t* v, disk_read_fn rd,
                          fat32_dirent_t* out, int max_items, int* out_count)
{
    if (v && v->fs_type == FAT_FS_EXFAT)
        return exfat_list_dir(v, rd, v->root_clus, out, max_items, out_count);
    if (out_count) *out_count = 0;
    if (!v || !rd || !out || max_items <= 0) return -1;
    int n = 0;
    uint32_t cl = v->root_clus;
    uint8_t sec[MAX_SECTOR_SIZE];
    while (!is_end_cluster(cl))
    {
        uint32_t lba = clus_to_lba(v, cl);
        for (uint8_t s = 0; s < v->sec_per_clus; ++s)
        {
            if (rd(lba + s, 1, sec)) goto done;
            for (int off = 0; off < v->bytes_per_sec; off += 32)
            {
                if (n >= max_items) goto done;
                dirent_t* de = (dirent_t*)(sec + off);
                if (de->name[0] == 0x00) goto done;
                if ((uint8_t)de->name[0] == 0xE5) continue;
                if (de->attr == ATTR_LONG_NAME) continue;
                // skip volume label entries (optional)
                // if (de->attr & 0x08) continue;
                fat32_dirent_t *e = &out[n++];
                // Build dotted 8.3 name: BASE.EXT
                char base[9]; char ext[4];
                memcpy(base, de->name, 8); base[8] = 0;
                memcpy(ext, de->name + 8, 3); ext[3] = 0;
                // trim spaces
                for (int i = 7; i >= 0; --i) { if (base[i] == ' ') base[i] = 0; else break; }
                for (int i = 2; i >= 0; --i) { if (ext[i] == ' ') ext[i] = 0; else break; }
                // compose (allow up to 12 chars plus terminator)
                const int maxlen = (int)sizeof(e->name83) - 1;
                int p = 0;
                for (int i = 0; base[i] && p < maxlen; ++i) e->name83[p++] = base[i];
                if (ext[0] && p < maxlen) e->name83[p++] = '.';
                for (int i = 0; ext[i] && p < maxlen; ++i) e->name83[p++] = ext[i];
                e->name83[p] = 0;
                e->attr = de->attr;
                e->size = de->file_size;
            }
        }
        cl = fat_read_fat_entry(v, rd, cl);
    }
done:
    if (out_count) *out_count = n;
    return 0;
}

static uint32_t dirent_start_cluster(const dirent_t *de)
{
    if (!de)
        return 0;
    uint16_t cl_lo = *(const uint16_t *)((const uint8_t *)de + 26);
    uint16_t cl_hi = *(const uint16_t *)((const uint8_t *)de + 20);
    return ((uint32_t)cl_hi << 16) | cl_lo;
}

static int next_path_component(const char **ppath, char out[13])
{
    const char *p = *ppath;
    while (*p == '/')
        p++;
    if (!*p)
        return 0;
    int len = 0;
    while (*p && *p != '/')
    {
        if (len < 12)
            out[len++] = *p;
        p++;
    }
    out[len] = 0;
    *ppath = p;
    return 1;
}

static int get_component(const char **ppath, char *out, int outsz)
{
    const char *p = *ppath;
    while (*p == '/')
        p++;
    if (!*p)
        return 0;
    int len = 0;
    while (*p && *p != '/')
    {
        if (len + 1 < outsz)
            out[len++] = *p;
        p++;
    }
    out[len] = 0;
    *ppath = p;
    return 1;
}

static const char *skip_drive_prefix(const char *path)
{
    if (path && path[0] && path[1] == ':')
        path += 2;
    while (path && *path == '/')
        path++;
    return path;
}

static int path_has_more(const char *p)
{
    while (*p == '/')
        p++;
    return *p != 0;
}

static int read_file_from_cluster(fat32_vol_t *v, disk_read_fn rd, uint32_t start_clus,
                                  uint32_t file_size, void *out, uint32_t max_bytes, uint32_t *out_bytes)
{
    if (out_bytes)
        *out_bytes = 0;
    if (!start_clus)
        return -1;
    uint8_t *dst = (uint8_t *)out;
    uint32_t left = (file_size < max_bytes) ? file_size : max_bytes;
    uint32_t cl = start_clus;
    uint8_t sec[MAX_SECTOR_SIZE];
    while (!is_end_cluster(cl) && left > 0)
    {
        uint32_t lba = clus_to_lba(v, cl);
        for (uint8_t s = 0; s < v->sec_per_clus && left > 0; ++s)
        {
            if (rd(lba + s, 1, sec))
                return -2;
            uint32_t take = (left < v->bytes_per_sec) ? left : v->bytes_per_sec;
            memcpy(dst, sec, take);
            dst += take;
            left -= take;
        }
        cl = fat_read_fat_entry(v, rd, cl);
    }
    if (out_bytes)
        *out_bytes = (file_size < max_bytes) ? file_size : max_bytes;
    return 0;
}

static int write_single_cluster(fat32_vol_t *v, disk_read_fn rd, disk_write_fn wr,
                                uint32_t clus, const void *data, uint32_t bytes)
{
    if (!clus)
        return -1;
    uint32_t cluster_bytes = v->sec_per_clus * v->bytes_per_sec;
    uint32_t to_write = (bytes < cluster_bytes) ? bytes : cluster_bytes;
    uint8_t sec[MAX_SECTOR_SIZE];
    const uint8_t *src = (const uint8_t *)data;
    uint32_t lba0 = clus_to_lba(v, clus);
    uint32_t left = to_write;
    for (uint8_t s = 0; s < v->sec_per_clus; ++s)
    {
        uint32_t take = (left >= v->bytes_per_sec) ? v->bytes_per_sec : left;
        if (take)
        {
            memcpy(sec, src, take);
            if (take < v->bytes_per_sec)
                memset(sec + take, 0, v->bytes_per_sec - take);
            src += take;
            left -= take;
        }
        else
        {
            memset(sec, 0, v->bytes_per_sec);
        }
        if (wr(lba0 + s, 1, sec))
            return -1;
    }
    // mark FAT entry EOC
    fat_write_fat_entry(v, rd, wr, clus, 0x0FFFFFFF);
    return 0;
}

static int create_dir_entry(fat32_vol_t *v, disk_read_fn rd, disk_write_fn wr,
                            uint32_t parent_cl, const char name11[11], uint8_t attr,
                            uint32_t start_clus, uint32_t size_bytes)
{
    uint8_t sec[512];
    uint32_t lba = 0;
    uint8_t s = 0;
    int off = 0;
    if (dir_find_free_slot(v, rd, wr, parent_cl, &lba, &s, &off, sec) != 0)
        return -1;

    dirent_t *de = (dirent_t *)(sec + off);
    memset(de, 0, sizeof(dirent_t));
    memcpy(de->name, name11, 11);
    de->attr = attr;
    *(uint16_t *)((uint8_t *)de + 20) = (uint16_t)((start_clus >> 16) & 0xFFFF);
    *(uint16_t *)((uint8_t *)de + 26) = (uint16_t)(start_clus & 0xFFFF);
    *(uint32_t *)((uint8_t *)de + 28) = size_bytes;
    if (wr(lba + s, 1, sec))
        return -2;
    return 0;
}

static int ensure_directory(fat32_vol_t *v, disk_read_fn rd, disk_write_fn wr,
                            uint32_t parent_cl, const char name11[11], uint32_t *out_cl)
{
    dirent_t found;
    uint32_t lba = 0;
    uint8_t s = 0;
    int off = 0;
    int r = dir_find_entry(v, rd, parent_cl, name11, &found, &lba, &s, &off);
    if (r == 0)
    {
        if (!(found.attr & ATTR_DIR))
            return -2; // exists but not dir
        if (out_cl)
            *out_cl = dirent_start_cluster(&found);
        return 0;
    }

    uint32_t new_cl = fat_alloc_free_cluster(v, rd, wr);
    if (!new_cl)
        return -3;

    // zero cluster and add . and .. entries
    uint8_t sec[512];
    memset(sec, 0, sizeof(sec));
    dirent_t *dot = (dirent_t *)sec;
    memcpy(dot->name, ".          ", 11);
    dot->attr = ATTR_DIR;
    *(uint16_t *)((uint8_t *)dot + 20) = (uint16_t)((new_cl >> 16) & 0xFFFF);
    *(uint16_t *)((uint8_t *)dot + 26) = (uint16_t)(new_cl & 0xFFFF);

    dirent_t *dotdot = (dirent_t *)(sec + 32);
    memcpy(dotdot->name, "..         ", 11);
    dotdot->attr = ATTR_DIR;
    *(uint16_t *)((uint8_t *)dotdot + 20) = (uint16_t)((parent_cl >> 16) & 0xFFFF);
    *(uint16_t *)((uint8_t *)dotdot + 26) = (uint16_t)(parent_cl & 0xFFFF);

    uint32_t lba0 = clus_to_lba(v, new_cl);
    if (wr(lba0, 1, sec))
        return -4;
    memset(sec, 0, sizeof(sec));
    for (uint8_t sct = 1; sct < v->sec_per_clus; ++sct)
    {
        if (wr(lba0 + sct, 1, sec))
            return -4;
    }
    fat_write_fat_entry(v, rd, wr, new_cl, 0x0FFFFFFF);

    if (create_dir_entry(v, rd, wr, parent_cl, name11, ATTR_DIR, new_cl, 0) != 0)
        return -5;
    if (out_cl)
        *out_cl = new_cl;
    return 0;
}

int fat32_ensure_dir_path(fat32_vol_t *vol, disk_read_fn rd, disk_write_fn wr,
                          const char *path)
{
    if (vol && vol->fs_type == FAT_FS_EXFAT)
        return exfat_ensure_dir_path(vol, rd, wr, path);
    if (!vol || !rd || !wr || !path)
        return -1;
    const char *p = skip_drive_prefix(path);
    uint32_t dir_cl = vol->root_clus;
    char comp[13];
    while (next_path_component(&p, comp))
    {
        char name11[11];
        make_name83(name11, comp);
        uint32_t next_cl = 0;
        int r = ensure_directory(vol, rd, wr, dir_cl, name11, &next_cl);
        if (r != 0)
            return r;
        dir_cl = next_cl;
        if (!dir_cl)
            dir_cl = vol->root_clus;
        if (!path_has_more(p))
            break;
    }
    return 0;
}

int fat32_read_file_path(fat32_vol_t *vol, disk_read_fn rd, const char *path,
                         void *out, uint32_t max_bytes, uint32_t *out_bytes)
{
    if (vol && vol->fs_type == FAT_FS_EXFAT)
        return exfat_read_file_path(vol, rd, path, out, max_bytes, out_bytes);
    if (out_bytes)
        *out_bytes = 0;
    if (!vol || !rd || !path || !out)
        return -1;

    const char *p = skip_drive_prefix(path);
    uint32_t dir_cl = vol->root_clus;
    char comp[13];
    while (next_path_component(&p, comp))
    {
        char name11[11];
        make_name83(name11, comp);
        dirent_t de;
        int r = dir_find_entry(vol, rd, dir_cl, name11, &de, NULL, NULL, NULL);
        if (r != 0)
            return -2;
        int more = path_has_more(p);
        uint32_t cl = dirent_start_cluster(&de);
        if (more)
        {
            if (!(de.attr & ATTR_DIR))
                return -3;
            dir_cl = cl;
            if (!dir_cl)
                dir_cl = vol->root_clus;
        }
        else
        {
            if (de.attr & ATTR_DIR)
                return -4;
            return read_file_from_cluster(vol, rd, cl, de.file_size, out, max_bytes, out_bytes);
        }
    }
    return -5;
}

int fat32_write_file_path(fat32_vol_t *vol, disk_read_fn rd, disk_write_fn wr,
                          const char *path, const void *data, uint32_t bytes)
{
    if (vol && vol->fs_type == FAT_FS_EXFAT)
        return exfat_write_file_path(vol, rd, wr, path, data, bytes);
    if (!vol || !rd || !wr || !path)
        return -1;
    const char *p = skip_drive_prefix(path);
    uint32_t dir_cl = vol->root_clus;
    char comp[13];
    while (next_path_component(&p, comp))
    {
        char name11[11];
        make_name83(name11, comp);
        int more = path_has_more(p);
        dirent_t de;
        uint32_t lba = 0;
        uint8_t s = 0;
        int off = 0;
        int found = dir_find_entry(vol, rd, dir_cl, name11, &de, &lba, &s, &off);
        if (more)
        {
            if (found != 0)
            {
                uint32_t next_cl = 0;
                int er = ensure_directory(vol, rd, wr, dir_cl, name11, &next_cl);
                if (er != 0)
                    return er;
                dir_cl = next_cl;
            }
            else
            {
                if (!(de.attr & ATTR_DIR))
                    return -3;
                dir_cl = dirent_start_cluster(&de);
                if (!dir_cl)
                    dir_cl = vol->root_clus;
            }
        }
        else
        {
            uint32_t target_cl = 0;
            if (found == 0)
            {
                if (de.attr & ATTR_DIR)
                    return -4;
                target_cl = dirent_start_cluster(&de);
            }
            if (!target_cl)
            {
                target_cl = fat_alloc_free_cluster(vol, rd, wr);
                if (!target_cl)
                    return -5;
            }
            if (write_single_cluster(vol, rd, wr, target_cl, data, bytes) != 0)
                return -6;

            // update or create entry
            if (found == 0)
            {
                uint8_t sec[MAX_SECTOR_SIZE];
                if (rd(lba + s, 1, sec))
                    return -7;
                dirent_t *e = (dirent_t *)(sec + off);
                *(uint16_t *)((uint8_t *)e + 20) = (uint16_t)((target_cl >> 16) & 0xFFFF);
                *(uint16_t *)((uint8_t *)e + 26) = (uint16_t)(target_cl & 0xFFFF);
                uint32_t maxb = vol->sec_per_clus * vol->bytes_per_sec;
                *(uint32_t *)((uint8_t *)e + 28) = (bytes < maxb) ? bytes : maxb;
                if (wr(lba + s, 1, sec))
                    return -8;
            }
            else
            {
                uint32_t maxb = vol->sec_per_clus * vol->bytes_per_sec;
                if (create_dir_entry(vol, rd, wr, dir_cl, name11, ATTR_ARCHIVE, target_cl, (bytes < maxb) ? bytes : maxb) != 0)
                    return -9;
            }
            return 0;
        }
    }
    return -10;
}

int fat32_list_dir_path(fat32_vol_t *vol, disk_read_fn rd, const char *path,
                        fat32_dirent_t *out, int max_items, int *out_count)
{
    if (vol && vol->fs_type == FAT_FS_EXFAT)
        return exfat_list_dir_path(vol, rd, path, out, max_items, out_count);
    if (out_count)
        *out_count = 0;
    if (!vol || !rd || !path || !out || max_items <= 0)
        return -1;
    const char *p = skip_drive_prefix(path);
    uint32_t dir_cl = vol->root_clus;
    char comp[13];
    while (next_path_component(&p, comp))
    {
        char name11[11];
        make_name83(name11, comp);
        dirent_t de;
        int r = dir_find_entry(vol, rd, dir_cl, name11, &de, NULL, NULL, NULL);
        if (r != 0)
            return -2;
        int more = path_has_more(p);
        if (!more)
        {
            if (!(de.attr & ATTR_DIR))
                return -3;
            dir_cl = dirent_start_cluster(&de);
            if (!dir_cl)
                dir_cl = vol->root_clus;
            break;
        }
        if (!(de.attr & ATTR_DIR))
            return -4;
        dir_cl = dirent_start_cluster(&de);
        if (!dir_cl)
            dir_cl = vol->root_clus;
    }

    // list directory
    int n = 0;
    uint8_t sec[MAX_SECTOR_SIZE];
    uint32_t cl = dir_cl;
    while (!is_end_cluster(cl))
    {
        uint32_t lba = clus_to_lba(vol, cl);
        for (uint8_t s = 0; s < vol->sec_per_clus; ++s)
        {
            if (rd(lba + s, 1, sec))
                goto done_list;
            for (int off = 0; off < vol->bytes_per_sec; off += 32)
            {
                if (n >= max_items)
                    goto done_list;
                dirent_t *de = (dirent_t *)(sec + off);
                if (de->name[0] == 0x00)
                    goto done_list;
                if ((uint8_t)de->name[0] == 0xE5)
                    continue;
                if (de->attr == ATTR_LONG_NAME)
                    continue;
                if (de->name[0] == '.')
                    continue; // skip . and .. for UI
                fat32_dirent_t *e = &out[n++];
                char base[9];
                char ext[4];
                memcpy(base, de->name, 8);
                base[8] = 0;
                memcpy(ext, de->name + 8, 3);
                ext[3] = 0;
                for (int i = 7; i >= 0; --i)
                {
                    if (base[i] == ' ')
                        base[i] = 0;
                    else
                        break;
                }
                for (int i = 2; i >= 0; --i)
                {
                    if (ext[i] == ' ')
                        ext[i] = 0;
                    else
                        break;
                }
                const int maxlen = (int)sizeof(e->name83) - 1;
                int pidx = 0;
                for (int i = 0; base[i] && pidx < maxlen; ++i)
                    e->name83[pidx++] = base[i];
                if (ext[0] && pidx < maxlen)
                    e->name83[pidx++] = '.';
                for (int i = 0; ext[i] && pidx < maxlen; ++i)
                    e->name83[pidx++] = ext[i];
                e->name83[pidx] = 0;
                e->attr = de->attr;
                e->size = de->file_size;
            }
        }
        cl = fat_read_fat_entry(vol, rd, cl);
    }
done_list:
    if (out_count)
        *out_count = n;
    return 0;
}
