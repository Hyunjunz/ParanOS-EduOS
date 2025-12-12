#pragma once

#include <stdint.h>
#include "fs_fat32.h"

// Desktop item metadata (derived from FAT root entries)
#define DESKTOP_MAX_ITEMS 128
typedef struct
{
    // Room for dotted 8.3 names (12 chars) plus NUL.
    char name[13];
    uint8_t attr;
    uint32_t size;
} desktop_item_t;

void desktop_init(void);

// Configure target FPS from config; falls back to defaults.
void desktop_config_frame_rate(void);
uint64_t desktop_frame_ticks_value(void);

// Icon management
void desktop_set_items(const desktop_item_t *items, int count);
int  desktop_item_count(void);
const desktop_item_t *desktop_get_item(int idx);
int  desktop_hit_test(int mx, int my);
int  desktop_is_double_click(int idx);
void desktop_set_selection(int idx);
int  desktop_get_selection(void);

// Background/cache management
void desktop_mark_dirty(void);
int  desktop_dirty(void);
void desktop_draw_background(void);

// Load wallpaper BMP from FAT32 root (8.3 uppercase). Returns 0 on success.
int desktop_load_wallpaper(fat32_vol_t *vol, disk_read_fn rd, const char *name83);
int desktop_load_wallpaper_path(fat32_vol_t *vol, disk_read_fn rd, const char *path);
