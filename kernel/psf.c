#include <stddef.h>
#include <stdint.h>
#include "psf.h"
#include "serial.h"

/* objcopy로 심볼 이름을 통일했을 때 */
extern unsigned char _binary_font_psf_start[];
extern unsigned char _binary_font_psf_end[];

/* PSF v1 헤더 (linux/drivers/video/console/fonts 참고) */
typedef struct __attribute__((packed)) {
    uint8_t magic[2];   // 0x36,0x04
    uint8_t mode;       // 0: 256 glyphs, 1: 512 glyphs
    uint8_t charsize;   // glyph bytes (height)
} psf1_hdr_t;

/* 커스텀 GRAY8 헤더 (빌드타임 TTF → 비트맵 변환기 출력) */
typedef struct __attribute__((packed)) {
    uint32_t magic;     // 'GFNT'
    uint16_t version;   // 1
    uint16_t format;    // 1 = GRAY8
    uint16_t width;     // 글리프 폭 (px)
    uint16_t height;    // 글리프 높이 (px)
    uint16_t count;     // 글리프 개수 (ex: 256)
    uint16_t reserved;  // 패딩
} gfnt_hdr_t;

static const uint8_t* g_glyphs;
static int g_w = 8;
static int g_h = 16;
static int g_pitch = 1;          // 행당 바이트
static int g_count = 256;
static int g_ready = 0;
static int g_fmt = PSF_FMT_PSF1; // psf.h enum

static int load_psf1(const uint8_t *base, size_t sz)
{
    if (sz < sizeof(psf1_hdr_t))
        return -1;
    const psf1_hdr_t *hdr = (const psf1_hdr_t *)base;
    if (!(hdr->magic[0] == 0x36 && hdr->magic[1] == 0x04))
        return -1;

    g_fmt   = PSF_FMT_PSF1;
    g_w     = 8;
    g_h     = hdr->charsize ? hdr->charsize : 16;
    g_pitch = 1;
    g_count = (hdr->mode & 1) ? 512 : 256;
    g_glyphs = (const uint8_t *)(hdr + 1);

    serial_printf("[psf] PSF1 font: %d glyphs, %dx%d\n", g_count, g_w, g_h);
    return 0;
}

static int load_gfnt(const uint8_t *base, size_t sz)
{
    if (sz < sizeof(gfnt_hdr_t))
        return -1;
    const gfnt_hdr_t *hdr = (const gfnt_hdr_t *)base;
    if (hdr->magic != 0x544E4647u) // 'GFNT'
        return -1;
    if (hdr->format != 1 || hdr->width == 0 || hdr->height == 0)
        return -1;

    size_t glyph_sz = (size_t)hdr->width * (size_t)hdr->height;
    size_t need = sizeof(gfnt_hdr_t) + glyph_sz * hdr->count;
    if (sz < need) {
        serial_printf("[psf] gfnt too small: have=%zu need=%zu\n", sz, need);
        return -1;
    }

    g_fmt   = PSF_FMT_GRAY8;
    g_w     = hdr->width;
    g_h     = hdr->height;
    g_pitch = hdr->width;
    g_count = hdr->count;
    g_glyphs = (const uint8_t *)(hdr + 1);

    serial_printf("[psf] GFNT font: %d glyphs, %dx%d (fmt=GRAY8)\n",
                  g_count, g_w, g_h);
    return 0;
}

int psf_init(void)
{
    const uint8_t *base = _binary_font_psf_start;
    const uint8_t *end  = _binary_font_psf_end;
    size_t blob_size = (size_t)(end - base);

    int ret = load_gfnt(base, blob_size);
    if (ret != 0)
        ret = load_psf1(base, blob_size);

    if (ret != 0) {
        serial_printf("[psf] unsupported font blob\n");
        return -1;
    }

    g_ready = 1;
    return 0;
}

int psf_width(void){ return g_w; }
int psf_height(void){ return g_h; }
int psf_pitch(void){ return g_pitch; }
int psf_format(void){ return g_fmt; }

int psf_ready(void){ return g_ready; }

const uint8_t* psf_glyph(char c){
    uint32_t uc = (uint8_t)c;
    if (uc >= (uint32_t)g_count) uc = '?';
    size_t stride = (size_t)g_pitch * (size_t)g_h;
    return g_glyphs + stride * uc;
}
