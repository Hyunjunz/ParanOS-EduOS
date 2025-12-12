#pragma once

#include <stdint.h>

// Lightweight UI helpers shared by the desktop and in-kernel apps.
uint8_t ui_clamp8(int v);
uint32_t ui_shade_color(uint32_t argb, int delta);
uint32_t ui_lerp_color(uint32_t c0, uint32_t c1, int i, int n);
uint32_t ui_usage_color(uint32_t pct);

// Drawing primitives built on top of the framebuffer helpers.
void ui_draw_hgrad_rect(int x, int y, int w, int h, uint32_t top, uint32_t bottom);
void ui_draw_bar_soft(int x, int y, int w, int h, uint32_t base);
void ui_fill_round_rect(int x, int y, int w, int h, int radius, uint32_t color);
