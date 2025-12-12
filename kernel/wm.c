#include "wm.h"

#include "fb.h"
#include "ui.h"

#define WM_MAX_WINDOWS 16

static wm_entry_t g_entries[WM_MAX_WINDOWS];
static int g_entry_count = 0;
static int g_front_id = -1;

void wm_init(void)
{
    g_entry_count = 0;
    g_front_id = -1;
}

int wm_register_window(const char *title,
                       uint32_t icon_color,
                       int *open_flag,
                       int *minimized_flag,
                       wm_taskbar_click_fn on_click,
                       void *user)
{
    if (g_entry_count >= WM_MAX_WINDOWS)
        return -1;

    wm_entry_t *e = &g_entries[g_entry_count];
    e->title = title;
    e->icon_color = icon_color;
    e->open_flag = open_flag;
    e->minimized_flag = minimized_flag;
    e->on_click = on_click;
    e->user = user;

    int id = g_entry_count++;
    if (g_front_id == -1)
        g_front_id = id;
    return id;
}

void wm_draw_taskbar(int y, int height, int start_x)
{
    int x = start_x;
    int padding = 4;
    int icon_size = height - padding * 2;
    if (icon_size < 8)
        icon_size = 8;

    for (int i = 0; i < g_entry_count; ++i)
    {
        wm_entry_t *e = &g_entries[i];
        if (!e->open_flag || !(*e->open_flag))
            continue;

        int btn_w = 120;
        int btn_h = height - 4;
        int btn_x = x;
        int btn_y = y + (height - btn_h) / 2;

        uint32_t base = 0xFFEFF2F7;
        uint32_t highlight = 0xFFDFE6F2;
        uint32_t focus = 0xFF4C8DFF;
        uint32_t color = base;

        if (i == g_front_id)
            color = focus;
        else if (!(e->minimized_flag && *e->minimized_flag))
            color = highlight;

        ui_fill_round_rect(btn_x, btn_y, btn_w, btn_h, 10, color);

        // icon
        int icon_x = btn_x + padding;
        int icon_y = btn_y + (btn_h - icon_size) / 2;
        ui_fill_round_rect(icon_x, icon_y, icon_size, icon_size, 6, e->icon_color);

        // title
        int text_x = icon_x + icon_size + padding;
        int text_y = btn_y + (btn_h - 16) / 2;
        uint32_t text_color = (color == focus) ? 0xFFFFFFFF : 0xFF2F3645;
        draw_text(text_x, text_y, e->title, text_color, color);

        x += btn_w + 4;
    }
}

void wm_handle_taskbar_click(int mx, int my, int y, int height, int start_x)
{
    int x = start_x;
    int btn_h = height - 4;

    for (int i = 0; i < g_entry_count; ++i)
    {
        wm_entry_t *e = &g_entries[i];
        if (!e->open_flag || !(*e->open_flag))
            continue;

        int btn_w = 120;
        int btn_x = x;
        int btn_y = y + (height - btn_h) / 2;

        if (mx >= btn_x && mx < btn_x + btn_w &&
            my >= btn_y && my < btn_y + btn_h)
        {
            wm_set_front(i);
            if (e->on_click)
                e->on_click(e, e->user);
            return;
        }

        x += btn_w + 4;
    }
}

int wm_get_front(void)
{
    return g_front_id;
}

void wm_set_front(int id)
{
    if (id < 0 || id >= g_entry_count)
        return;
    g_front_id = id;
}

void wm_cycle_next(void)
{
    if (g_entry_count == 0)
        return;

    int start = (g_front_id >= 0 && g_front_id < g_entry_count) ? g_front_id : 0;
    for (int i = 1; i <= g_entry_count; ++i)
    {
        int idx = (start + i) % g_entry_count;
        wm_entry_t *e = &g_entries[idx];
        if (e->open_flag && *e->open_flag)
        {
            g_front_id = idx;
            return;
        }
    }
}
