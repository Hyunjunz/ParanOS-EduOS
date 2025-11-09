#include <stdint.h>
#include <stddef.h>
#include "fb.h"
#include "psf.h"
#include "serial.h"
#include "mm/vmm.h" 
#include "bootinfo.h"

extern void vmm_map_page(uint32_t va, uint32_t pa, uint32_t flags);
extern void *kmalloc(size_t sz);
#define VMM_RW 0x3
#define VMM_RW_NOCACHE (VMM_RW | (1 << 3) | (1 << 4)) // bit3=PWT, bit4=PCD

fb_t fb;



int fb_map(uint32_t phys, uint32_t w, uint32_t h, uint32_t pitch, uint32_t bpp)
{
    if (!phys || !w || !h || !pitch || (bpp != 32 && bpp != 24))
        return -1;

    fb.width = w;
    fb.height = h;
    fb.pitch = pitch;
    fb.bpp = bpp;

    uint32_t pa_base = phys & ~0xFFFu;
    uint32_t off = phys & 0xFFFu;
    size_t sz = (size_t)h * pitch;
    size_t map_sz = sz + off;
    size_t pages = (map_sz + 0xFFFu) >> 12;
    uint32_t va_base = FB_FIXED_VA;

    // ───────────────────────────────────────────────
    // 페이지 매핑 (이미 매핑되어 있으면 skip)
    // ───────────────────────────────────────────────
    extern int vmm_query(uintptr_t va, uintptr_t* pa_out, uint32_t* flags_out);
    extern int vmm_unmap(uintptr_t virt);

    for (size_t i = 0; i < pages; ++i)
    {
        uint32_t va = va_base + (uint32_t)(i << 12);
        uint32_t pa = pa_base + (uint32_t)(i << 12);

        uint32_t cur_pa, cur_flags;
        if (vmm_query(va, &cur_pa, &cur_flags) == 0 && (cur_flags & 1))
        {
            serial_printf("[fb_map] skip VA=%08x (already %08x)\n", va, cur_pa);
            continue;
        }

        vmm_map_page(va, pa, VMM_RW_NOCACHE);
    }

    fb.front = (uint8_t *)(uintptr_t)(va_base + off);
    fb.back = (uint8_t *)kmalloc(sz);

    serial_printf("[fb_map] fb.front=%08x fb.back=%08x\n", fb.front, fb.back);
    serial_printf("[fb_map] width=%u height=%u pitch=%u bpp=%u\n",
                  fb.width, fb.height, fb.pitch, fb.bpp);

    if (!fb.back)
    {
        serial_printf("[fb_map] warning: no backbuffer, using front buffer directly\n");
        fb.back = fb.front;
    }

    serial_printf("[fb_map] front=%08x back=%08x size=%u\n",
                  fb.front, fb.back, (uint32_t)sz);

    return 0;
}

void put_pixel_raw(uint8_t *buf, int x, int y, uint32_t color)
{
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

void fb_clear(uint32_t argb)
{
    for (uint32_t y = 0; y < fb.height; ++y)
        for (uint32_t x = 0; x < fb.width; ++x)
            put_pixel_raw(fb.back, x, y, argb);
}

void fb_putpixel(int x, int y, uint32_t color)
{
    if (x < 0 || y < 0 || x >= (int)fb.width || y >= (int)fb.height)
        return;

    uint8_t *p = fb.front + (size_t)y * fb.pitch + (size_t)x * (fb.bpp / 8);
    uint8_t r = (color >> 16) & 0xFF;
    uint8_t g = (color >> 8) & 0xFF;
    uint8_t b = (color >> 0) & 0xFF;

    if (fb.bpp == 32) {
        p[0] = b;
        p[1] = g;
        p[2] = r;
        p[3] = 0xFF;
    } else if (fb.bpp == 24) {
        p[0] = b;
        p[1] = g;
        p[2] = r;
    }
}


void draw_rect(int x, int y, int w, int h, uint32_t argb)
{
    if (w <= 0 || h <= 0)
        return;
    int x2 = x + w, y2 = y + h;
    if (x < 0)
        x = 0;
    if (y < 0)
        y = 0;
    if (x2 > (int)fb.width)
        x2 = fb.width;
    if (y2 > (int)fb.height)
        y2 = fb.height;
    for (int yy = y; yy < y2; ++yy)
        for (int xx = x; xx < x2; ++xx)
            put_pixel_raw(fb.back, xx, yy, argb);
}

void fb_flush(void)
{
    extern void *memcpy(void *, const void *, size_t);
    size_t sz = (size_t)fb.height * fb.pitch;
    memcpy(fb.front, fb.back, sz);
}

void draw_text(int px, int py, const char *s, uint32_t fg, uint32_t bg)
{
    int fw = psf_width();
    int fh = psf_height();
    for (int i = 0; s[i]; ++i)
    {
        const uint8_t *g = psf_glyph(s[i]);
        for (int r = 0; r < fh; ++r)
        {
            uint8_t line = g[r];
            for (int c = 0; c < 8; ++c)
            {
                uint32_t color = (line & (0x80 >> c)) ? fg : bg;
                fb_putpixel(px + c, py + r, color);
            }
        }
        px += fw;
    }
}

uint32_t* fb_get_addr(void) {
    return (uint32_t*)fb.front;
}

uint32_t fb_get_width(void) {
    return fb.width;
}

uint32_t fb_get_height(void) {
    return fb.height;
}
