#pragma once
#include <stdint.h>

/* 폰트 로더: 기존 PSF1 + 빌드타임 생성된 그레이스케일 비트맵 둘 다 지원 */
enum psf_format {
    PSF_FMT_PSF1  = 0,  // 1bpp, width=8 고정
    PSF_FMT_GRAY8 = 1,  // 8bpp alpha
};

int  psf_init(void);               // 성공=0
int  psf_width(void);              // 글리프 폭
int  psf_height(void);             // 글리프 높이
int  psf_pitch(void);              // 행당 바이트 수 (GRAY8: width, PSF1: 1)
int  psf_format(void);             // enum psf_format 값
int  psf_ready(void);
const uint8_t* psf_glyph(char c);  // 글리프 비트맵 포인터
