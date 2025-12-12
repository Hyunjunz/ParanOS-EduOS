#include "ui.h"
#include "fb.h"

// Basic helpers for light color math.
uint8_t ui_clamp8(int v) {
    return (v < 0) ? 0 : ((v > 255) ? 255 : v);
}

uint32_t ui_shade_color(uint32_t argb, int delta) {
    int a = (argb >> 24) & 0xFF;
    int r = (argb >> 16) & 0xFF;
    int g = (argb >> 8) & 0xFF;
    int b = (argb >> 0) & 0xFF;
    r = ui_clamp8(r + delta);
    g = ui_clamp8(g + delta);
    b = ui_clamp8(b + delta);
    return (uint32_t)((a << 24) | (r << 16) | (g << 8) | b);
}

uint32_t ui_lerp_color(uint32_t c0, uint32_t c1, int i, int n) {
    if (n <= 0)
        return c0;
    int a0 = (c0 >> 24) & 0xFF, r0 = (c0 >> 16) & 0xFF, g0 = (c0 >> 8) & 0xFF, b0 = c0 & 0xFF;
    int a1 = (c1 >> 24) & 0xFF, r1 = (c1 >> 16) & 0xFF, g1 = (c1 >> 8) & 0xFF, b1 = c1 & 0xFF;
    int a = a0 + (a1 - a0) * i / n;
    int r = r0 + (r1 - r0) * i / n;
    int g = g0 + (g1 - g0) * i / n;
    int b = b0 + (b1 - b0) * i / n;
    return (uint32_t)((ui_clamp8(a) << 24) | (ui_clamp8(r) << 16) | (ui_clamp8(g) << 8) | ui_clamp8(b));
}

uint32_t ui_usage_color(uint32_t pct) {
    if (pct < 50)
        return 0xFF4CAF50; // low usage: green
    if (pct < 80)
        return 0xFFFFC107; // medium: amber
    return 0xFFF44336;     // high: red
}

void ui_draw_hgrad_rect(int x, int y, int w, int h, uint32_t top, uint32_t bottom) {
    if (h <= 0 || w <= 0)
        return;
    if (h == 1) {
        draw_rect(x, y, w, 1, top);
        return;
    }
    for (int i = 0; i < h; i++) {
        uint32_t c = ui_lerp_color(top, bottom, i, h - 1);
        draw_rect(x, y + i, w, 1, c);
    }
}

void ui_draw_bar_soft(int x, int y, int w, int h, uint32_t base) {
    if (h <= 0 || w <= 0)
        return;
    if (h >= 3) {
        uint32_t hl = ui_shade_color(base, +28);
        uint32_t sh = ui_shade_color(base, -28);
        uint32_t top = ui_shade_color(base, +12);
        uint32_t bot = ui_shade_color(base, -12);
        draw_rect(x, y, w, 1, hl);
        ui_draw_hgrad_rect(x, y + 1, w, h - 2, top, bot);
        draw_rect(x, y + h - 1, w, 1, sh);
    } else {
        draw_rect(x, y, w, h, base);
    }
}

void ui_fill_round_rect(int x, int y, int w, int h, int radius, uint32_t color) {
    if (w <= 0 || h <= 0)
        return;

    int r = radius < 0 ? 0 : radius;
    if (r * 2 > w)
        r = w / 2;
    if (r * 2 > h)
        r = h / 2;

    uint8_t *buf = fb.back ? fb.back : fb.front;
    if (!buf) // No framebuffer available
        return;

    // Core rectangles without the rounded corners.
    draw_rect(x + r, y, w - 2 * r, h, color);
    if (r == 0)
        return;
    draw_rect(x, y + r, r, h - 2 * r, color);
    draw_rect(x + w - r, y + r, r, h - 2 * r, color);

    // Corner fills using a simple circle mask.
    int r2 = r * r;
    for (int dy = 0; dy < r; ++dy) {
        for (int dx = 0; dx < r; ++dx) {
            if (dx * dx + dy * dy > r2)
                continue;
            put_pixel_raw(buf, x + r - 1 - dx, y + r - 1 - dy, color);         // top-left
            put_pixel_raw(buf, x + w - r + dx, y + r - 1 - dy, color);          // top-right
            put_pixel_raw(buf, x + r - 1 - dx, y + h - r + dy, color);          // bottom-left
            put_pixel_raw(buf, x + w - r + dx, y + h - r + dy, color);          // bottom-right
        }
    }
}
