#include "desktop.h"
#include "fb.h"
#include "psf.h"
#include "ui.h"
#include "config.h"
#include "fs_fat32.h"
#include "ata.h"
#include "serial.h"
#include "mm/pmm.h"
#include "mm/vmm.h"
#include "string.h"
#include "stdlib.h"

extern void *kmalloc(size_t sz);
extern void *memcpy_exact(void *dst, const void *src, size_t n);
extern volatile uint64_t jiffies;

#define DESKTOP_MAX_ITEMS 128

typedef struct
{
    int x, y, w, h;
} rect_t;

static desktop_item_t g_items[DESKTOP_MAX_ITEMS];
static int g_item_count = 0;
static int g_selection = -1;
static rect_t g_rects[DESKTOP_MAX_ITEMS];
static int g_layout_valid = 0;

static uint8_t *desktop_bg_cache = NULL;
static size_t desktop_bg_cache_bytes = 0;
static int desktop_bg_dirty = 1;

static uint64_t desktop_frame_ticks = 1;      // max refresh (~100fps at 100Hz PIT)
static uint64_t last_click_tick = 0;
static int last_click_index = -1;
static const int double_click_threshold = 50; // ~500ms at 100Hz

static const uint32_t THEME_ACCENT = 0xFF4C8DFF;
static const uint32_t THEME_TEXT = 0xFF2F3645;
static const uint32_t THEME_SURFACE = 0xFFF6F8FC;
static const uint32_t THEME_SURFACE_MUTED = 0xFFE7EDF6;
static const uint32_t THEME_CARD = 0xFFF0F3F9;

// Wallpaper cache (converted to ARGB)
static uint32_t *wallpaper = NULL;
static uint32_t wallpaper_w = 0;
static uint32_t wallpaper_h = 0;
static int wallpaper_loaded = 0;

static void *wp_alloc(size_t sz)
{
    void *p = kmalloc(sz);
    if (p)
        return p;
    void *phys = ext_mem_alloc(sz);
    if (!phys)
        return NULL;
    uintptr_t hhdm = vmm_hhdm_offset();
    return (void *)((uintptr_t)phys + hhdm);
}

static int ensure_desktop_bg_cache(void)
{
    size_t frame_bytes = (size_t)fb.pitch * (size_t)fb.height;
    if (frame_bytes == 0)
        return 0;

    if (desktop_bg_cache && desktop_bg_cache_bytes == frame_bytes)
        return 1;

    uint8_t *buf = kmalloc(frame_bytes);
    if (buf)
    {
        desktop_bg_cache = buf;
        desktop_bg_cache_bytes = frame_bytes;
        return 1;
    }

    desktop_bg_cache = NULL;
    desktop_bg_cache_bytes = 0;
    return 0;
}

static void desktop_layout_icons(void)
{
    int margin_x = 28, margin_y = 56;
    int icon_w = 44, icon_h = 44;
    int cell_w = icon_w + 60;
    int cell_h = icon_h + 42;
    int x = margin_x, y = margin_y;
    int max_w = (fb.width > (uint32_t)margin_x) ? (int)fb.width - margin_x : 0;

    for (int i = 0; i < g_item_count; ++i)
    {
        g_rects[i].x = x;
        g_rects[i].y = y;
        g_rects[i].w = icon_w;
        g_rects[i].h = icon_h + 16;

        x += cell_w;
        if (x + cell_w > max_w)
        {
            x = margin_x;
            y += cell_h;
        }
    }
    g_layout_valid = 1;
}

static void desktop_draw_icons(void)
{
    if (!g_layout_valid)
        desktop_layout_icons();

    for (int i = 0; i < g_item_count; ++i)
    {
        uint32_t ic = (g_items[i].attr & 0x10) ? 0xFF6FA9FF : 0xFF7BC67E; // softer dir/file colors
        int x = g_rects[i].x;
        int y = g_rects[i].y;
        int icon_w = g_rects[i].w;
        int icon_h = g_rects[i].h - 16;

        int tile_x = x - 8;
        int tile_y = y - 8;
        int tile_w = icon_w + 16;
        int tile_h = icon_h + 22;

        if (i == g_selection)
        {
            ui_fill_round_rect(tile_x, tile_y, tile_w, tile_h, 10, ui_shade_color(THEME_ACCENT, +18));
            ui_fill_round_rect(tile_x + 1, tile_y + 1, tile_w - 2, tile_h - 2, 9, THEME_SURFACE);
        }
        else
        {
            ui_fill_round_rect(tile_x, tile_y, tile_w, tile_h, 10, THEME_CARD);
        }

        ui_fill_round_rect(x, y, icon_w, icon_h, 10, ic);
        draw_rect(x, y, icon_w, 2, ui_shade_color(ic, +14));

        draw_text(x, y + icon_h + 6, g_items[i].name, THEME_TEXT, 0x00000000);
    }
}

void desktop_init(void)
{
    g_item_count = 0;
    g_selection = -1;
    desktop_bg_dirty = 1;
    g_layout_valid = 0;
}

void desktop_config_frame_rate(void)
{
    char *fps_s = config_get_value(NULL, 0, "GUI_FPS");
    if (!fps_s)
        return;

    int fps = atoi(fps_s);
    if (fps <= 0)
        return;

    uint64_t ticks = 100u / (uint64_t)fps;
    if (ticks == 0)
        ticks = 1;
    if (ticks == 0)
        ticks = 1;
    // Prefer fastest available to keep cursor smooth.
    if (ticks < desktop_frame_ticks)
        desktop_frame_ticks = ticks;
    else
        desktop_frame_ticks = 1;
}

uint64_t desktop_frame_ticks_value(void)
{
    return desktop_frame_ticks ? desktop_frame_ticks : 1;
}

void desktop_set_items(const desktop_item_t *items, int count)
{
    if (!items || count <= 0)
    {
        g_item_count = 0;
        g_selection = -1;
        desktop_bg_dirty = 1;
        g_layout_valid = 0;
        return;
    }

    if (count > DESKTOP_MAX_ITEMS)
        count = DESKTOP_MAX_ITEMS;

    for (int i = 0; i < count; ++i)
        g_items[i] = items[i];

    g_item_count = count;
    g_selection = -1;
    desktop_bg_dirty = 1;
    g_layout_valid = 0;
}

int desktop_item_count(void)
{
    return g_item_count;
}

const desktop_item_t *desktop_get_item(int idx)
{
    if (idx < 0 || idx >= g_item_count)
        return NULL;
    return &g_items[idx];
}

int desktop_hit_test(int mx, int my)
{
    if (!g_layout_valid)
        desktop_layout_icons();

    for (int i = 0; i < g_item_count; ++i)
    {
        rect_t r = g_rects[i];
        if (mx >= r.x && mx < r.x + r.w && my >= r.y && my < r.y + r.h)
            return i;
    }
    return -1;
}

int desktop_is_double_click(int idx)
{
    uint64_t now = jiffies;
    int dc = (idx == last_click_index) && ((now - last_click_tick) <= (uint64_t)double_click_threshold);
    last_click_tick = now;
    last_click_index = idx;
    return dc;
}

void desktop_set_selection(int idx)
{
    if (idx < 0 || idx >= g_item_count)
        g_selection = -1;
    else
        g_selection = idx;
    desktop_bg_dirty = 1;
}

int desktop_get_selection(void)
{
    return g_selection;
}

void desktop_mark_dirty(void)
{
    desktop_bg_dirty = 1;
}

int desktop_dirty(void)
{
    return desktop_bg_dirty;
}

static int load_bmp_header(const uint8_t *buf, uint32_t len,
                           uint32_t *out_size, uint32_t *out_data_off,
                           int *out_w, int *out_h, uint16_t *out_bpp,
                           uint32_t *out_palette_colors)
{
    if (len < 54)
        return -1;
    if (buf[0] != 'B' || buf[1] != 'M')
        return -1;
    uint32_t file_size = *(const uint32_t *)(buf + 2);
    uint32_t data_off = *(const uint32_t *)(buf + 10);
    uint32_t hdr_size = *(const uint32_t *)(buf + 14);
    if (hdr_size < 40)
        return -1;
    int32_t w = *(const int32_t *)(buf + 18);
    int32_t h = *(const int32_t *)(buf + 22);
    uint16_t planes = *(const uint16_t *)(buf + 26);
    uint16_t bpp = *(const uint16_t *)(buf + 28);
    uint32_t comp = *(const uint32_t *)(buf + 30);
    if (planes != 1 || (bpp != 4 && bpp != 8 && bpp != 24 && bpp != 32) || comp != 0)
        return -1;
    if (w <= 0 || h == 0)
        return -1;
    uint32_t clr_used = 0;
    if (hdr_size >= 40)
        clr_used = *(const uint32_t *)(buf + 46);
    if (out_palette_colors)
    {
        if (bpp == 4 || bpp == 8)
            *out_palette_colors = (clr_used != 0) ? clr_used : (1u << bpp);
        else
            *out_palette_colors = 0;
    }
    *out_size = file_size;
    *out_data_off = data_off;
    *out_w = w;
    *out_h = h;
    *out_bpp = bpp;
    return 0;
}

static int load_bmp_into_bg(const uint8_t *file_buf, uint32_t file_size,
                            uint32_t data_off, int w, int h, uint16_t bpp,
                            uint32_t palette_colors)
{
    if (!file_buf || file_size == 0)
        return -1;
    if (data_off >= file_size)
        return -1;
    if (file_size < 18)
        return -1;

    uint32_t hdr_size = *(const uint32_t *)(file_buf + 14);
    if (hdr_size > file_size || 14u + hdr_size > file_size)
        return -1;
    const uint8_t *palette = NULL;
    if (bpp == 4 || bpp == 8)
    {
        uint32_t palette_bytes = palette_colors * 4;
        uint32_t palette_start = 14 + hdr_size;
        if ((uint64_t)palette_start + palette_bytes > file_size)
            return -1;
        if ((uint64_t)palette_start + palette_bytes > data_off)
            return -1;
        palette = file_buf + palette_start;
        if (palette_colors == 0)
            return -1;
    }

    const uint8_t *data = file_buf + data_off;
    int abs_h = (h < 0) ? -h : h;
    uint32_t row_stride = ((uint32_t)bpp * (uint32_t)w + 31) / 32 * 4;
    if ((uint64_t)data_off + (uint64_t)row_stride * abs_h > file_size)
        return -1;

    uint32_t target_w = (uint32_t)w;
    uint32_t target_h = (uint32_t)abs_h;
    uint64_t max_px = (fb.width && fb.height)
                      ? (uint64_t)fb.width * (uint64_t)fb.height
                      : 1920ull * 1080ull;
    while ((uint64_t)target_w * (uint64_t)target_h > max_px)
    {
        target_w = (target_w > 1) ? (target_w + 1) / 2 : 1;
        target_h = (target_h > 1) ? (target_h + 1) / 2 : 1;
    }

    uint32_t *pix = NULL;
    uint32_t attempt_w = target_w, attempt_h = target_h;
    while (attempt_w >= 1 && attempt_h >= 1)
    {
        uint64_t need_px = (uint64_t)attempt_w * (uint64_t)attempt_h;
        pix = wp_alloc(need_px * sizeof(uint32_t));
        if (pix)
            break;
        if (attempt_w == 1 && attempt_h == 1)
            break;
        attempt_w = (attempt_w > 1) ? (attempt_w + 1) / 2 : 1;
        attempt_h = (attempt_h > 1) ? (attempt_h + 1) / 2 : 1;
    }
    if (!pix)
        return -1;
    target_w = attempt_w;
    target_h = attempt_h;

    for (uint32_t y = 0; y < target_h; ++y)
    {
        uint32_t sy = (uint32_t)(((uint64_t)y * (uint64_t)abs_h) / target_h);
        int src_y = (h > 0) ? (abs_h - 1 - (int)sy) : (int)sy;
        const uint8_t *src = data + (uint32_t)src_y * row_stride;
        uint32_t *dst = pix + y * target_w;
        if (bpp == 24)
        {
            for (uint32_t x = 0; x < target_w; ++x)
            {
                uint32_t sx = (uint32_t)(((uint64_t)x * (uint64_t)w) / target_w);
                uint8_t b = src[sx * 3 + 0];
                uint8_t g = src[sx * 3 + 1];
                uint8_t r = src[sx * 3 + 2];
                dst[x] = 0xFF000000 | ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
            }
        }
        else if (bpp == 32)
        {
            for (uint32_t x = 0; x < target_w; ++x)
            {
                uint32_t sx = (uint32_t)(((uint64_t)x * (uint64_t)w) / target_w);
                uint8_t b = src[sx * 4 + 0];
                uint8_t g = src[sx * 4 + 1];
                uint8_t r = src[sx * 4 + 2];
                uint8_t a = src[sx * 4 + 3];
                if (a == 0) a = 0xFF;
                dst[x] = ((uint32_t)a << 24) | ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
            }
        }
        else if (bpp == 8)
        {
            for (uint32_t x = 0; x < target_w; ++x)
            {
                uint32_t sx = (uint32_t)(((uint64_t)x * (uint64_t)w) / target_w);
                uint8_t idx = src[sx];
                if (idx >= palette_colors)
                    idx = 0;
                const uint8_t *ent = palette + (uint32_t)idx * 4;
                uint8_t b = ent[0], g = ent[1], r = ent[2], a = ent[3];
                if (a == 0) a = 0xFF;
                dst[x] = ((uint32_t)a << 24) | ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
            }
        }
        else if (bpp == 4)
        {
            for (uint32_t x = 0; x < target_w; ++x)
            {
                uint32_t sx = (uint32_t)(((uint64_t)x * (uint64_t)w) / target_w);
                uint8_t byte = src[sx / 2];
                uint8_t idx = (sx & 1) ? (byte & 0x0F) : (byte >> 4);
                if (idx >= palette_colors)
                    idx = 0;
                const uint8_t *ent = palette + (uint32_t)idx * 4;
                uint8_t b = ent[0], g = ent[1], r = ent[2], a = ent[3];
                if (a == 0) a = 0xFF;
                dst[x] = ((uint32_t)a << 24) | ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
            }
        }
    }

    wallpaper = pix;
    wallpaper_w = target_w;
    wallpaper_h = target_h;
    wallpaper_loaded = 1;
    desktop_mark_dirty();
    return 0;
}

int desktop_load_wallpaper(fat32_vol_t *vol, disk_read_fn rd, const char *name83)
{
    if (!vol || !rd || !name83)
        return -1;

    uint8_t hdr[512];
    uint32_t read = 0;
    if (fat32_read_file(vol, rd, name83, hdr, sizeof(hdr), &read) != 0)
    {
        serial_printf("[WALLPAPER] read header failed\n");
        return -1;
    }

    uint32_t file_size = 0, data_off = 0;
    int w = 0, h = 0;
    uint16_t bpp = 0;
    uint32_t palette_colors = 0;
    if (load_bmp_header(hdr, read, &file_size, &data_off, &w, &h, &bpp, &palette_colors) != 0)
    {
        serial_printf("[WALLPAPER] header parse failed\n");
        return -1;
    }
    serial_printf("[WALLPAPER] hdr ok: size=%u data_off=%u %dx%d bpp=%u pal=%u\n",
                  file_size, data_off, w, h, bpp, palette_colors);

    if (file_size == 0 || file_size > 16 * 1024 * 1024) // simple guard
    {
        serial_printf("[WALLPAPER] bad size %u\n", file_size);
        return -1;
    }

    uint8_t *file_buf = wp_alloc(file_size);
    if (!file_buf)
    {
        serial_printf("[WALLPAPER] file buf alloc failed (%u bytes)\n", file_size);
        return -1;
    }

    uint32_t full_read = 0;
    if (fat32_read_file(vol, rd, name83, file_buf, file_size, &full_read) != 0 || full_read < file_size)
    {
        serial_printf("[WALLPAPER] full read failed (%u/%u)\n", full_read, file_size);
        return -1;
    }
    serial_printf("[WALLPAPER] file read ok (%u bytes)\n", full_read);

    return load_bmp_into_bg(file_buf, file_size, data_off, w, h, bpp, palette_colors);
}

int desktop_load_wallpaper_path(fat32_vol_t *vol, disk_read_fn rd, const char *path)
{
    if (!vol || !rd || !path)
        return -1;

    uint8_t hdr[512];
    uint32_t read = 0;
    if (fat32_read_file_path(vol, rd, path, hdr, sizeof(hdr), &read) != 0)
        return -1;

    uint32_t file_size = 0, data_off = 0;
    int w = 0, h = 0;
    uint16_t bpp = 0;
    uint32_t palette_colors = 0;
    if (load_bmp_header(hdr, read, &file_size, &data_off, &w, &h, &bpp, &palette_colors) != 0)
        return -1;
    if (file_size == 0 || file_size > 16 * 1024 * 1024)
        return -1;

    uint8_t *file_buf = wp_alloc(file_size);
    if (!file_buf)
        return -1;

    uint32_t full_read = 0;
    if (fat32_read_file_path(vol, rd, path, file_buf, file_size, &full_read) != 0 || full_read < file_size)
        return -1;

    int ret = load_bmp_into_bg(file_buf, file_size, data_off, w, h, bpp, palette_colors);
    return ret;
}

static void draw_wallpaper_letterbox(void)
{
    if (!wallpaper || wallpaper_w == 0 || wallpaper_h == 0 || !fb.back)
        return;

    // Base fill to handle bars
    uint32_t base_top = 0xFFEFF3F9;
    uint32_t base_bot = 0xFFE3EAF4;
    ui_draw_hgrad_rect(0, 0, fb.width, fb.height, base_top, base_bot);

    // Compute fit (preserve aspect)
    uint32_t fit_w = fb.width;
    uint32_t fit_h = (uint32_t)(((uint64_t)wallpaper_h * fit_w + wallpaper_w / 2) / wallpaper_w);
    if (fit_h > fb.height)
    {
        fit_h = fb.height;
        fit_w = (uint32_t)(((uint64_t)wallpaper_w * fit_h + wallpaper_h / 2) / wallpaper_h);
    }

    int dst_x = ((int)fb.width - (int)fit_w) / 2;
    int dst_y = ((int)fb.height - (int)fit_h) / 2;
    if (dst_x < 0) dst_x = 0;
    if (dst_y < 0) dst_y = 0;

    for (uint32_t y = 0; y < fit_h; ++y)
    {
        uint32_t src_y = (uint32_t)(((uint64_t)y * wallpaper_h) / fit_h);
        uint32_t *row = wallpaper + src_y * wallpaper_w;
        for (uint32_t x = 0; x < fit_w; ++x)
        {
            uint32_t src_x = (uint32_t)(((uint64_t)x * wallpaper_w) / fit_w);
            uint32_t c = row[src_x];
            put_pixel_raw(fb.back, dst_x + (int)x, dst_y + (int)y, c);
        }
    }
}

static void desktop_draw_backdrop(void)
{
    // Wallpaper if available, else gradient backdrop
    if (wallpaper_loaded)
    {
        draw_wallpaper_letterbox();
    }
    else
    {
        // Base gradient: soft light neutral
        uint32_t top_bg = 0xFFF7F9FC;
        uint32_t bot_bg = 0xFFE7ECF4;
        ui_draw_hgrad_rect(0, 0, fb.width, fb.height, top_bg, bot_bg);

        // Soft diagonal glow
        int glow_w = (int)(fb.width / 2);
        int glow_h = (int)(fb.height / 2);
        uint32_t glow_top = 0xFFDFEAF9;
        uint32_t glow_bot = 0xFFC9D8F2;
        ui_draw_hgrad_rect((int)fb.width - glow_w, 0, glow_w, glow_h, glow_top, glow_bot);
        ui_draw_hgrad_rect(0, (int)fb.height - glow_h, glow_w, glow_h, glow_bot, glow_top);
    }

    // top bar
    int bar_h = 32;
    ui_draw_hgrad_rect(0, 0, fb.width, bar_h, 0xFFFFFFFF, 0xFFE8EDF5);
    draw_rect(0, bar_h - 1, fb.width, 1, 0xFFD5DEEA);
    draw_text(12, 8, "ParanOS", THEME_ACCENT, 0x00000000);

    // start/taskbar base
    int taskbar_h = 34;
    int taskbar_y = (int)fb.height - taskbar_h;
    ui_draw_hgrad_rect(0, taskbar_y, (int)fb.width, taskbar_h, 0xFFFDFEFE, 0xFFE7EDF6);
    draw_rect(0, taskbar_y, fb.width, 1, 0xFFD5DEEA);
    draw_text(12, taskbar_y + 9, "Launch", THEME_ACCENT, 0x00000000);

    desktop_draw_icons();
}

void desktop_draw_background(void)
{
    if (!fb.back)
        return;
    size_t frame_bytes = (size_t)fb.pitch * (size_t)fb.height;
    int have_cache = ensure_desktop_bg_cache();

    if (desktop_bg_dirty || !have_cache)
    {
        desktop_draw_backdrop();
        if (have_cache && desktop_bg_cache)
            memcpy_exact(desktop_bg_cache, fb.back, frame_bytes);
        desktop_bg_dirty = 0;
    }
    else if (have_cache && desktop_bg_cache)
    {
        memcpy_exact(fb.back, desktop_bg_cache, frame_bytes);
    }
}
