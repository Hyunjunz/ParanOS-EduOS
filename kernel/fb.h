#pragma once
#include <stdint.h>
#include <stddef.h>

typedef struct {
    uint32_t width, height, pitch, bpp;
    uint8_t* front;   // LFB(물리) 매핑된 VA
    uint8_t* back;    // 백버퍼(고정 가상 메모리)
} fb_t;
#define FB_FIXED_VA 0xE0000000u 
extern fb_t fb;

/* 프레임버퍼 매핑 및 백버퍼 준비 */
int fb_map(uint64_t phys, uint32_t w, uint32_t h, uint32_t pitch, uint32_t bpp);


/* 그리기 유틸 */
void fb_putpixel(int x, int y, uint32_t argb);
void fb_flush(void);
void draw_rect(int x, int y, int w, int h, uint32_t argb);
// Front-buffer helpers for lightweight cursor drawing
void fb_putpixel_front(int x, int y, uint32_t argb);
void fb_blit_rect_to_front(int x, int y, int w, int h);
void fb_copy_rect_front(int sx, int sy, int w, int h, int dx, int dy);
void fb_draw_cursor_front(int x, int y);
void fb_set_cursor_image(uint32_t *argb, int w, int h);
void draw_rect_front(int x, int y, int w, int h, uint32_t argb);
void draw_text_front(int px, int py, const char *s, uint32_t fg, uint32_t bg);

/* Framebuffer metadata helpers */
uint32_t *fb_get_addr(void);
uint32_t fb_get_width(void);
uint32_t fb_get_height(void);

/* 텍스트 (PSF 폰트 사용) */
void draw_text(int x, int y, const char* s, uint32_t fg, uint32_t bg);
void put_pixel_raw(uint8_t* buf, int x, int y, uint32_t color);
void *memcpy_exact(void *dst, const void *src, size_t n);
