#pragma once
#include <stdint.h>

/* PSF1 기반 8xN 폰트 사용 */
int  psf_init(void);               // 성공=0
int  psf_width(void);              // 보통 8
int  psf_height(void);             // ex) 14/16
const uint8_t* psf_glyph(char c);  // 글리프 비트맵 포인터
