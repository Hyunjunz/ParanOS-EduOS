#pragma once

#include <stdint.h>

// Simple window manager API for the kernel GUI.
// - Windows register themselves with title/icon and state flags.
// - The WM draws taskbar buttons for all open windows.
// - Taskbar clicks are routed back via a callback.

typedef struct wm_entry wm_entry_t;

typedef void (*wm_taskbar_click_fn)(wm_entry_t *win, void *user);

struct wm_entry
{
    const char *title;
    uint32_t icon_color;   // simple colored square as icon
    int *open_flag;        // non-zero => window exists
    int *minimized_flag;   // non-zero => window minimized (may be NULL)

    wm_taskbar_click_fn on_click;
    void *user;            // user data passed back on click
};

void wm_init(void);

// Register a window for taskbar listing.
// The flags are owned by the caller and must remain valid.
int wm_register_window(const char *title,
                       uint32_t icon_color,
                       int *open_flag,
                       int *minimized_flag,
                       wm_taskbar_click_fn on_click,
                       void *user);

// Draw taskbar entries on top of an existing taskbar bar.
// start_x: X position where window buttons begin (e.g., after "Start").
void wm_draw_taskbar(int y, int height, int start_x);

// Handle mouse click in taskbar area; calls registered callbacks as needed.
void wm_handle_taskbar_click(int mx, int my, int y, int height, int start_x);

// Focus helpers
int  wm_get_front(void);
void wm_set_front(int id);
void wm_cycle_next(void);
