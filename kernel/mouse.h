#pragma once
#include <stdint.h>

/*
 * PS/2 Mouse Driver Header
 * -------------------------
 * 마우스 초기화, 상태 읽기, 커서 그리기용 기본 인터페이스
 */

#ifdef __cplusplus
extern "C" {
#endif

// ───────────────────────────────────────────────
// 초기화 및 IRQ 핸들러
// ───────────────────────────────────────────────
void mouse_init(uint32_t fb_w, uint32_t fb_h);
void mouse_irq_handler(void);

// ───────────────────────────────────────────────
// 상태 접근
// ───────────────────────────────────────────────
int mouse_get_x(void);
int mouse_get_y(void);
uint8_t mouse_get_buttons(void);  // bit0=left, bit1=right, bit2=middle

// ───────────────────────────────────────────────
// 커서 표시 (프레임버퍼 기반)
// ───────────────────────────────────────────────
void fb_draw_cursor(int x, int y);

#ifdef __cplusplus
}
#endif
