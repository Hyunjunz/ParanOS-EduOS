#include <stdint.h>
#include <stddef.h>
#include "fb.h"
#include "psf.h"
#include "serial.h"
#include "bootinfo.h"
#include <mm/vmm.h>
#include <mm/pmm.h> 

extern void *kmalloc(size_t sz);

static size_t g_back_pages = 0;

#define PAGE_SIZE 0x1000

fb_t fb;

void *memcpy_exact(void *dst, const void *src, size_t n)
{
    if (!dst || !src) {
        serial_printf("[memcpy_exact] NULL ptr: dst=%p src=%p n=%u\n",
                      dst, src, (uint32_t)n);

        return dst;
    }
    uint8_t *d = (uint8_t *)dst;
    const uint8_t *s = (const uint8_t *)src;
    while (n--)
    {
        *d++ = *s++;
    }
    return dst;
}

static void memmove_exact(uint8_t *dst, const uint8_t *src, size_t n)
{
    if (!n || dst == src)
        return;
    if (dst < src)
    {
        while (n--)
            *dst++ = *src++;
    }
    else
    {
        dst += n;
        src += n;
        while (n--)
        {
            *--dst = *--src;
        }
    }
}

int fb_map(uint64_t phys, uint32_t w, uint32_t h, uint32_t pitch, uint32_t bpp)
{
    if (!phys || !w || !h || !pitch || (bpp != 24 && bpp != 32))
        return -1;

    fb.width  = w;
    fb.height = h;
    fb.pitch  = pitch;
    fb.bpp    = bpp;

    size_t sz = (size_t)h * pitch;

    uintptr_t hhdm = vmm_hhdm_offset();
    uintptr_t front_va;

    // Limine HHDM: phys 가 이미 HHDM 영역이면 그대로, 아니면 hhdm 더해서 VA 로 만든다
    if (phys >= hhdm)
        front_va = phys;
    else
        front_va = phys + hhdm;

    fb.front = (uint8_t *)front_va;

    serial_printf("[fb_map] framebuffer size=%u bytes\n", (uint32_t)sz);

    //------------------------------------------------------------------
    // 1) 백버퍼 확보: 먼저 커널 힙(kmalloc), 안 되면 ext_mem_alloc + HHDM
    //------------------------------------------------------------------
    void *back = kmalloc(sz);
    if (!back) {
        serial_printf("[fb_map] kmalloc(%u) failed, trying ext_mem_alloc\n",
                      (uint32_t)sz);

        void *back_phys = ext_mem_alloc(sz);
        if (back_phys) {
            back = (void *)((uintptr_t)back_phys + hhdm);
            serial_printf("[fb_map] ext_mem_alloc back_phys=%p -> back_va=%p\n",
                          back_phys, back);
        }
    }

    if (!back) {
        // 진짜 메모리 부족이면 그냥 front 를 back 으로 같이 쓴다 (최후의 수단)
        fb.back = fb.front;
        serial_printf("[fb_map] WARNING: no separate backbuffer, using front\n");
    } else {
        fb.back = (uint8_t *)back;
        serial_printf("[fb_map] backbuffer VA=%p (allocated %u KB)\n",
                      fb.back, (uint32_t)(sz / 1024));
    }

    // 여기서는 HHDM/커널 pagemap 이 이미 셋업돼 있으므로
    // 따로 vmm_map_page, map_page, vmm_reload_cr3 등을 호출할 필요가 없음

    serial_printf("[fb_map] front VA=%p phys=%p\n",
                  fb.front, (void *)(uintptr_t)phys);

    return 0;
}

void put_pixel_raw(uint8_t *buf, int x, int y, uint32_t color)
{
    if (x < 0 || y < 0 || x >= (int)fb.width || y >= (int)fb.height)
    {
        serial_printf("[put_pixel_raw] OOB: x=%d y=%d (w=%u h=%u)\n",
                      x, y, fb.width, fb.height);
        return;
    }

    uint8_t r = (color >> 16) & 0xFF;
    uint8_t g = (color >> 8) & 0xFF;
    uint8_t b = color & 0xFF;
    uint8_t *p = buf + (size_t)y * fb.pitch + (size_t)x * (fb.bpp / 8);

    if (fb.bpp == 24)
    {
        p[0] = b;
        p[1] = g;
        p[2] = r;
    }
    else if (fb.bpp == 32)
    {
        p[0] = b;
        p[1] = g;
        p[2] = r;
        p[3] = 0xFF;
    }
}

void fb_putpixel(int x, int y, uint32_t color)
{
    if (x < 0 || y < 0 || x >= (int)fb.width || y >= (int)fb.height)
        return;

    // back이 없으면 front에 직접 그리기
    uint8_t *base = fb.back ? fb.back : fb.front;
    if (!base)
        return; // 이건 진짜 프레임버퍼 자체가 없는 이상 심각한 상태

    uint8_t *p = base + (size_t)y * fb.pitch + (size_t)x * (fb.bpp / 8);
    uint8_t r = (color >> 16) & 0xFF;
    uint8_t g = (color >> 8) & 0xFF;
    uint8_t b = (color >> 0) & 0xFF;

    if (fb.bpp == 32)
    {
        p[0] = b;
        p[1] = g;
        p[2] = r;
        p[3] = 0xFF;
    }
    else if (fb.bpp == 24)
    {
        p[0] = b;
        p[1] = g;
        p[2] = r;
    }
}


void fb_putpixel_front(int x, int y, uint32_t color)
{
    if (x < 0 || y < 0 || x >= (int)fb.width || y >= (int)fb.height)
        return;
    uint8_t *p = fb.front + (size_t)y * fb.pitch + (size_t)x * (fb.bpp / 8);
    uint8_t r = (color >> 16) & 0xFF;
    uint8_t g = (color >> 8) & 0xFF;
    uint8_t b = (color >> 0) & 0xFF;
    if (fb.bpp == 32)
    {
        p[0] = b; p[1] = g; p[2] = r; p[3] = 0xFF;
    }
    else if (fb.bpp == 24)
    {
        p[0] = b; p[1] = g; p[2] = r;
    }
}

void fb_blit_rect_to_front(int x, int y, int w, int h)
{
    if (w <= 0 || h <= 0)
        return;
    if (!fb.back || fb.back == fb.front)
        return;
    int x0 = x, y0 = y, x1 = x + w, y1 = y + h;
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > (int)fb.width) x1 = fb.width;
    if (y1 > (int)fb.height) y1 = fb.height;
    size_t bytes = (size_t)(x1 - x0) * (fb.bpp / 8);
    for (int yy = y0; yy < y1; ++yy)
    {
        uint8_t *src = fb.back  + (size_t)yy * fb.pitch + (size_t)x0 * (fb.bpp / 8);
        uint8_t *dst = fb.front + (size_t)yy * fb.pitch + (size_t)x0 * (fb.bpp / 8);
        memcpy_exact(dst, src, bytes);
    }
}

void fb_copy_rect_front(int sx, int sy, int w, int h, int dx, int dy)
{
    if (w <= 0 || h <= 0)
        return;
    if (fb.bpp != 24 && fb.bpp != 32)
        return;
    int bpp = fb.bpp / 8;

    if (sx < 0 || sy < 0 || dx < 0 || dy < 0)
        return;
    if (sx + w > (int)fb.width || dx + w > (int)fb.width ||
        sy + h > (int)fb.height || dy + h > (int)fb.height)
        return;

    size_t row_bytes = (size_t)w * (size_t)bpp;

    int y_start, y_end, y_step;
    if (dy > sy)
    {
        y_start = h - 1;
        y_end = -1;
        y_step = -1;
    }
    else
    {
        y_start = 0;
        y_end = h;
        y_step = 1;
    }

    for (int i = y_start; i != y_end; i += y_step)
    {
        int sy_row = sy + i;
        int dy_row = dy + i;
        uint8_t *src = fb.front + (size_t)sy_row * fb.pitch + (size_t)sx * (size_t)bpp;
        uint8_t *dst = fb.front + (size_t)dy_row * fb.pitch + (size_t)dx * (size_t)bpp;
        memmove_exact(dst, src, row_bytes);
    }
}

static int cursor_prev_x = 0;
static int cursor_prev_y = 0;
static int cursor_has_prev = 0;
#define CUR_SIZE 3
static uint32_t *cursor_img = NULL;
static int cursor_w = 0;
static int cursor_h = 0;

void fb_set_cursor_image(uint32_t *argb, int w, int h)
{
    cursor_img = argb;
    cursor_w = w;
    cursor_h = h;
}

void fb_draw_cursor_front(int x, int y)
{
    int size = CUR_SIZE;

    if (cursor_has_prev && fb.back && fb.front && fb.back != fb.front)
    {
        int rx = cursor_prev_x - size;
        int ry = cursor_prev_y - size;
        int rw = size * 2 + 1;
        int rh = size * 2 + 1;

        // Left clip
        if (rx < 0) {
            rw -= (0 - rx);
            rx = 0;
        }

        // Top clip
        if (ry < 0) {
            rh -= (0 - ry);
            ry = 0;
        }

        // Right clip
        if (rx + rw > (int)fb.width)
            rw = fb.width - rx;

        // Bottom clip
        if (ry + rh > (int)fb.height)
            rh = fb.height - ry;

        // ★★★ 가장 중요한 안정성 체크 ★★★
        if (rw > 0 && rh > 0)
            fb_blit_rect_to_front(rx, ry, rw, rh);
    }

    cursor_prev_x = x;
    cursor_prev_y = y;
    cursor_has_prev = 1;

    if (cursor_img && cursor_w > 0 && cursor_h > 0)
    {
        for (int yy = 0; yy < cursor_h; ++yy)
        {
            int dy = y + yy;
            if (dy < 0 || dy >= (int)fb.height)
                continue;
            for (int xx = 0; xx < cursor_w; ++xx)
            {
                int dx = x + xx;
                if (dx < 0 || dx >= (int)fb.width)
                    continue;
                uint32_t px = cursor_img[yy * cursor_w + xx];
                uint8_t a = (px >> 24) & 0xFF;
                if (a == 0)
                    continue;
                fb_putpixel_front(dx, dy, px);
            }
        }
    }
    else
    {
        uint32_t color = 0xFFFFFFFF;
        for (int i = -size; i <= size; i++)
        {
            int xx = x + i;
            int yy = y;
            if (xx >= 0 && xx < (int)fb.width && yy >= 0 && yy < (int)fb.height)
                fb_putpixel_front(xx, yy, color);
        }
        for (int i = -size; i <= size; i++)
        {
            int xx = x;
            int yy = y + i;
            if (xx >= 0 && xx < (int)fb.width && yy >= 0 && yy < (int)fb.height)
            fb_putpixel_front(xx, yy, color);
        }
    }
}

static inline uint32_t read_pixel_color(const uint8_t *p)
{
    // Framebuffer는 BGR(A) 순서
    uint8_t b = p[0], g = p[1], r = p[2];
    uint8_t a = 0xFF;
    // 24bpp이면 알파가 없으니 0xFF로 채운다
    return ((uint32_t)a << 24) | ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
}

static inline void write_pixel_color(uint8_t *p, uint32_t color, int bpp)
{
    p[0] = (uint8_t)(color & 0xFF);
    p[1] = (uint8_t)((color >> 8) & 0xFF);
    p[2] = (uint8_t)((color >> 16) & 0xFF);
    if (bpp == 32)
        p[3] = (uint8_t)((color >> 24) & 0xFF);
}

static inline uint32_t blend_over(uint32_t bg, uint32_t fg, uint8_t alpha)
{
    uint32_t inv = 255 - alpha;
    uint32_t br = (bg >> 16) & 0xFF, bgc = (bg >> 8) & 0xFF, bb = bg & 0xFF;
    uint32_t fr = (fg >> 16) & 0xFF, fgc = (fg >> 8) & 0xFF, fb = fg & 0xFF;

    uint32_t r = (fr * alpha + br * inv + 127) / 255;
    uint32_t g = (fgc * alpha + bgc * inv + 127) / 255;
    uint32_t b = (fb * alpha + bb * inv + 127) / 255;
    return (0xFFu << 24) | (r << 16) | (g << 8) | b;
}

static void blend_putpixel(uint8_t *buf, int x, int y, uint32_t fg, uint32_t bg,
                           uint8_t alpha, int bg_transparent)
{
    if (x < 0 || y < 0 || x >= (int)fb.width || y >= (int)fb.height)
        return;
    int bpp = fb.bpp / 8;
    if (bpp != 3 && bpp != 4)
        return;
    uint8_t *p = buf + (size_t)y * fb.pitch + (size_t)x * bpp;
    uint32_t dst = bg_transparent ? read_pixel_color(p) : bg;
    uint32_t out = blend_over(dst, fg, alpha);
    write_pixel_color(p, out, bpp == 4 ? 32 : 24);
}


void draw_rect_front(int x, int y, int w, int h, uint32_t argb)
{
    if (w <= 0 || h <= 0) return;
    int x2 = x + w, y2 = y + h;
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if (x2 > (int)fb.width) x2 = fb.width;
    if (y2 > (int)fb.height) y2 = fb.height;
    for (int yy = y; yy < y2; ++yy)
        for (int xx = x; xx < x2; ++xx)
            fb_putpixel_front(xx, yy, argb);
}

void draw_text_front(int px, int py, const char *s, uint32_t fg, uint32_t bg)
{
    int fw = psf_width();
    int fh = psf_height();
    int pitch = psf_pitch();
    int fmt = psf_format();
    int bg_transparent = ((bg >> 24) == 0);
    for (int i = 0; s[i]; ++i)
    {
        const uint8_t *g = psf_glyph(s[i]);
        if (fmt == PSF_FMT_GRAY8) {
            for (int r = 0; r < fh; ++r) {
                const uint8_t *row = g + (size_t)r * pitch;
                for (int c = 0; c < fw; ++c) {
                    uint8_t a = row[c];
                    if (a == 0) {
                        if (!bg_transparent)
                            fb_putpixel_front(px + c, py + r, bg);
                        continue;
                    }
                    if (bg_transparent && a == 255) {
                        fb_putpixel_front(px + c, py + r, fg);
                    } else {
                        blend_putpixel(fb.front, px + c, py + r, fg, bg, a, bg_transparent);
                    }
                }
            }
        } else {
            for (int r = 0; r < fh; ++r) {
                uint8_t line = g[r];
                for (int c = 0; c < fw && c < 8; ++c) {
                    if (line & (0x80 >> c)) {
                        fb_putpixel_front(px + c, py + r, fg);
                    } else if (!bg_transparent) {
                        fb_putpixel_front(px + c, py + r, bg);
                    }
                }
            }
        }
        px += fw;
    }
}

void draw_rect(int x, int y, int w, int h, uint32_t argb)
{
    if (w <= 0 || h <= 0)
        return;
    if (!fb.back) {
        // back buffer 없으면 front에 바로 그리거나 그냥 리턴
        for (int yy = y; yy < y + h; ++yy)
            for (int xx = x; xx < x + w; ++xx)
                fb_putpixel_front(xx, yy, argb);
        return;
    }
    static int dbg_rect_once = 0;
    if (!dbg_rect_once)
    {
        dbg_rect_once = 1;
        serial_printf("[dbg] draw_rect x=%d y=%d w=%d h=%d (fb.w=%u fb.h=%u pitch=%u bpp=%u)\n",
                      x, y, w, h, fb.width, fb.height, fb.pitch, fb.bpp);
    }

    int x2 = x + w, y2 = y + h;
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if (x2 > (int)fb.width) x2 = fb.width;
    if (y2 > (int)fb.height) y2 = fb.height;
    for (int yy = y; yy < y2; ++yy)
        for (int xx = x; xx < x2; ++xx)
            put_pixel_raw(fb.back, xx, yy, argb);
}

void fb_flush(void)
{
    if (!fb.back || fb.back == fb.front)
        return; 
    if (fb.back == fb.front)
        return;

    uint8_t *dst = fb.front;
    uint8_t *src = fb.back;

    size_t row = fb.pitch;
    size_t total = (size_t)fb.height * fb.pitch;
    uint8_t *limit = fb.back + total;

    for (uint32_t y = 0; y < fb.height; ++y)
    {
        if (src + row > limit)
        {
            serial_printf("[fb_flush] OOB at y=%u src=%p (limit=%p)\n",
                          y, src, limit);
            break;
        }

        memcpy_exact(dst, src, row);

        dst += fb.pitch;
        src += fb.pitch;
    }
}

void draw_text(int px, int py, const char *s, uint32_t fg, uint32_t bg)
{
    int fw = psf_width();
    int fh = psf_height();
    int pitch = psf_pitch();
    int fmt = psf_format();
    int bg_transparent = ((bg >> 24) == 0);
    uint8_t *dst = fb.back ? fb.back : fb.front;
    for (int i = 0; s[i]; ++i)
    {
        const uint8_t *g = psf_glyph(s[i]);
        if (fmt == PSF_FMT_GRAY8) {
            for (int r = 0; r < fh; ++r) {
                const uint8_t *row = g + (size_t)r * pitch;
                for (int c = 0; c < fw; ++c) {
                    uint8_t a = row[c];
                    if (a == 0) {
                        if (!bg_transparent)
                            fb_putpixel(px + c, py + r, bg);
                        continue;
                    }
                    if (bg_transparent && a == 255) {
                        fb_putpixel(px + c, py + r, fg);
                    } else {
                        blend_putpixel(dst, px + c, py + r, fg, bg, a, bg_transparent);
                    }
                }
            }
        } else {
            for (int r = 0; r < fh; ++r) {
                uint8_t line = g[r];
                for (int c = 0; c < fw && c < 8; ++c) {
                    if (line & (0x80 >> c)) {
                        fb_putpixel(px + c, py + r, fg);
                    } else if (!bg_transparent) {
                        fb_putpixel(px + c, py + r, bg);
                    }
                }
            }
        }
        px += fw;
    }
}

uint32_t *fb_get_addr(void)
{
    return (uint32_t *)fb.front;
}

uint32_t fb_get_width(void)
{
    return fb.width;
}

uint32_t fb_get_height(void)
{
    return fb.height;
}

void fb_draw_cursor(int x, int y)
{
    if (cursor_img && cursor_w > 0 && cursor_h > 0)
    {
        for (int yy = 0; yy < cursor_h; ++yy)
        {
            int dy = y + yy;
            if (dy < 0 || dy >= (int)fb.height)
                continue;
            for (int xx = 0; xx < cursor_w; ++xx)
            {
                int dx = x + xx;
                if (dx < 0 || dx >= (int)fb.width)
                    continue;
                uint32_t px = cursor_img[yy * cursor_w + xx];
                uint8_t a = (px >> 24) & 0xFF;
                if (a == 0)
                    continue;
                fb_putpixel(dx, dy, px);
            }
        }
        return;
    }

    int size = 3;
    uint32_t color = 0xFFFFFFFF;

    for (int i = -size; i <= size; i++)
    {
        int xx = x + i;
        int yy = y;
        if (xx >= 0 && xx < (int)fb.width &&
            yy >= 0 && yy < (int)fb.height)
            fb_putpixel(xx, yy, color);
    }
    for (int i = -size; i <= size; i++)
    {
        int xx = x;
        int yy = y + i;
        if (xx >= 0 && xx < (int)fb.width &&
            yy >= 0 && yy < (int)fb.height)
            fb_putpixel(xx, yy, color);
    }
}
