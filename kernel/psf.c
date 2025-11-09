#include <stddef.h>
#include <stdint.h>
#include "serial.h"
#include "bootinfo.h" 

/* objcopy로 심볼 이름을 통일했을 때 */
extern unsigned char _binary_font_psf_start[];
extern unsigned char _binary_font_psf_end[];

#ifndef VIRT_BASE
#define VIRT_BASE 0xC0000000u
#endif

/* PSF v1 헤더 (linux/drivers/video/console/fonts 참고) */
typedef struct __attribute__((packed)) {
    uint8_t magic[2];   // 0x36,0x04
    uint8_t mode;       // 0: 256 glyphs, 1: 512 glyphs
    uint8_t charsize;   // glyph bytes (height)
} psf1_hdr_t;

static const psf1_hdr_t* g_hdr;
static const uint8_t*    g_glyphs;
static int g_w = 8;
static int g_h = 16;
static int g_count = 256;

int psf_init(void){
    uintptr_t p = (uintptr_t)_binary_font_psf_start;

    // 이미 하이하프 VA면 그대로 사용, 아니면 물리→가상 변환
    if (p < VIRT_BASE) {
        p = (uintptr_t)PA2VA(p);
    }

    g_hdr = (const psf1_hdr_t*)p;

    serial_printf("[psf] font_ptr=%08x (after cond-conv)\n", (uint32_t)p);

    // 여기서 deref
    if (!(g_hdr->magic[0] == 0x36 && g_hdr->magic[1] == 0x04)) return -1;
    g_h = g_hdr->charsize ? g_hdr->charsize : 16;
    g_count = (g_hdr->mode & 1) ? 512 : 256;
    g_glyphs = (const uint8_t*)(g_hdr + 1);

    serial_printf("PSF font: %d glyphs, %dx%d (first=%08x)\n",
                  g_count, g_w, g_h, (uint32_t)(uintptr_t)g_glyphs);
    return 0;
}

int psf_width(void){ return g_w; }
int psf_height(void){ return g_h; }

const uint8_t* psf_glyph(char c){
    uint32_t uc = (uint8_t)c;
    if (uc >= (uint32_t)g_count) uc = '?';
    return g_glyphs + (size_t)uc * g_h;
}
