// kernel/kernel.c — High-half kernel + VGA + PMM + IDT/PIC/PIT/Serial/Keyboard

#include <stdint.h>
#include <stddef.h>
#include <mm/vmm.h>
#include "gdt.h"
#include "tss.h"
#include "syscall.h"
#include "multiboot2.h"
#include "fb.h"
#include "psf.h"
#include "bootinfo.h"
#include "idt.h"
#include "pic.h"
#include "pit.h"
#include "serial.h"
#include "keyboard.h"
#include "task/task.h"
#include "utils/keyboard_map.h"
#include "mouse.h"
#include "wm.h"
#include "ata.h"
#include "fs_mbr.h"
#include "fs_fat32.h"
#include "desktop.h"
#include "string.h"
#include "stdlib.h"
#include "io.h"
#include "pmm.h"
#include "config.h"
#include "ui.h"
#include "drivers/ac97.h"
#include "wav.h"

// BIOS
#include "bios/rtc.h"

// Shared context menu types
typedef enum { CTX_NONE = 0, CTX_DESKTOP, CTX_NOTEPAD, CTX_FILEWIN } ctx_target_t;
typedef enum {
    CTX_ACT_NONE = 0,
    CTX_ACT_NEW_TXT,
    CTX_ACT_REFRESH_DESKTOP,
    CTX_ACT_WALLPAPER,
    CTX_ACT_NOTEPAD_NEW,
    CTX_ACT_NOTEPAD_OPEN,
    CTX_ACT_NOTEPAD_SAVE,
    CTX_ACT_NOTEPAD_SAVE_AS,
    CTX_ACT_NOTEPAD_CLOSE,
    CTX_ACT_FILE_REFRESH,
    CTX_ACT_FILE_OPEN,
    CTX_ACT_FILE_UP,
    CTX_ACT_WIN_MINIMIZE,
    CTX_ACT_WIN_TOGGLE_MAX
} ctx_action_t;

extern void *memcpy_exact(void *dst, const void *src, size_t n);
static int ensure_desktop_bg_cache(void);
void map_kernel_heap(uintptr_t start, uintptr_t end);
static void filewin_open(void);
static void taskmgr_open(void);
static void desktop_refresh_from_path(void);
static void ensure_user_dirs_on_disk(const char *uname);
static void build_user_desktop_path(const char *uname);
static void desktop_path_for_name(char *out, size_t outsz, const char *name83);
static void filewin_refresh_list(void);
static void filewin_toggle_maximize(void);
static void taskmgr_toggle_maximize(void);
static void notepad_toggle_maximize(void);
static void file_picker_start(const char *ext_filter,
                              void (*on_select)(const char *fullpath, const char *name83, void *user),
                              void *user);
static void file_picker_cancel(void);
static void file_picker_handle_selection(const char *fullpath, const char *name83);
static void notepad_file_pick_cb(const char *fullpath, const char *name83, void *user);
static void notepad_open_path(const char *fullpath);
static void cursor_load_from_disk(void);
static void taskbar_draw_shortcuts(int x, int y, int h, int *next_start_x);
static int taskbar_handle_shortcut_click(int mx, int my, int x, int y, int h);
static void terminal_render(void);
static void cursor_use_default(void);
static void cursor_use_resize(void);
static void imgview_open_path(const char *fullpath);
static void imgview_render(void);
static void imgview_close(void);
static void imgview_toggle_maximize(void);
static void imgview_taskbar_click(wm_entry_t *win, void *user);
static void wavplay_render(void);
static void filewin_delete_selection(void);
static void desktop_delete_selection(void);
static void launch_refresh_items(void);
static void launch_toggle(int show);
static void launch_activate(int idx);
static void boot_anim_render(void);
static void sound_play_wav_path(const char *path);
void *kmalloc(size_t sz);
void *ext_mem_alloc(size_t sz);

// Simple linear resampler for 16-bit interleaved PCM
static uint16_t *resample_linear_16(const uint16_t *src, uint32_t src_frames, uint8_t channels,
                                    uint32_t src_rate, uint32_t dst_rate, uint32_t *out_frames)
{
    if (!src || !out_frames || src_frames == 0 || src_rate == 0 || dst_rate == 0 || (channels != 1 && channels != 2))
        return NULL;

    uint64_t oframes = ((uint64_t)src_frames * dst_rate + (src_rate / 2)) / src_rate;
    if (oframes == 0)
        return NULL;
    uint64_t samples = oframes * channels;
    uint64_t bytes = samples * sizeof(uint16_t);
    serial_printf("[WAV] resample alloc: in_frames=%u out_frames=%llu ch=%u bytes=%llu\n",
                  src_frames, (unsigned long long)oframes, channels, (unsigned long long)bytes);
    uint16_t *dst = kmalloc((size_t)bytes);
    if (!dst)
    {
        serial_printf("[WAV] resample alloc failed (%llu bytes)\n", (unsigned long long)bytes);
        return NULL;
    }

    uint64_t step = ((uint64_t)src_rate << 32) / dst_rate;
    uint64_t pos = 0;
    for (uint64_t i = 0; i < oframes; ++i)
    {
        uint32_t idx = (uint32_t)(pos >> 32);
        if (idx >= src_frames)
            idx = src_frames - 1;
        uint32_t frac = (uint32_t)(pos & 0xFFFFFFFFu);
        for (uint32_t c = 0; c < channels; ++c)
        {
            uint32_t s0 = src[idx * channels + c];
            uint32_t s1 = (idx + 1 < src_frames) ? src[(idx + 1) * channels + c] : s0;
            uint32_t sample = (uint32_t)(((uint64_t)s0 * (0x100000000ull - frac) + (uint64_t)s1 * frac) >> 32);
            dst[i * channels + c] = (uint16_t)sample;
        }
        pos += step;
    }
    *out_frames = (uint32_t)oframes;
    return dst;
}
static void wavplay_open(void);
static void wavplay_render(void);
static void wavplay_close(void);
static void wavplay_taskbar_click(wm_entry_t *win, void *user);
static void wavplay_pick_cb(const char *fullpath, const char *name83, void *user);

// Unified accent palette
static const uint32_t COLOR_ACCENT = 0xFF4C8DFF;
static const uint32_t COLOR_TEXT   = 0xFFE7EEF9;
static const uint32_t COLOR_SURFACE = 0xFFF6F8FC;
static const uint32_t COLOR_BORDER = 0xFFD5DEEA;
static const uint32_t COLOR_TEXT_DARK = 0xFF2F3645;
static int g_shift_down = 0;
static int g_caps_on = 0;
static const char g_key_normal[128] = {
    0,27,'1','2','3','4','5','6','7','8','9','0','-','=', '\b',
    '\t','q','w','e','r','t','y','u','i','o','p','[',']','\n',0,
    'a','s','d','f','g','h','j','k','l',';','\'','`', 0,'\\',
    'z','x','c','v','b','n','m',',','.','/', 0, '*', 0,' ',
};
static const char g_key_shift[128] = {
    0,27,'!','@','#','$','%','^','&','*','(',')','_','+', '\b',
    '\t','Q','W','E','R','T','Y','U','I','O','P','{','}','\n',0,
    'A','S','D','F','G','H','J','K','L',':','\"','~', 0,'|',
    'Z','X','C','V','B','N','M','<','>','?', 0, '*', 0,' ',
};
static char key_to_char(uint8_t sc)
{
    if (sc >= 128)
        return 0;
    char c_norm = g_key_normal[sc];
    char c_shift = g_key_shift[sc];
    if (c_norm >= 'a' && c_norm <= 'z')
    {
        int upper = g_caps_on ^ g_shift_down;
        return upper ? (char)(c_norm - 32) : c_norm;
    }
    if (g_shift_down)
        return c_shift;
    return c_norm;
}

extern void limine_fill_bootinfo_from_fb(void);

#define U_ENTRY 0x00800000u
#define U_STACK_TOP 0x00C00000u

extern uint32_t g_mbinfo_phys;
extern uint32_t stack_top;
extern pagemap_t kernel_pagemap;
extern struct multiboot_tag_framebuffer *mb_fb;
extern uint32_t *pgdir;
//extern uint32_t page_directory[], page_table0[], page_table_hh[];
static uint64_t page_table0[1024] __attribute__((aligned(4096)));
extern uintptr_t phys_addr_of(uintptr_t va);
extern char __text_lma[], __kernel_high_start[], __kernel_high_end[];
extern uint8_t _kernel_stack_top;
#if 0
#define VGA_TEXT ((volatile uint16_t *)0xB8000)
#endif

#define VGA_COLS 80
#define VGA_ROWS 25

static uint8_t kbd_keydown[256] = {0};
static int kbd_e0_prefix = 0;

#ifndef KBD_SERIAL_DEBUG
#define KBD_SERIAL_DEBUG 0
#endif

static uint64_t kbd_next_log_tick = 0;
static inline void kbd_serial_log_char(char c)
{
#if KBD_SERIAL_DEBUG
    if (jiffies >= kbd_next_log_tick)
    {
        serial_printf("key: %c\n", c);
        kbd_next_log_tick = jiffies + 5;
    }
#else
    (void)c;
#endif
}

static void probe_back_tail(void)
{
    // Framebuffer가 준비되지 않았다면 건드리지 않는다.
    if (!fb.back || fb.width == 0 || fb.height == 0 || fb.pitch == 0)
        return;

    volatile uint8_t *p = (volatile uint8_t *)fb.back;
    size_t sz = (size_t)fb.height * fb.pitch;
    p[sz - 1] = p[sz - 1]; // 마지막 유효 바이트 R/W
}

static void gui_draw_clock_fb(void)
{
    rtc_time_t now;
    rtc_read_time(&now);
    char buf[9];
    rtc_format(buf, &now);

    int len = 8;
    int pad = 8;
    int wpx = psf_width() * len;
    int hpx = psf_height();
    int x0 = fb.width - wpx - pad;
    if (x0 < 0)
        x0 = 0;
    int y0 = 6;

    // Draw clock into back buffer; desktop_render() will flush.
    draw_rect(x0 - 4, y0 - 2, wpx + 8, hpx + 4, 0xFF2A2A2A);
    draw_text(x0, y0, buf, 0xFFFFFFFF, 0xFF2A2A2A);
}

extern uint32_t pmm_alloc_phys(void);
extern int vmm_map(uintptr_t v, uintptr_t p, uint32_t flags);

static int map_user_minimal(void)
{
    uint32_t pa_code = pmm_alloc_phys();
    uint32_t pa_stack = pmm_alloc_phys();
    if (!pa_code || !pa_stack)
    {
        serial_printf("[user] pmm_alloc_phys failed\n");
        return -1;
    }
    if (vmm_map(U_ENTRY, pa_code, VMM_P | VMM_RW | VMM_US))
        return -1;
    if (vmm_map(U_STACK_TOP - 0x1000, pa_stack, VMM_P | VMM_RW | VMM_US))
        return -1;

    return 0;
}

static const uint8_t user_stub[] = {0x90, 0xEB, 0xFE};

static void load_user_stub(void)
{
    memcpy((void *)U_ENTRY, user_stub, sizeof(user_stub));
}

#if 0
static inline void put_cell(size_t x, size_t y, char c, uint8_t color)
{
    VGA_TEXT[y * VGA_COLS + x] = ((uint16_t)color << 8) | (uint8_t)c;
}

static void clear(uint8_t color)
{
    for (size_t y = 0; y < VGA_ROWS; ++y)
        for (size_t x = 0; x < VGA_COLS; ++x)
            put_cell(x, y, ' ', color);
}

static void write_at(const char *s, uint8_t color, size_t x, size_t y)
{
    for (size_t i = 0; s[i] && x + i < VGA_COLS; ++i)
    {
        put_cell(x + i, y, s[i], color);
    }
}

static void write_center(const char *s, uint8_t color, size_t row)
{
    size_t len = 0;
    while (s[len])
        ++len;
    size_t start = (len >= VGA_COLS) ? 0 : (VGA_COLS - len) / 2;
    write_at(s, color, start, row);
}
#endif

static void init_fpu_sse(void)
{
    uint64_t cr0, cr4;

    // --- CR0 설정: EM=0, MP=1 ---
    __asm__ volatile ("mov %%cr0, %0" : "=r"(cr0));
    cr0 &= ~(1ULL << 2);   // EM = 0 (FPU/SSE 사용 허용)
    cr0 |=  (1ULL << 1);   // MP = 1 (FWAIT 관련)
    __asm__ volatile ("mov %0, %%cr0" :: "r"(cr0));

    // --- CR4 설정: OSFXSR=1, OSXMMEXCPT=1 ---
    __asm__ volatile ("mov %%cr4, %0" : "=r"(cr4));
    cr4 |= (1ULL << 9);    // OSFXSR: FXSAVE/FXRSTOR + SSE state 지원
    cr4 |= (1ULL << 10);   // OSXMMEXCPT: XMM 예외 지원
    __asm__ volatile ("mov %0, %%cr4" :: "r"(cr4));

    // --- FPU 초기화 ---
    __asm__ volatile ("fninit");
}

void identity_map_low(uint64_t limit)
{
    for (uint64_t pa = 0; pa < limit; pa += 0x1000) {
        // va == pa 로 매핑
        vmm_map((uintptr_t)pa,
                (uintptr_t)pa,
                PAGE_PRESENT | PAGE_RW);
    }
}

static inline void enable_io_iopl3(void)
{
    uint64_t rflags;

    // RFLAGS 읽기: pushfq 후 pop으로 레지스터에 꺼내기
    __asm__ volatile("pushfq; pop %0" : "=r"(rflags));

    // IOPL = 3 (비트 12~13)
    rflags |= (3ull << 12);

    // 수정된 RFLAGS 다시 쓰기: 일반 레지스터를 push, 그걸 popfq로 플래그에 적용
    __asm__ volatile("push %0; popfq" :: "r"(rflags) : "memory");
}

static inline void enable_io_full(void)
{
    uint64_t rflags;

    // 현재 RFLAGS 읽기
    __asm__ volatile("pushfq; pop %0" : "=r"(rflags));

    rflags |= (3ull << 12);

    // 수정된 RFLAGS 쓰기
    __asm__ volatile("push %0; popfq" :: "r"(rflags) : "memory");

    uint64_t new_rflags;
    __asm__ volatile("pushfq; pop %0" : "=r"(new_rflags));

    serial_printf("[rflags-after]=%016lx\n", new_rflags);
}

/*
static void jump_to_user(void)
{
    extern void user_mode_start(void);

    uint32_t user_attack = 0x800000;

    __asm__ volatile(
        "cli\n"
        "mov $0x23, %%ax\n"
        "mov %%ax, %%ds\n"
        "mov %%ax, %%es\n"
        "mov %%ax, %%fs\n"
        "mov %%ax, %%gs\n"
        "pushl $0x23\n"
        "pushl %[ustack]\n"
        "pushfl\n"
        "pushl $0x1B\n"
        "pushl %[entry]\n"
        "iret\n"
        :
        : [entry] "r"(user_mode_start), [ustack] "r"(user_attack)
        : "ax");
} */

static void jump_to_user_real(uintptr_t entry, uintptr_t user_stack_top)
{
    if (!vmm_alloc_page(U_ENTRY, VMM_P | VMM_RW | VMM_US)) {
        serial_printf("[user] map failed: entry\n");
        for (;;)
            __asm__ __volatile__("cli; hlt");
    }
    if (!vmm_alloc_page(U_STACK_TOP - 0x1000, VMM_P | VMM_RW | VMM_US)) {
        serial_printf("[user] map failed: stack\n");
        for (;;)
            __asm__ __volatile__("cli; hlt");
    }

    volatile uint8_t *u = (uint8_t *)U_ENTRY;
    u[0] = 0x90; /* NOP */
    u[1] = 0xEB; /* JMP rel8 */
    u[2] = 0xFE; /* -2 */

    vmm_reload_cr3();

    uintptr_t phys;
    uint32_t fl;
    int q = vmm_query(U_ENTRY, &phys, &fl);
    serial_printf("[user] entry=%08lx stack=%08lx q=%d phys=%08lx fl=%08x\n",
                  (unsigned long)U_ENTRY,
                  (unsigned long)U_STACK_TOP,
                  q,
                  (unsigned long)phys,
                  fl);

    if (q != 0 || (fl & (VMM_P | VMM_US)) != (VMM_P | VMM_US)) {
        serial_printf("[user] bad mapping (P/US missing)\n");
        for (;;)
            __asm__ __volatile__("cli; hlt");
    }

    __asm__ volatile(
        "cli\n"
        "mov $0x23, %%ax\n"       /* user data selector (DPL=3) */
        "mov %%ax, %%ds\n"
        "mov %%ax, %%es\n"
        "mov %%ax, %%fs\n"
        "mov %%ax, %%gs\n"
        "pushq $0x23\n"           /* SS = user data */
        "pushq %[ustack]\n"       /* RSP */
        "pushfq\n"                /* RFLAGS */
        "pushq $0x1B\n"           /* CS = user code */
        "pushq %[entry]\n"        /* RIP */
        "iretq\n"
        :
        : [entry]  "r" ((uint64_t)entry),
          [ustack] "r" ((uint64_t)user_stack_top)
        : "ax", "memory");
}


#define PAGE_SIZE 4096u
// Grow kernel heap to 32 MiB so audio resample buffers fit
#define KHEAP_PAGES 8192u

static inline uintptr_t align_up(uintptr_t v, uintptr_t a)
{
    return (v + (a - 1)) & ~(a - 1);
}

static uint8_t *kheap_begin, *kheap_end, *kheap_brk;

static void kheap_init(void)
{
    uintptr_t hb = align_up((uintptr_t)__kernel_high_end, PAGE_SIZE);
    uintptr_t he = hb + KHEAP_PAGES * PAGE_SIZE;

    // 힙 VA 범위 매핑
    map_kernel_heap(hb, he);

    kheap_begin = (uint8_t *)hb;
    kheap_end   = (uint8_t *)he;
    kheap_brk   = kheap_begin;

    serial_printf("[kheap] heap=%p..%p\n", (void*)hb, (void*)he);
}

void *kmalloc(size_t sz)
{
    if (!sz)
        return 0;
    uintptr_t p = align_up((uintptr_t)kheap_brk, 8);
    if (p + sz > (uintptr_t)kheap_end)
        return 0;
    kheap_brk = (uint8_t *)(p + sz);
    return (void *)p;
}

// 커널 PMM은 사용하지 않고, Limine의 ext_mem_alloc 기반 HHDM만 사용

#ifndef COM1
#define COM1 0x3F8
#endif
extern volatile uint64_t jiffies;

#define BOOTINFO_PHYS 0x0009F000u
typedef struct
{
    uint32_t e820_ptr;
    uint32_t e820_count;
    uint64_t fb_phys;
    uint32_t fb_w;
    uint32_t fb_h;
    uint32_t fb_pitch;
    uint32_t fb_bpp;
} bootinfo_t;

static volatile bootinfo_t *bi = (volatile bootinfo_t *)(uintptr_t)BOOTINFO_PHYS;

// Global FAT32 mount state (set during disk probe)
static fat32_vol_t g_vol;
static int g_vol_mounted = 0;

// Right-click info popup state
static int info_visible = 0;
static int info_x = 0, info_y = 0;
static int info_idx = -1;
static const int info_w = 160;
static const int info_h = 40;

// Context menu
// (type defs moved earlier)
typedef struct
{
    char label[32];
    ctx_action_t act;
} ctx_item_t;

static ctx_item_t g_ctx_items[8];
static int g_ctx_count = 0;
static int ctx_menu_visible = 0;
static int ctx_menu_x = 0, ctx_menu_y = 0, ctx_menu_w = 180, ctx_menu_h = 0;
static ctx_target_t ctx_menu_target = CTX_NONE;
static int ctx_menu_item_h = 24;

// Top menu (mac-like)
typedef struct { const char *title; int x, w; } topmenu_title_t;
static int g_topmenu_open = -1; // -1 none, 0 File, 1 Window
static int g_topmenu_hover = -1;
static int g_topmenu_item_hover = -1;
static const char *g_topmenu_file_labels[] = { "New", "Open...", "Save", "Save As...", "Close" };
static const ctx_action_t g_topmenu_file_actions[] = {
    CTX_ACT_NOTEPAD_NEW, CTX_ACT_NOTEPAD_OPEN, CTX_ACT_NOTEPAD_SAVE, CTX_ACT_NOTEPAD_SAVE_AS, CTX_ACT_NOTEPAD_CLOSE
};
static const char *g_topmenu_window_labels[] = { "Minimize", "Toggle Maximize" };
static const ctx_action_t g_topmenu_window_actions[] = { CTX_ACT_WIN_MINIMIZE, CTX_ACT_WIN_TOGGLE_MAX };
static topmenu_title_t g_topmenu_titles[2];
static const int TOPBAR_H = 32;
// Taskbar quick launch
typedef struct { const char *name; uint32_t color; int id; } quick_launch_t;
enum { QL_FILE = 0, QL_TASKMGR = 1, QL_TERMINAL = 2, QL_WAVPLAY = 3 };
static const quick_launch_t g_quick_launch[] = {
    { "Explorer", 0xFF4C8DFF, QL_FILE },
    { "SysMon",   0xFF5FCFA0, QL_TASKMGR },
    { "Terminal", 0xFFB689FF, QL_TERMINAL },
    { "WAV",      0xFF63C6E0, QL_WAVPLAY },
};
static const int QL_COUNT = 4;
static const int QL_BTN = 28;
static const int QL_PAD = 6;
// Cursor state
static uint32_t *g_cursor_default_img = NULL;
static int g_cursor_default_w = 0, g_cursor_default_h = 0;
static uint32_t g_cursor_fallback[9 * 9];
static uint32_t g_cursor_resize_img[9 * 9];
static const int g_cursor_resize_w = 9;
static const int g_cursor_resize_h = 9;
static int g_cursor_resize_active = 0;
static int g_alt_down = 0;
static int g_win_down = 0;
typedef struct
{
    int open;
    int minimized;
    int wx, wy, ww, wh;
    int dragging;
    int drag_offx, drag_offy;
    int can_minimize;
    int can_maximize;
    char path[128];
    char name[32];
} wavplay_t;
static wavplay_t g_wavplay = {0};
static int topmenu_active_window(void);
static void topmenu_get_menu(int menu_idx, const char ***labels, const ctx_action_t **actions, int *count);
static void topmenu_apply_action(ctx_action_t act);
static void topmenu_layout_titles(void);
static void topmenu_draw(void);
static int topmenu_handle_click(int mx, int my);

static int name_prompt_active = 0;
static int name_prompt_ready_click = 0;
static char name_prompt_buf[32];
static int name_prompt_len = 0;
typedef enum { PROMPT_NONE = 0, PROMPT_CREATE_TXT, PROMPT_OPEN_TXT, PROMPT_SAVE_AS } prompt_mode_t;
static prompt_mode_t name_prompt_mode = PROMPT_NONE;
static char name_prompt_title[48] = "Filename:";

// Forward declaration for overlay redraw from desktop_render
static void notepad_render(void);
static void notepad_taskbar_click(wm_entry_t *win, void *user);
static void filewin_render(void);
static void taskmgr_render(void);
static void display_render(void);
static void display_taskbar_click(wm_entry_t *win, void *user);

// Cached static desktop background to avoid re-drawing gradients/icons every frame.
static uint8_t *desktop_bg_cache = NULL;
static size_t desktop_bg_cache_bytes = 0;
static int desktop_bg_dirty = 1;

// Launch menu state
typedef enum { L_ACTION_LOGOUT = 1, L_ACTION_REBOOT, L_ACTION_SHUTDOWN, L_ACTION_APP } launch_type_t;
typedef struct
{
    char name[32];
    launch_type_t type;
    char path[96];
} launch_item_t;

static launch_item_t g_launch_items[24];
static int g_launch_count = 0;
static int g_launch_visible = 0;
static int g_launch_x = 10;
static int g_launch_y = 0;
static const int g_launch_item_h = 26;
static uint64_t g_launch_last_refresh = 0;

// Context menu helpers
static void ctx_menu_show(ctx_target_t target, int x, int y);
static void ctx_menu_hide(void);
static void ctx_menu_handle_action(ctx_action_t act);
static int point_in_rect(int mx, int my, int x, int y, int w, int h);

static void start_name_prompt(prompt_mode_t mode, const char *default_text, const char *title)
{
    name_prompt_mode = mode;
    name_prompt_active = 1;
    name_prompt_ready_click = 0;
    name_prompt_len = 0;
    const char *def = default_text ? default_text : "";
    while (def[name_prompt_len] && name_prompt_len < (int)sizeof(name_prompt_buf) - 1)
    {
        name_prompt_buf[name_prompt_len] = def[name_prompt_len];
        name_prompt_len++;
    }
    name_prompt_buf[name_prompt_len] = 0;
    if (title)
    {
        strncpy(name_prompt_title, title, sizeof(name_prompt_title) - 1);
        name_prompt_title[sizeof(name_prompt_title) - 1] = 0;
    }
    else
    {
        strcpy(name_prompt_title, "Filename:");
    }
    desktop_mark_dirty();
}

// WM window handles
static int g_win_notepad = -1;
static int g_win_file = -1;
static int g_win_taskmgr = -1;
static int g_win_display = -1;
static int g_win_terminal = -1;
static int g_win_imgview = -1;
static int g_win_wavplay = -1;
// File picker state (uses File Explorer UI)
typedef void (*file_picker_cb)(const char *fullpath, const char *name83, void *user);
static struct
{
    int active;
    char ext[6]; // 3-letter + null; uppercase
    file_picker_cb cb;
    void *cb_user;
} g_file_picker = {0};

static void desktop_render(void)
{
    desktop_draw_background();

    // Taskbar buttons are dynamic; draw them after the background restore.
    int taskbar_h = 34;
    int start_x = 80;
    taskbar_draw_shortcuts(start_x, fb.height - taskbar_h, taskbar_h, &start_x);
    wm_draw_taskbar(fb.height - taskbar_h, taskbar_h, start_x);

    int front = wm_get_front();

    // 5) Windows: draw non-front first, then front-most
    if (front != g_win_notepad)
        notepad_render();
    if (front != g_win_file)
        filewin_render();
    if (front != g_win_taskmgr)
        taskmgr_render();
    if (front != g_win_display)
        display_render();
    if (front != g_win_terminal)
        terminal_render();
    if (front != g_win_imgview)
        imgview_render();
    if (front != g_win_wavplay)
        wavplay_render();

    if (front == g_win_notepad)
        notepad_render();
    else if (front == g_win_file)
        filewin_render();
    else if (front == g_win_taskmgr)
        taskmgr_render();
    else if (front == g_win_display)
        display_render();
    else if (front == g_win_terminal)
        terminal_render();
    else if (front == g_win_imgview)
        imgview_render();
    else if (front == g_win_wavplay)
        wavplay_render();

    // Top menu (between branding and clock)
    topmenu_draw();

    // 6) Info popup (on top of desktop / windows)
    if (info_visible)
    {
        const desktop_item_t *it = desktop_get_item(info_idx);
        if (it)
        {
            char line1[32];
            char line2[32];
            sprintf(line1, "%s", it->name);
            sprintf(line2, "%s", (it->attr & 0x10) ? "<DIR>" : "FILE");
            draw_rect(info_x, info_y, info_w, info_h, COLOR_BORDER);
            draw_rect(info_x + 1, info_y + 1, info_w - 2, info_h - 2, COLOR_SURFACE);
            draw_text(info_x + 6, info_y + 6, line1, COLOR_TEXT_DARK, COLOR_SURFACE);
            draw_text(info_x + 6, info_y + 20, line2, COLOR_TEXT_DARK, COLOR_SURFACE);
        }
    }

    // Context menu
    if (ctx_menu_visible && g_ctx_count > 0)
    {
        int x = ctx_menu_x, y = ctx_menu_y;
        if (x + ctx_menu_w > (int)fb.width)  x = fb.width - ctx_menu_w;
        if (y + ctx_menu_h > (int)fb.height) y = fb.height - ctx_menu_h;
        draw_rect(x, y, ctx_menu_w, ctx_menu_h, COLOR_BORDER);
        draw_rect(x + 1, y + 1, ctx_menu_w - 2, ctx_menu_h - 2, COLOR_SURFACE);
        int mx = mouse_get_x();
        int my = mouse_get_y();
        for (int i = 0; i < g_ctx_count; ++i)
        {
            int iy = y + 4 + i * ctx_menu_item_h;
            uint32_t bg = COLOR_SURFACE;
            if (mx >= x && mx < x + ctx_menu_w && my >= iy && my < iy + ctx_menu_item_h)
                bg = 0xFFE8F0FB;
            draw_rect(x + 3, iy, ctx_menu_w - 6, ctx_menu_item_h - 4, bg);
            draw_text(x + 10, iy + 4, g_ctx_items[i].label, COLOR_TEXT_DARK, bg);
        }
    }

    // Name prompt overlay
    if (name_prompt_active)
    {
        int w = 260, h = 70;
        int x = (fb.width > (uint32_t)w) ? (int)(fb.width - w) / 2 : 0;
        int y = (fb.height > (uint32_t)h) ? (int)(fb.height - h) / 2 : 0;
        draw_rect(x, y, w, h, COLOR_BORDER);
        draw_rect(x + 1, y + 1, w - 2, h - 2, COLOR_SURFACE);
        draw_text(x + 10, y + 8, name_prompt_title, COLOR_TEXT_DARK, COLOR_SURFACE);
        draw_rect(x + 10, y + 28, w - 20, 28, COLOR_SURFACE);
        draw_rect(x + 10, y + 28, w - 20, 1, COLOR_BORDER);
        draw_rect(x + 10, y + 28 + 27, w - 20, 1, COLOR_BORDER);
        draw_text(x + 16, y + 34, name_prompt_buf, COLOR_TEXT_DARK, COLOR_SURFACE);
        draw_text(x + 10, y + 60, "Enter to create, click anywhere to confirm", COLOR_TEXT_DARK, COLOR_SURFACE);
    }

    // Launch menu overlay
    if (g_launch_visible && g_launch_count > 0)
    {
        int w = 220;
        int h = g_launch_item_h * g_launch_count + 12;
        int x = g_launch_x;
        int y = g_launch_y;
        draw_rect(x, y, w, h, COLOR_BORDER);
        draw_rect(x + 1, y + 1, w - 2, h - 2, COLOR_SURFACE);
        int mx = mouse_get_x();
        int my = mouse_get_y();
        for (int i = 0; i < g_launch_count; ++i)
        {
            int iy = y + 6 + i * g_launch_item_h;
            uint32_t bg = COLOR_SURFACE;
            if (mx >= x && mx < x + w && my >= iy && my < iy + g_launch_item_h)
                bg = 0xFFE8F0FB;
            draw_rect(x + 4, iy, w - 8, g_launch_item_h - 2, bg);
            draw_text(x + 12, iy + 6, g_launch_items[i].name, COLOR_TEXT_DARK, bg);
        }
    }

    // 7) Clock (draw into back buffer)
    gui_draw_clock_fb();

    // 8) Mouse cursor (draw into back buffer before presenting)
    fb_draw_cursor(mouse_get_x(), mouse_get_y());

    // 9) Present back buffer to front
    fb_flush();
}

static int str_ieq_ext(const char *name, const char *ext)
{
    // return 1 if name ends with .EXT (case-insensitive)
    int ln = 0;
    while (name[ln])
        ln++;
    int le = 0;
    while (ext[le])
        le++;
    if (le == 0)
        return 0;
    int i = ln - 1, j = le - 1;
    // find dot
    while (i >= 0 && name[i] != '.')
        i--;
    if (i < 0)
        return 0;
    // compare after dot
    i++;
    if (ln - i != le)
        return 0;
    for (j = 0; j < le; ++j, ++i)
    {
        char a = name[i];
        char b = ext[j];
        if (a >= 'a' && a <= 'z')
            a -= 32;
        if (b >= 'a' && b <= 'z')
            b -= 32;
        if (a != b)
            return 0;
    }
    return 1;
}

static int find_first_wallpaper_name(char *out, size_t out_sz)
{
    if (!out || out_sz == 0)
        return -1;

    fat32_dirent_t entries[128];
    int n = 0;
    if (fat32_list_root_array(&g_vol, ata_read28, entries, 128, &n) != 0)
        return -1;

    for (int i = 0; i < n; ++i)
    {
        if (entries[i].attr & 0x10) // directory bit
            continue;
        if (!str_ieq_ext(entries[i].name83, "BMP"))
            continue;

        int j = 0;
        for (; j + 1 < (int)out_sz && entries[i].name83[j]; ++j)
            out[j] = entries[i].name83[j];
        out[j] = 0;
        return 0;
    }

    return -1;
}

// --- User accounts / login ---
#define MAX_USERS 8
typedef struct
{
    char name[16];
    char pass[32];
} user_entry_t;

static const char *g_path_base = "PARANOS";
static const char *g_path_users_dir = "PARANOS/USERS";
static const char *g_path_wall_dir = "PARANOS/WALLPAPR";
static const char *g_path_wallpaper = "PARANOS/WALLPAPR/WALLPAPR.BMP";
static const char *g_user_file = "PARANOS/USERS/USERS.TXT";
static user_entry_t g_users[MAX_USERS];
static int g_user_count = 0;
static char g_logged_in_user[16] = "ADMIN";
static char g_desktop_dir[64] = "PARANOS/USERS/ADMIN/DESKTOP";
static char g_prog_dir[64] = "PARANOS/USERS/ADMIN/PROGRAMS";
static int g_fb_ready = 0;
static int g_session_started = 0;
static int g_boot_anim = 1;
static uint64_t g_boot_anim_start = 0;

static int g_login_active = 1;
static int g_login_dirty = 1;
static int g_login_selected = 0;
static int g_login_hover = -1;
static int g_login_pw_focus = 1;
static char g_login_pw[32];
static int g_login_pw_len = 0;
static char g_login_message[64] = "Select a user and log in.";

typedef struct
{
    int px, py, pw, ph;
    int card_x, card_w, card_h, card_gap, list_y;
    int pw_x, pw_y, pw_w, pw_h;
    int btn_x, btn_y, btn_w, btn_h;
} login_layout_t;

static void upper_copy(char *dst, size_t dst_sz, const char *src)
{
    size_t i = 0;
    for (; src && src[i] && i + 1 < dst_sz; ++i)
    {
        char c = src[i];
        if (c >= 'a' && c <= 'z')
            c -= 32;
        dst[i] = c;
    }
    dst[i] = 0;
}

static void path_join(char *dst, size_t dst_sz, const char *dir, const char *name)
{
    if (!dst || dst_sz == 0)
        return;
    size_t j = 0;
    if (dir)
    {
        for (size_t i = 0; dir[i] && j + 1 < dst_sz; ++i)
            dst[j++] = dir[i];
    }
    if (j > 0 && dst[j - 1] != '/' && j + 1 < dst_sz)
        dst[j++] = '/';
    if (name)
    {
        for (size_t i = 0; name[i] && j + 1 < dst_sz; ++i)
            dst[j++] = name[i];
    }
    dst[j] = 0;
}

static void login_mark_dirty(void)
{
    g_login_dirty = 1;
}

static void boot_anim_render(void)
{
    if (!g_fb_ready)
        return;
    ui_draw_hgrad_rect(0, 0, fb.width, fb.height, 0xFF0A1018, 0xFF05080D);
    int cx = (int)fb.width / 2;
    int cy = (int)fb.height / 2;
    int r_inner = 40;
    static const int orbit[16][2] = {
        {60,0},{45,45},{0,60},{-45,45},{-60,0},{-45,-45},{0,-60},{45,-45},
        {60,0},{45,45},{0,60},{-45,45},{-60,0},{-45,-45},{0,-60},{45,-45}
    };
    uint64_t t = jiffies - g_boot_anim_start;
    int phase = (int)(t % 16);
    for (int i = 0; i < 8; ++i)
    {
        int idx = (phase + i * 2) % 16;
        int bx = cx + orbit[idx][0];
        int by = cy + orbit[idx][1];
        draw_rect(bx - 6, by - 6, 12, 12, 0xFF2F6FAB);
    }
    draw_rect(cx - r_inner, cy - 2, r_inner * 2, 4, 0xFF5E8C31);
    draw_text(cx - 40, cy + 30, "Loading...", 0xFFB0BECF, 0xFF0A1018);
    fb_draw_cursor(mouse_get_x(), mouse_get_y());
    fb_flush();
}

static void user_reset_list(void)
{
    memset(g_users, 0, sizeof(g_users));
    g_user_count = 0;
    g_login_selected = 0;
    g_login_hover = -1;
}

static void user_add(const char *name, const char *pw)
{
    if (!name || !pw || g_user_count >= MAX_USERS)
        return;
    user_entry_t *u = &g_users[g_user_count++];
    strncpy(u->name, name, sizeof(u->name) - 1);
    u->name[sizeof(u->name) - 1] = 0;
    strncpy(u->pass, pw, sizeof(u->pass) - 1);
    u->pass[sizeof(u->pass) - 1] = 0;
}

static void user_load_default(void)
{
    user_reset_list();
    user_add("admin", "1234");
    user_add("guest", "");
}

static void user_parse_lines(const char *buf)
{
    if (!buf)
        return;
    const char *p = buf;
    char line[64];
    while (*p)
    {
        int li = 0;
        while (*p && *p != '\n' && li < (int)sizeof(line) - 1)
            line[li++] = *p++;
        if (*p == '\n')
            p++;
        line[li] = 0;
        if (li == 0)
            continue;
        // split "name:pass"
        char *colon = NULL;
        for (int i = 0; line[i]; ++i)
        {
            if (line[i] == ':')
            {
                colon = &line[i];
                break;
            }
        }
        if (!colon)
            continue;
        *colon = 0;
        const char *nm = line;
        const char *pw = colon + 1;
        // trim spaces from name end
        int len_nm = (int)strlen(nm);
        while (len_nm > 0 && (nm[len_nm - 1] == ' ' || nm[len_nm - 1] == '\r'))
            ((char *)nm)[--len_nm] = 0;
        // skip empty names
        if (nm[0] == 0)
            continue;
        user_add(nm, pw);
    }
}

static void login_reset_password(void)
{
    memset(g_login_pw, 0, sizeof(g_login_pw));
    g_login_pw_len = 0;
}

static void ensure_user_store(void)
{
    user_reset_list();
    login_reset_password();
    if (!g_vol_mounted)
    {
        user_load_default();
        return;
    }

    ensure_user_dirs_on_disk("ADMIN");
    ensure_user_dirs_on_disk("GUEST");
    fat32_ensure_dir_path(&g_vol, ata_read28, ata_write28, g_path_wall_dir);

    char buf[512];
    uint32_t nread = 0;
    int r = fat32_read_file_path(&g_vol, ata_read28, g_user_file, buf, sizeof(buf) - 1, &nread);
    if (r == 0)
    {
        buf[(nread < sizeof(buf) - 1) ? nread : (sizeof(buf) - 1)] = 0;
        user_parse_lines(buf);
    }
    else
    {
        const char def_body[] = "admin:1234\nguest:\n";
        fat32_write_file_path(&g_vol, ata_read28, ata_write28, g_user_file, def_body, (uint32_t)(sizeof(def_body) - 1));
        user_parse_lines(def_body);
    }

    if (g_user_count == 0)
        user_load_default();
    build_user_desktop_path(g_logged_in_user);
    g_login_active = 1;
    g_login_dirty = 1;
    g_login_pw_focus = 1;
    strncpy(g_login_message, "Select a user and log in.", sizeof(g_login_message) - 1);
    g_login_message[sizeof(g_login_message) - 1] = 0;
}

static int user_verify(const char *name, const char *pw)
{
    if (!name || !pw)
        return 0;
    for (int i = 0; i < g_user_count; ++i)
    {
        if (strcmp(g_users[i].name, name) == 0)
        {
            if (strcmp(g_users[i].pass, pw) == 0)
                return 1;
            return 0;
        }
    }
    return 0;
}

static void login_layout_compute(login_layout_t *lo)
{
    if (!lo)
        return;
    lo->pw = (fb.width > 420) ? 420 : (int)fb.width - 20;
    if (lo->pw < 260)
        lo->pw = 260;
    lo->ph = 220;
    lo->px = (int)(fb.width - lo->pw) / 2;
    lo->py = (int)(fb.height - lo->ph) / 2;

    lo->card_h = 32;
    lo->card_w = lo->pw - 24;
    lo->card_x = lo->px + 12;
    lo->card_gap = 8;
    lo->list_y = lo->py + 36;

    lo->pw_x = lo->card_x;
    lo->pw_w = lo->card_w - 110;
    lo->pw_h = 28;
    lo->pw_y = lo->py + lo->ph - 72;

    lo->btn_w = 90;
    lo->btn_h = 28;
    lo->btn_x = lo->card_x + lo->card_w - lo->btn_w;
    lo->btn_y = lo->pw_y;
}

static void login_render(void)
{
    login_layout_t lo;
    login_layout_compute(&lo);

    ui_draw_hgrad_rect(0, 0, fb.width, fb.height, 0xFF0E141D, 0xFF060A10);
    // soft glow behind panel
    ui_draw_hgrad_rect(lo.px - 12, lo.py - 12, lo.pw + 24, lo.ph + 24, 0x221F6CAB, 0x001F6CAB);

    draw_rect(lo.px, lo.py, lo.pw, lo.ph, 0xFF10141A);
    draw_rect(lo.px, lo.py, lo.pw, 2, 0xFF35507A);
    draw_text(lo.px + 12, lo.py + 10, "Welcome", 0xFFFFFFFF, 0xFF10141A);

    for (int i = 0; i < g_user_count; ++i)
    {
        int cy = lo.list_y + i * (lo.card_h + lo.card_gap);
        uint32_t base = (i == g_login_selected) ? 0xFF2F6FAB : 0xFF1F2329;
        if (i == g_login_hover && i != g_login_selected)
            base = 0xFF26303A;
        draw_rect(lo.card_x, cy, lo.card_w, lo.card_h, base);
        draw_text(lo.card_x + 10, cy + 10, g_users[i].name, 0xFFFFFFFF, base);
    }

    draw_text(lo.card_x, lo.pw_y - 16, "Password", 0xFFB0B6BF, 0xFF111417);
    uint32_t pw_bg = g_login_pw_focus ? 0xFF1F6FB0 : 0xFF1A1E24;
    draw_rect(lo.pw_x, lo.pw_y, lo.pw_w, lo.pw_h, pw_bg);
    char masked[sizeof(g_login_pw)];
    int mi = 0;
    for (; mi < g_login_pw_len && mi < (int)sizeof(masked) - 1; ++mi)
        masked[mi] = '*';
    masked[mi] = 0;
    draw_text(lo.pw_x + 8, lo.pw_y + 8, masked, 0xFFFFFFFF, pw_bg);

    draw_rect(lo.btn_x, lo.btn_y, lo.btn_w, lo.btn_h, 0xFF3B9D4A);
    draw_text(lo.btn_x + 16, lo.btn_y + 8, "Login", 0xFFFFFFFF, 0xFF3B9D4A);

    draw_text(lo.card_x, lo.py + lo.ph - 26, g_login_message, 0xFFB0B6BF, 0xFF111417);

    fb_draw_cursor(mouse_get_x(), mouse_get_y());
    fb_flush();
    g_login_dirty = 0;
}

static void build_user_desktop_path(const char *uname)
{
    char upper[16];
    upper_copy(upper, sizeof(upper), uname ? uname : "ADMIN");
    sprintf(g_desktop_dir, "%s/USERS/%s/DESKTOP", g_path_base, upper);
    sprintf(g_prog_dir, "%s/USERS/%s/PROGRAMS", g_path_base, upper);
}

static void ensure_user_dirs_on_disk(const char *uname)
{
    if (!g_vol_mounted)
        return;
    fat32_ensure_dir_path(&g_vol, ata_read28, ata_write28, g_path_base);
    fat32_ensure_dir_path(&g_vol, ata_read28, ata_write28, g_path_users_dir);
    fat32_ensure_dir_path(&g_vol, ata_read28, ata_write28, g_path_wall_dir);
    char user_dir[64];
    char upper[16];
    upper_copy(upper, sizeof(upper), uname ? uname : "ADMIN");
    sprintf(user_dir, "%s/USERS/%s", g_path_base, upper);
    fat32_ensure_dir_path(&g_vol, ata_read28, ata_write28, user_dir);
    sprintf(user_dir, "%s/USERS/%s/DESKTOP", g_path_base, upper);
    fat32_ensure_dir_path(&g_vol, ata_read28, ata_write28, user_dir);
    sprintf(user_dir, "%s/USERS/%s/PROGRAMS", g_path_base, upper);
    fat32_ensure_dir_path(&g_vol, ata_read28, ata_write28, user_dir);
}

static void desktop_refresh_from_path(void)
{
    if (!g_vol_mounted)
        return;
    ensure_user_dirs_on_disk(g_logged_in_user);
    fat32_dirent_t tmp[128];
    int n = 0;
    if (fat32_list_dir_path(&g_vol, ata_read28, g_desktop_dir, tmp, 128, &n) == 0)
    {
        desktop_item_t items[DESKTOP_MAX_ITEMS];
        int count = 0;
        for (int i = 0; i < n && count < DESKTOP_MAX_ITEMS; ++i)
        {
            // Skip zero-length files as "deleted" entries (we zero files on delete)
            if (!(tmp[i].attr & 0x10) && tmp[i].size == 0)
                continue;
            int j = 0;
            for (; j < (int)sizeof(items[count].name) - 1 && tmp[i].name83[j]; ++j)
                items[count].name[j] = tmp[i].name83[j];
            items[count].name[j] = 0;
            items[count].attr = tmp[i].attr;
            items[count].size = tmp[i].size;
            count++;
        }
        desktop_set_items(items, count);
        desktop_mark_dirty();
        desktop_render();
    }
    else
    {
        desktop_set_items(NULL, 0);
        desktop_mark_dirty();
    }
}

static void desktop_delete_selection(void)
{
    int sel = desktop_get_selection();
    if (sel < 0)
        return;
    const desktop_item_t *it = desktop_get_item(sel);
    if (!it || (it->attr & 0x10))
        return;
    char full[96];
    desktop_path_for_name(full, sizeof(full), it->name);
    int r = fat32_write_file_path(&g_vol, ata_read28, ata_write28, full, "", 0);
    if (r == 0)
    {
        serial_printf("[DESKTOP] deleted (zeroed) %s\n", full);
        desktop_refresh_from_path();
    }
    else
    {
        serial_printf("[DESKTOP] delete failed (%d) %s\n", r, full);
    }
}

static void sound_play_wav_path(const char *path)
{
    if (!path || !ac97_is_ready() || !g_vol_mounted)
        return;
    uint32_t max_sz = 2 * 1024 * 1024;
    uint8_t *buf = kmalloc(max_sz);
    if (!buf)
        return;
    uint32_t read = 0;
    if (fat32_read_file_path(&g_vol, ata_read28, path, buf, max_sz, &read) != 0 || read < 44)
        return;
    wav_info_t info;
    if (wav_parse(buf, read, &info) != 0 || info.bits != 16 || info.channels == 0)
        return;
    uint32_t frames = info.data_bytes / (info.channels * 2);
    uint32_t rate = info.rate ? info.rate : 48000;
    const uint16_t *pcm = (const uint16_t *)info.data;

    // Resample to 48 kHz to avoid host/backend quirks with uncommon rates.
    if (rate != 48000)
    {
        uint32_t rframes = 0;
        uint16_t *rbuf = resample_linear_16(pcm, frames, (uint8_t)info.channels, rate, 48000, &rframes);
        if (rbuf && rframes > 0)
        {
            serial_printf("[WAV] resample %u->48000 Hz: frames %u->%u\n", rate, frames, rframes);
            pcm = rbuf;
            frames = rframes;
            rate = 48000;
        }
        else
        {
            serial_printf("[WAV] resample failed (%u Hz), falling back to original\n", rate);
        }
    }

    serial_printf("[WAV] play %s: rate=%u ch=%u frames=%u bytes=%u\n",
                  path, rate, info.channels, frames, info.data_bytes);
    ac97_play_pcm(pcm, frames, rate, (uint8_t)info.channels);
}

static void desktop_move_selection(int delta)
{
    int count = desktop_item_count();
    if (count <= 0)
        return;
    int sel = desktop_get_selection();
    if (sel < 0)
        sel = 0;
    sel += delta;
    if (sel < 0)
        sel = 0;
    if (sel >= count)
        sel = count - 1;
    desktop_set_selection(sel);
    desktop_mark_dirty();
}

static void start_desktop_session(void)
{
    if (g_session_started)
        return;
    if (!g_fb_ready)
        return;
    g_session_started = 1;

    filewin_open();
    taskmgr_open();
    desktop_mark_dirty();
    desktop_render();
}

static void login_attempt(void)
{
    if (g_user_count == 0)
        user_load_default();
    if (g_login_selected >= g_user_count)
        g_login_selected = 0;
    const char *uname = g_users[g_login_selected].name;
    if (user_verify(uname, g_login_pw))
    {
        strncpy(g_logged_in_user, uname, sizeof(g_logged_in_user) - 1);
        g_logged_in_user[sizeof(g_logged_in_user) - 1] = 0;
        sprintf(g_login_message, "Welcome, %s", uname);
        ensure_user_dirs_on_disk(uname);
        build_user_desktop_path(uname);
        desktop_refresh_from_path();
        g_login_active = 0;
        g_login_dirty = 0;
        start_desktop_session();
        return;
    }

    sprintf(g_login_message, "Password does not match for %s", uname);
    login_reset_password();
    login_mark_dirty();
}

// Simple Notepad app state
typedef struct
{
    int open;
    char name[13];
    char buf[4096];
    char path[96];
    int len;
    int cursor;
    int wx, wy, ww, wh;
    int dragging;
    int drag_offx, drag_offy;
    int last_drawn;
    int last_x, last_y, last_w, last_h;
    int minimized;
    int maximized;
    int prev_x, prev_y, prev_w, prev_h;
    int can_minimize;
    int can_maximize;
} notepad_t;

static notepad_t g_notepad = {0};

static const int NOTEPAD_MENU_H = 18;
static const int NOTEPAD_MENU_BTN_W = 60;
static const int NOTEPAD_MENU_BTN_H = 14;
static const int NOTEPAD_MENU_BTN_PAD = 6;

static void path_dirname(char *out, size_t outsz, const char *fullpath)
{
    if (!out || outsz == 0)
        return;
    size_t len = 0;
    while (fullpath && fullpath[len])
        len++;
    if (len == 0)
    {
        out[0] = 0;
        return;
    }
    int i = (int)len - 1;
    while (i >= 0 && fullpath[i] != '/')
        i--;
    if (i <= 0)
    {
        out[0] = 0;
        return;
    }
    size_t copy = (size_t)i;
    if (copy >= outsz)
        copy = outsz - 1;
    for (size_t j = 0; j < copy; ++j)
        out[j] = fullpath[j];
    out[copy] = 0;
}

static int notepad_menu_hit(int mx, int my)
{
    if (!g_notepad.open || g_notepad.minimized)
        return -1;

    int menu_y = g_notepad.wy;
    if (my < menu_y || my >= menu_y + NOTEPAD_MENU_H)
        return -1;

    int btn_y = menu_y + (NOTEPAD_MENU_H - NOTEPAD_MENU_BTN_H) / 2;
    int btn_x0 = g_notepad.wx + NOTEPAD_MENU_BTN_PAD;

    for (int i = 0; i < 3; ++i)
    {
        int bx = btn_x0 + i * (NOTEPAD_MENU_BTN_W + NOTEPAD_MENU_BTN_PAD);
        if (mx >= bx && mx < bx + NOTEPAD_MENU_BTN_W &&
            my >= btn_y && my < btn_y + NOTEPAD_MENU_BTN_H)
        {
            return i; // 0=New,1=Open,2=Save
        }
    }
    return -1;
}

static void notepad_toggle_maximize(void)
{
    if (!g_notepad.open || !g_notepad.can_maximize)
        return;

    if (!g_notepad.maximized)
    {
        g_notepad.prev_x = g_notepad.wx;
        g_notepad.prev_y = g_notepad.wy;
        g_notepad.prev_w = g_notepad.ww;
        g_notepad.prev_h = g_notepad.wh;
        int top = 28;
        int bottom = 32;
        g_notepad.wx = 6;
        g_notepad.wy = top + 4;
        g_notepad.ww = fb.width - 12;
        g_notepad.wh = fb.height - top - bottom - 8;
        g_notepad.maximized = 1;
    }
    else
    {
        g_notepad.wx = g_notepad.prev_x;
        g_notepad.wy = g_notepad.prev_y;
        g_notepad.ww = g_notepad.prev_w;
        g_notepad.wh = g_notepad.prev_h;
        g_notepad.maximized = 0;
    }
    desktop_mark_dirty();
}

static void notepad_set_path_and_name(const char *fullpath)
{
    if (!fullpath)
        return;
    strncpy(g_notepad.path, fullpath, sizeof(g_notepad.path) - 1);
    g_notepad.path[sizeof(g_notepad.path) - 1] = 0;

    // Derive name from path for window title
    const char *base = fullpath;
    for (const char *p = fullpath; *p; ++p)
    {
        if (*p == '/')
            base = p + 1;
    }
    size_t i = 0;
    for (; base[i] && i + 1 < sizeof(g_notepad.name); ++i)
        g_notepad.name[i] = base[i];
    g_notepad.name[i] = 0;
}

static void notepad_file_pick_cb(const char *fullpath, const char *name83, void *user)
{
    (void)user;
    (void)name83;
    notepad_open_path(fullpath);
    wm_set_front(g_win_notepad);
}

// Load cursor image from disk (BMP 32bpp) at /ParanOS/System64/Cursor/Cursor.bmp
static void cursor_load_from_disk(void)
{
    if (!g_vol_mounted)
        return;

    const char *path = "PARANOS/SYSTEM64/CURSOR/CURSOR.BMP";
    uint8_t hdr[128];
    uint32_t n = 0;
    if (fat32_read_file_path(&g_vol, ata_read28, path, hdr, sizeof(hdr), &n) != 0 || n < 54)
        return;

    if (hdr[0] != 'B' || hdr[1] != 'M')
        return;

    uint32_t file_size = *(uint32_t *)&hdr[2];
    uint32_t data_off = *(uint32_t *)&hdr[10];
    int32_t w = *(int32_t *)&hdr[18];
    int32_t h_raw = *(int32_t *)&hdr[22];
    uint16_t planes = *(uint16_t *)&hdr[26];
    uint16_t bpp = *(uint16_t *)&hdr[28];
    uint32_t comp = *(uint32_t *)&hdr[30];

    if (file_size == 0 || file_size > 4 * 1024 * 1024)
        return;
    if (planes != 1 || bpp != 32 || comp != 0 || w <= 0 || h_raw == 0)
        return;

    uint8_t *file_buf = kmalloc(file_size);
    if (!file_buf)
        return;
    uint32_t full = 0;
    if (fat32_read_file_path(&g_vol, ata_read28, path, file_buf, file_size, &full) != 0 || full < file_size)
        return;

    if (data_off >= file_size)
        return;
    int top_down = (h_raw < 0);
    uint32_t h = (h_raw < 0) ? (uint32_t)(-h_raw) : (uint32_t)h_raw;
    uint32_t *img = (uint32_t *)kmalloc((size_t)w * (size_t)h * sizeof(uint32_t));
    if (!img)
        return;

    const uint8_t *src = file_buf + data_off;
    uint32_t row_bytes = (uint32_t)w * 4;
    for (uint32_t y = 0; y < h; ++y)
    {
        uint32_t sy = top_down ? y : (h - 1 - y);
        if ((size_t)sy * row_bytes + row_bytes > file_size - data_off)
            break;
        const uint8_t *row = src + (size_t)sy * row_bytes;
        for (uint32_t x = 0; x < (uint32_t)w; ++x)
        {
            uint8_t b = row[x * 4 + 0];
            uint8_t g = row[x * 4 + 1];
            uint8_t r = row[x * 4 + 2];
            uint8_t a = row[x * 4 + 3];
            img[y * (uint32_t)w + x] = ((uint32_t)a << 24) | ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
        }
    }

    // Downscale to a sane cursor size if needed
    const int CURSOR_MAX_DIM = 32;
    int target_w = (int)w;
    int target_h = (int)h;
    if (target_w > CURSOR_MAX_DIM || target_h > CURSOR_MAX_DIM)
    {
        if (target_w >= target_h)
        {
            target_w = CURSOR_MAX_DIM;
            target_h = (int)(((int64_t)h * target_w) / (int)w);
        }
        else
        {
            target_h = CURSOR_MAX_DIM;
            target_w = (int)(((int64_t)w * target_h) / (int)h);
        }
        if (target_w < 8) target_w = 8;
        if (target_h < 8) target_h = 8;
    }

    uint32_t *scaled = img;
    if (target_w != (int)w || target_h != (int)h)
    {
        scaled = (uint32_t *)kmalloc((size_t)target_w * (size_t)target_h * sizeof(uint32_t));
        if (scaled)
        {
            for (int y = 0; y < target_h; ++y)
            {
                int sy = (int)((int64_t)y * (int)h / target_h);
                for (int x = 0; x < target_w; ++x)
                {
                    int sx = (int)((int64_t)x * (int)w / target_w);
                    scaled[y * target_w + x] = img[sy * (int)w + sx];
                }
            }
        }
        else
        {
            scaled = img; // fallback to full size if alloc fails
            target_w = (int)w;
            target_h = (int)h;
        }
    }

    fb_set_cursor_image(scaled, target_w, target_h);
    g_cursor_default_img = scaled;
    g_cursor_default_w = target_w;
    g_cursor_default_h = target_h;
}

static void cursor_use_default(void)
{
    if (g_cursor_default_img)
    {
        fb_set_cursor_image(g_cursor_default_img, g_cursor_default_w, g_cursor_default_h);
        g_cursor_resize_active = 0;
    }
    else
    {
        // fallback simple cross
        if (!g_cursor_fallback[0])
        {
            for (int y = 0; y < 9; ++y)
                for (int x = 0; x < 9; ++x)
                    g_cursor_fallback[y * 9 + x] = 0x00000000;
            for (int i = 0; i < 9; ++i)
            {
                g_cursor_fallback[4 * 9 + i] = 0xFFFFFFFF;
                g_cursor_fallback[i * 9 + 4] = 0xFFFFFFFF;
            }
        }
        fb_set_cursor_image(g_cursor_fallback, 9, 9);
        g_cursor_default_img = g_cursor_fallback;
        g_cursor_default_w = 9;
        g_cursor_default_h = 9;
        g_cursor_resize_active = 0;
    }
}

static void cursor_use_resize(void)
{
    if (!g_cursor_resize_img[0])
    {
        for (int y = 0; y < g_cursor_resize_h; ++y)
            for (int x = 0; x < g_cursor_resize_w; ++x)
                g_cursor_resize_img[y * g_cursor_resize_w + x] = 0x00000000;
        for (int i = 0; i < g_cursor_resize_w; ++i)
        {
            g_cursor_resize_img[(g_cursor_resize_h - 1 - i) * g_cursor_resize_w + i] = 0xFFFFFFFF;
            g_cursor_resize_img[i * g_cursor_resize_w + (g_cursor_resize_w - 1 - i)] = 0xFFFFFFFF;
        }
    }
    fb_set_cursor_image(g_cursor_resize_img, g_cursor_resize_w, g_cursor_resize_h);
    g_cursor_resize_active = 1;
}

static void notepad_render(void)
{
    if (!g_notepad.open || g_notepad.minimized)
        return;
    int wx = g_notepad.wx, wy = g_notepad.wy, ww = g_notepad.ww, wh = g_notepad.wh;

    // window frame
    draw_rect(wx - 2, wy - 24, ww + 4, wh + 26, 0xFFE4DAFF);
    ui_draw_hgrad_rect(wx, wy - 22, ww, 20, 0xFF334155, 0xFF273140);
    char title[64];
    sprintf(title, "Text Editor - %s", g_notepad.name[0] ? g_notepad.name : "(unnamed)");
    draw_text(wx + 8, wy - 20, title, COLOR_ACCENT, 0x00000000);

    // simple menu bar with clickable buttons
    int menu_y = wy;
    ui_draw_hgrad_rect(wx, menu_y, ww, NOTEPAD_MENU_H, 0xFF283243, 0xFF222A38);
    const char *labels[3] = { "New", "Open", "Save" };
    int btn_y = menu_y + (NOTEPAD_MENU_H - NOTEPAD_MENU_BTN_H) / 2;
    for (int i = 0; i < 3; ++i)
    {
        int bx = wx + NOTEPAD_MENU_BTN_PAD + i * (NOTEPAD_MENU_BTN_W + NOTEPAD_MENU_BTN_PAD);
        ui_draw_hgrad_rect(bx, btn_y, NOTEPAD_MENU_BTN_W, NOTEPAD_MENU_BTN_H, 0xFF36445A, 0xFF2A3446);
        draw_text(bx + 8, btn_y + 2, labels[i], COLOR_ACCENT, 0x00000000);
    }

    // Title buttons: [ _ ] [ □ ] [ X ]
    int bx = wx + ww - 18 - 18 - 18 - 6;
    int by = wy - 20;
    int bw = 18;
    int bh = 14;
    // minimize
    ui_draw_hgrad_rect(bx, by, bw, bh, 0xFF3D4A60, 0xFF2F3849);
    draw_text(bx + 6, by - 1, "_", 0xFFE7EEF9, 0x00000000);
    // maximize
    ui_draw_hgrad_rect(bx + bw + 2, by, bw, bh, 0xFF3D4A60, 0xFF2F3849);
    draw_text(bx + bw + 2 + 5, by - 1, "[]", 0xFFE7EEF9, 0x00000000);
    // close
    ui_draw_hgrad_rect(bx + 2 * (bw + 2), by, bw, bh, 0xFF6E2B2B, 0xFF581F1F);
    draw_text(bx + 2 * (bw + 2) + 5, by - 1, "X", 0xFFFBECEC, 0x00000000);
    draw_rect(wx, wy, ww, wh, 0xFF10161F);

    // draw buffer text (simple, no wrapping beyond window width)
    int cx = wx + 10;
    int cy = wy + 6 + 18; // below menu bar
    int i = 0;
    char line[128];
    int li = 0;
    int fh = psf_height();
    for (; i < g_notepad.len && cy + fh < wy + wh - 6; ++i)
    {
        char c = g_notepad.buf[i];
        if (c == '\n' || li >= 120)
        {
            line[li] = 0;
            draw_text(cx, cy, line, 0xFFE7EEF9, 0xFF10161F);
            cy += fh + 2;
            li = 0;
        }
        else
        {
            line[li++] = c;
        }
    }
    if (li > 0 && cy + fh < wy + wh - 6)
    {
        line[li] = 0;
        draw_text(cx, cy, line, 0xFFE7EEF9, 0xFF10161F);
    }

    // mark last drawn info (not used by renderer, but kept for state)
    g_notepad.last_x = wx;
    g_notepad.last_y = wy;
    g_notepad.last_w = ww;
    g_notepad.last_h = wh;
    g_notepad.last_drawn = 1;
}

static void notepad_open(const char *name)
{
    // load file content if exists (Desktop relative)
    char fullpath[96];
    desktop_path_for_name(fullpath, sizeof(fullpath), name);
    // reuse common loader
    notepad_open_path(fullpath);
}

static void notepad_open_path(const char *fullpath)
{
    if (!fullpath || !fullpath[0])
        return;

    memset(&g_notepad, 0, sizeof(g_notepad));
    notepad_set_path_and_name(fullpath);
    g_notepad.wx = 120;
    g_notepad.wy = 80;
    g_notepad.ww = (fb.width > 400) ? fb.width - 240 : 320;
    g_notepad.wh = (fb.height > 200) ? fb.height - 160 : 160;
    uint32_t nread = 0;
    int r = fat32_read_file_path(&g_vol, ata_read28, fullpath, g_notepad.buf, sizeof(g_notepad.buf) - 1, &nread);
    if (r == 0)
    {
        g_notepad.len = (int)nread;
        g_notepad.buf[g_notepad.len] = 0;
    }
    g_notepad.open = 1;
    g_notepad.minimized = 0;
    g_notepad.maximized = 0;
    g_notepad.can_minimize = 1;
    g_notepad.can_maximize = 1;
    g_notepad.cursor = g_notepad.len;
    notepad_render();
    wm_set_front(g_win_notepad);
    desktop_mark_dirty();
}

static void notepad_close(void)
{
    // 창이 마지막으로 그려졌던 영역을 복구

    g_notepad.open = 0;
    g_notepad.minimized = 0;
    g_notepad.maximized = 0;
    g_notepad.last_drawn = 0;

    // 전체 화면 다시 렌더 (back buffer → front flush)
    desktop_mark_dirty();
    desktop_render();
}

static void notepad_save_as_auto(void)
{
    // Save to NOTE00.TXT .. NOTE99.TXT
    for (int idx = 0; idx < 100; ++idx)
    {
        char nm[12];
        nm[0] = 'N';
        nm[1] = 'O';
        nm[2] = 'T';
        nm[3] = 'E';
        nm[4] = (char)('0' + (idx / 10));
        nm[5] = (char)('0' + (idx % 10));
        nm[6] = '.';
        nm[7] = 'T';
        nm[8] = 'X';
        nm[9] = 'T';
        nm[10] = 0;
        char fullpath[96];
        desktop_path_for_name(fullpath, sizeof(fullpath), nm);
        int r = fat32_write_file_path(&g_vol, ata_read28, ata_write28, fullpath, g_notepad.buf, (uint32_t)g_notepad.len);
        if (r == 0)
        {
            serial_printf("[NOTEPAD] saved as %s\n", nm);
            desktop_refresh_from_path();
            break;
        }
    }
}

static void sanitize_txt_name(char *out, size_t outsz, const char *in)
{
    size_t j = 0;
    int has_dot = 0;
    for (size_t i = 0; in[i] && j + 1 < outsz; ++i)
    {
        char c = in[i];
        if (c == '.')
        {
            if (has_dot)
                continue;
            has_dot = 1;
            out[j++] = '.';
            continue;
        }
        if (c == ' ')
            c = '_';
        if (c >= 'a' && c <= 'z')
            c -= 32;
        // allow A-Z 0-9 and underscore
        if (!((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_'))
            continue;
        out[j++] = c;
    }
    out[j] = 0;
    if (!has_dot)
    {
        const char *ext = ".TXT";
        for (size_t k = 0; ext[k] && j + 1 < outsz; ++k)
            out[j++] = ext[k];
        out[j] = 0;
    }
}

static void desktop_path_for_name(char *out, size_t outsz, const char *name83)
{
    path_join(out, outsz, g_desktop_dir, name83);
}

static int ensure_volume_mounted(void)
{
    if (g_vol_mounted)
        return 1;

    ata_init();
    ata_identify_t id;
    if (ata_identify(&id) != 0 || !id.present)
    {
        serial_printf("[TXT] no ATA disk present\n");
        return 0;
    }

    uint8_t sec0[512];
    if (ata_read28(0, 1, sec0) != 0)
    {
        serial_printf("[TXT] read LBA0 failed\n");
        return 0;
    }

    mbr_t m;
    int mbr_r = mbr_parse(sec0, &m);
    uint32_t mount_lba = 0;
    int found = 0;
    if (mbr_r == 0 && m.valid)
    {
        for (int i = 0; i < 4; ++i)
        {
            if (m.parts[i].type == 0x0B || m.parts[i].type == 0x0C)
            {
                mount_lba = m.parts[i].lba_first;
                found = 1;
                break;
            }
        }
    }
    if (!found)
        mount_lba = 0; // superfloppy fallback

    int mnt = fat32_mount(&g_vol, ata_read28, mount_lba);
    if (mnt == 0)
    {
        g_vol_mounted = 1;
        serial_printf("[TXT] auto-mounted FAT32 at LBA %u\n", mount_lba);
        ensure_user_dirs_on_disk(g_logged_in_user);
        desktop_refresh_from_path();
        return 1;
    }

    serial_printf("[TXT] auto-mount failed (err=%d)\n", mnt);
    return 0;
}

static int notepad_save_current(const char *name_override)
{
    if (!ensure_volume_mounted())
        return -1;

    const char *src = name_override ? name_override : g_notepad.name;
    if (!src || !src[0])
        return -1;

    char name[32];
    sanitize_txt_name(name, sizeof(name), src);

    char dir[96];
    path_dirname(dir, sizeof(dir), g_notepad.path[0] ? g_notepad.path : g_desktop_dir);

    char fullpath[96];
    if (dir[0])
        path_join(fullpath, sizeof(fullpath), dir, name);
    else
        desktop_path_for_name(fullpath, sizeof(fullpath), name);

    uint32_t bytes = (g_notepad.len > 0) ? (uint32_t)g_notepad.len : 1;
    int r = fat32_write_file_path(&g_vol, ata_read28, ata_write28, fullpath,
                                  (g_notepad.len > 0) ? g_notepad.buf : "\n",
                                  bytes);
    if (r == 0)
    {
        strncpy(g_notepad.name, name, sizeof(g_notepad.name) - 1);
        g_notepad.name[sizeof(g_notepad.name) - 1] = 0;
        notepad_set_path_and_name(fullpath);
        serial_printf("[TXT] saved %s\n", name);
        desktop_refresh_from_path();
    }
    else
    {
        serial_printf("[TXT] save failed (%d) for %s\n", r, name);
    }
    return r;
}

static void notepad_new(void)
{
    memset(g_notepad.buf, 0, sizeof(g_notepad.buf));
    g_notepad.len = 0;
    strncpy(g_notepad.name, "UNTITLED.TXT", sizeof(g_notepad.name) - 1);
    g_notepad.name[sizeof(g_notepad.name) - 1] = 0;
    g_notepad.cursor = 0;
    desktop_path_for_name(g_notepad.path, sizeof(g_notepad.path), g_notepad.name);
    notepad_render();
    desktop_mark_dirty();
}

static void create_txt_file_from_prompt(void)
{
    if (!ensure_volume_mounted() || g_vol.sec_per_clus == 0 || g_vol.tot_sec32 == 0 || g_vol.first_data_lba == 0)
    {
        serial_printf("[TXT] no volume mounted; cannot create file\n");
        name_prompt_active = 0;
        ctx_menu_visible = 0;
        desktop_mark_dirty();
        return;
    }

    char name[32];
    if (name_prompt_len <= 0)
    {
        sanitize_txt_name(name, sizeof(name), "NEWFILE.TXT");
    }
    else
    {
        name_prompt_buf[name_prompt_len] = 0;
        sanitize_txt_name(name, sizeof(name), name_prompt_buf);
    }
    if (name[0] == 0)
        sanitize_txt_name(name, sizeof(name), "NEWFILE.TXT");

    char fullpath[96];
    desktop_path_for_name(fullpath, sizeof(fullpath), name);
    const char placeholder = '\n'; // ensure non-zero size so it shows in filtered lists
    int r = fat32_write_file_path(&g_vol, ata_read28, ata_write28, fullpath, &placeholder, 1);
    if (r == 0)
    {
        serial_printf("[TXT] created %s\n", name);
        desktop_refresh_from_path();
        notepad_open(name);
        wm_set_front(g_win_notepad);
    }
    else
    {
        serial_printf("[TXT] create failed (%d) for %s\n", r, name);
    }

    name_prompt_active = 0;
    ctx_menu_visible = 0;
    desktop_mark_dirty();
}

static void name_prompt_commit(void)
{
    switch (name_prompt_mode)
    {
    case PROMPT_CREATE_TXT:
        create_txt_file_from_prompt();
        break;
    case PROMPT_OPEN_TXT:
        if (ensure_volume_mounted())
        {
            // Deprecated manual entry; keep for fallback
            name_prompt_buf[name_prompt_len] = 0;
            char name[32];
            sanitize_txt_name(name, sizeof(name), name_prompt_buf);
            notepad_open(name);
            wm_set_front(g_win_notepad);
        }
        name_prompt_active = 0;
        ctx_menu_visible = 0;
        desktop_mark_dirty();
        break;
    case PROMPT_SAVE_AS:
        if (ensure_volume_mounted())
        {
                    name_prompt_buf[name_prompt_len] = 0;
                    char name[32];
                    sanitize_txt_name(name, sizeof(name), name_prompt_buf);
                    notepad_save_current(name);
                }
                name_prompt_active = 0;
                ctx_menu_visible = 0;
                desktop_mark_dirty();
                break;
    default:
        name_prompt_active = 0;
        ctx_menu_visible = 0;
        desktop_mark_dirty();
        break;
    }
}

void map_kernel_heap(uintptr_t start, uintptr_t end)
{
    for (uintptr_t addr = start; addr < end; addr += 0x1000)
    {
        uint32_t phys = pmm_alloc_phys();
        if (!phys)
        {
            serial_printf("[kheap] pmm_alloc_phys failed at %p\n", (void *)addr);
            break;
        }
        map_page(
            kernel_pagemap,         // pagemap_t
            addr,                   // VA
            phys,                   // PA
            PAGE_PRESENT | PAGE_RW, // flags
            Size4KiB                // enum page_size
        );
    }
}

static void notepad_taskbar_click(wm_entry_t *win, void *user)
{
    (void)win;
    (void)user;

    if (!g_notepad.open)
        return;

    // Toggle minimized state when clicking on taskbar entry
    if (g_notepad.minimized)
        g_notepad.minimized = 0;
    else
        g_notepad.minimized = 1;

    desktop_mark_dirty();
    desktop_render();
}

// Simple File Explorer window
typedef struct
{
    int open;
    int minimized;
    int wx, wy, ww, wh;
    int dragging;
    int drag_offx, drag_offy;
    int maximized;
    int prev_x, prev_y, prev_w, prev_h;
    int can_minimize;
    int can_maximize;
    int path_focus;
    char path[96];
    int path_len;
    int selection;
} filewin_t;

static filewin_t g_filewin = {0};
static desktop_item_t g_file_items[DESKTOP_MAX_ITEMS];
static int g_file_item_count = 0;
static char g_file_status[64] = "";
static const int FILEWIN_SIDEBAR_W = 140;
static const int FILEWIN_HEADER_H = 22;

static void filewin_close(void)
{
    g_filewin.open = 0;
    g_filewin.minimized = 0;
    g_filewin.maximized = 0;
    file_picker_cancel();
    desktop_mark_dirty();
    desktop_render();
}

static void filewin_toggle_maximize(void)
{
    if (!g_filewin.open || !g_filewin.can_maximize)
        return;

    if (!g_filewin.maximized)
    {
        g_filewin.prev_x = g_filewin.wx;
        g_filewin.prev_y = g_filewin.wy;
        g_filewin.prev_w = g_filewin.ww;
        g_filewin.prev_h = g_filewin.wh;
        int top = 28;
        int bottom = 32;
        g_filewin.wx = 6;
        g_filewin.wy = top + 4;
        g_filewin.ww = fb.width - 12;
        g_filewin.wh = fb.height - top - bottom - 8;
        g_filewin.maximized = 1;
    }
    else
    {
        g_filewin.wx = g_filewin.prev_x;
        g_filewin.wy = g_filewin.prev_y;
        g_filewin.ww = g_filewin.prev_w;
        g_filewin.wh = g_filewin.prev_h;
        g_filewin.maximized = 0;
    }
    desktop_mark_dirty();
}

static void filewin_set_path(const char *path)
{
    if (!path || !path[0])
        return;
    strncpy(g_filewin.path, path, sizeof(g_filewin.path) - 1);
    g_filewin.path[sizeof(g_filewin.path) - 1] = 0;
    g_filewin.path_len = (int)strlen(g_filewin.path);
    g_filewin.selection = -1;
    filewin_refresh_list();
    desktop_mark_dirty();
}

static void filewin_refresh_list(void)
{
    g_file_item_count = 0;
    g_file_status[0] = 0;
    if (!g_vol_mounted)
    {
        strcpy(g_file_status, "Disk not mounted");
        return;
    }

    fat32_dirent_t tmp[DESKTOP_MAX_ITEMS];
    int n = 0;
    if (fat32_list_dir_path(&g_vol, ata_read28, g_filewin.path, tmp, DESKTOP_MAX_ITEMS, &n) != 0)
    {
        strcpy(g_file_status, "Path not found");
        return;
    }

    int count = 0;
    for (int i = 0; i < n && count < DESKTOP_MAX_ITEMS; ++i)
    {
        // Skip zero-length files (treated as deleted placeholders)
        if (!(tmp[i].attr & 0x10) && tmp[i].size == 0)
            continue;
        int j = 0;
        for (; j < (int)sizeof(g_file_items[count].name) - 1 && tmp[i].name83[j]; ++j)
            g_file_items[count].name[j] = tmp[i].name83[j];
        g_file_items[count].name[j] = 0;
        g_file_items[count].attr = tmp[i].attr;
        g_file_items[count].size = tmp[i].size;
        count++;
    }
    g_file_item_count = count;
    g_filewin.selection = -1;
    strcpy(g_file_status, "OK");
}

static void filewin_move_selection(int delta)
{
    if (g_file_item_count <= 0)
        return;
    if (g_filewin.selection < 0)
        g_filewin.selection = 0;
    g_filewin.selection += delta;
    if (g_filewin.selection < 0)
        g_filewin.selection = 0;
    if (g_filewin.selection >= g_file_item_count)
        g_filewin.selection = g_file_item_count - 1;
    desktop_mark_dirty();
}

static void filewin_delete_selection(void)
{
    if (!g_vol_mounted || g_filewin.selection < 0 || g_filewin.selection >= g_file_item_count)
        return;
    const desktop_item_t *it = &g_file_items[g_filewin.selection];
    if (!it || (it->attr & 0x10))
        return; // skip directories for now
    char full[128];
    path_join(full, sizeof(full), g_filewin.path, it->name);
    int r = fat32_write_file_path(&g_vol, ata_read28, ata_write28, full, "", 0);
    if (r == 0)
    {
        serial_printf("[FILE] deleted (zeroed) %s\n", full);
        filewin_refresh_list();
        desktop_refresh_from_path();
    }
    else
    {
        serial_printf("[FILE] delete failed (%d) %s\n", r, full);
    }
}

static void launch_refresh_items(void)
{
    g_launch_count = 0;
    // Core actions
    strcpy(g_launch_items[g_launch_count].name, "Logout");
    g_launch_items[g_launch_count].type = L_ACTION_LOGOUT;
    g_launch_items[g_launch_count].path[0] = 0;
    g_launch_count++;

    strcpy(g_launch_items[g_launch_count].name, "Reboot");
    g_launch_items[g_launch_count].type = L_ACTION_REBOOT;
    g_launch_items[g_launch_count].path[0] = 0;
    g_launch_count++;

    strcpy(g_launch_items[g_launch_count].name, "Shutdown");
    g_launch_items[g_launch_count].type = L_ACTION_SHUTDOWN;
    g_launch_items[g_launch_count].path[0] = 0;
    g_launch_count++;

    // User programs
    if (g_vol_mounted)
    {
        fat32_dirent_t tmp[16];
        int n = 0;
        if (fat32_list_dir_path(&g_vol, ata_read28, g_prog_dir, tmp, 16, &n) == 0)
        {
            for (int i = 0; i < n && g_launch_count < (int)(sizeof(g_launch_items) / sizeof(g_launch_items[0])); ++i)
            {
                if (tmp[i].attr & 0x10) // skip dirs
                    continue;
                launch_item_t *it = &g_launch_items[g_launch_count++];
                strncpy(it->name, tmp[i].name83, sizeof(it->name) - 1);
                it->name[sizeof(it->name) - 1] = 0;
                it->type = L_ACTION_APP;
                path_join(it->path, sizeof(it->path), g_prog_dir, tmp[i].name83);
            }
        }
    }
    g_launch_last_refresh = jiffies;
}

static void launch_toggle(int show)
{
    if (show)
    {
        launch_refresh_items();
        int h = g_launch_item_h * g_launch_count + 12;
        int taskbar_h = 34;
        g_launch_x = 8;
        g_launch_y = (int)fb.height - taskbar_h - h - 4;
        if (g_launch_y < 4)
            g_launch_y = 4;
        g_launch_visible = 1;
    }
    else
    {
        g_launch_visible = 0;
    }
    desktop_mark_dirty();
}

static void launch_do_logout(void)
{
    g_session_started = 0;
    g_login_active = 1;
    g_login_dirty = 1;
    g_login_pw_focus = 1;
    login_reset_password();
    strncpy(g_login_message, "Select a user and log in.", sizeof(g_login_message) - 1);
    g_notepad.open = 0;
    g_filewin.open = 0;
    g_launch_visible = 0;
    desktop_mark_dirty();
}

static void launch_activate(int idx)
{
    if (idx < 0 || idx >= g_launch_count)
        return;
    launch_item_t *it = &g_launch_items[idx];
    switch (it->type)
    {
    case L_ACTION_LOGOUT:
        launch_do_logout();
        break;
    case L_ACTION_REBOOT:
        serial_printf("[Launch] reboot requested (not implemented)\n");
        break;
    case L_ACTION_SHUTDOWN:
        serial_printf("[Launch] shutdown requested (hlt)\n");
        for (;;)
            __asm__ __volatile__("cli; hlt");
        break;
    case L_ACTION_APP:
        if (str_ieq_ext(it->name, "TXT"))
        {
            // Copy name portion only for open
            char fname[13];
            strncpy(fname, it->name, sizeof(fname) - 1);
            fname[sizeof(fname) - 1] = 0;
            notepad_open(fname);
        }
        else
        {
            serial_printf("[Launch] no handler for %s\n", it->name);
        }
        break;
    default:
        break;
    }
    g_launch_visible = 0;
    desktop_mark_dirty();
}

static int point_in_rect(int mx, int my, int x, int y, int w, int h)
{
    return (mx >= x && mx < x + w && my >= y && my < y + h);
}

static void ctx_menu_hide(void)
{
    ctx_menu_visible = 0;
    ctx_menu_target = CTX_NONE;
    g_ctx_count = 0;
    desktop_mark_dirty();
}

static void ctx_menu_build(ctx_target_t target)
{
    g_ctx_count = 0;
    switch (target)
    {
    case CTX_DESKTOP:
        strcpy(g_ctx_items[g_ctx_count].label, "New Text File");
        g_ctx_items[g_ctx_count++].act = CTX_ACT_NEW_TXT;
        strcpy(g_ctx_items[g_ctx_count].label, "Refresh");
        g_ctx_items[g_ctx_count++].act = CTX_ACT_REFRESH_DESKTOP;
        break;
    case CTX_NOTEPAD:
        strcpy(g_ctx_items[g_ctx_count].label, "New");
        g_ctx_items[g_ctx_count++].act = CTX_ACT_NOTEPAD_NEW;
        strcpy(g_ctx_items[g_ctx_count].label, "Open...");
        g_ctx_items[g_ctx_count++].act = CTX_ACT_NOTEPAD_OPEN;
        strcpy(g_ctx_items[g_ctx_count].label, "Save");
        g_ctx_items[g_ctx_count++].act = CTX_ACT_NOTEPAD_SAVE;
        strcpy(g_ctx_items[g_ctx_count].label, "Save As...");
        g_ctx_items[g_ctx_count++].act = CTX_ACT_NOTEPAD_SAVE_AS;
        strcpy(g_ctx_items[g_ctx_count].label, "Close");
        g_ctx_items[g_ctx_count++].act = CTX_ACT_NOTEPAD_CLOSE;
        break;
    case CTX_FILEWIN:
        strcpy(g_ctx_items[g_ctx_count].label, "Open");
        g_ctx_items[g_ctx_count++].act = CTX_ACT_FILE_OPEN;
        strcpy(g_ctx_items[g_ctx_count].label, "Up");
        g_ctx_items[g_ctx_count++].act = CTX_ACT_FILE_UP;
        strcpy(g_ctx_items[g_ctx_count].label, "Refresh");
        g_ctx_items[g_ctx_count++].act = CTX_ACT_FILE_REFRESH;
        break;
    default:
        break;
    }

    int max_len = 0;
    for (int i = 0; i < g_ctx_count; ++i)
    {
        int len = (int)strlen(g_ctx_items[i].label);
        if (len > max_len)
            max_len = len;
    }
    int char_w = psf_width() ? psf_width() : 8;
    ctx_menu_w = 20 + max_len * char_w;
    ctx_menu_h = g_ctx_count * ctx_menu_item_h + 8;
}

static void ctx_menu_show(ctx_target_t target, int x, int y)
{
    ctx_menu_target = target;
    ctx_menu_x = x;
    ctx_menu_y = y;
    ctx_menu_build(target);
    ctx_menu_visible = (g_ctx_count > 0);
    desktop_mark_dirty();
}

static void filewin_go_up(void)
{
    int len = (int)strlen(g_filewin.path);
    if (len <= 0)
        return;
    int i = len - 1;
    while (i >= 0 && g_filewin.path[i] == '/')
        i--;
    while (i >= 0 && g_filewin.path[i] != '/')
        i--;
    if (i <= 0)
    {
        filewin_set_path(g_path_base);
        return;
    }
    g_filewin.path[i] = 0;
    g_filewin.path_len = i;
    filewin_refresh_list();
    desktop_mark_dirty();
}

static void ctx_menu_handle_action(ctx_action_t act)
{
    switch (act)
    {
    case CTX_ACT_NEW_TXT:
        start_name_prompt(PROMPT_CREATE_TXT, "NEWFILE.TXT", "New TXT filename:");
        break;
    case CTX_ACT_REFRESH_DESKTOP:
        desktop_refresh_from_path();
        break;
    case CTX_ACT_NOTEPAD_NEW:
        notepad_new();
        break;
    case CTX_ACT_NOTEPAD_OPEN:
        file_picker_start("TXT", notepad_file_pick_cb, NULL);
        break;
    case CTX_ACT_NOTEPAD_SAVE:
        notepad_save_current(NULL);
        break;
    case CTX_ACT_NOTEPAD_SAVE_AS:
        start_name_prompt(PROMPT_SAVE_AS, g_notepad.name[0] ? g_notepad.name : "UNTITLED.TXT", "Save As:");
        break;
    case CTX_ACT_NOTEPAD_CLOSE:
        notepad_close();
        break;
    case CTX_ACT_FILE_REFRESH:
        filewin_refresh_list();
        break;
    case CTX_ACT_FILE_OPEN:
        if (g_filewin.selection >= 0 && g_filewin.selection < g_file_item_count)
        {
            const desktop_item_t *it = &g_file_items[g_filewin.selection];
            if (it->attr & 0x10)
            {
                char new_path[96];
                path_join(new_path, sizeof(new_path), g_filewin.path, it->name);
                filewin_set_path(new_path);
            }
            else if (str_ieq_ext(it->name, "TXT"))
            {
                notepad_open(it->name);
            }
        }
        break;
    case CTX_ACT_FILE_UP:
        filewin_go_up();
        break;
    case CTX_ACT_WIN_MINIMIZE:
        topmenu_apply_action(CTX_ACT_WIN_MINIMIZE);
        break;
    case CTX_ACT_WIN_TOGGLE_MAX:
        topmenu_apply_action(CTX_ACT_WIN_TOGGLE_MAX);
        break;
    default:
        break;
    }
    ctx_menu_hide();
}

static void filewin_open(void)
{
    if (g_filewin.open && !g_filewin.minimized)
        return;
    g_filewin.open = 1;
    g_filewin.minimized = 0;
    g_filewin.wx = 80;
    g_filewin.wy = 80;
    g_filewin.ww = (fb.width > 500) ? fb.width - 200 : 320;
    g_filewin.wh = (fb.height > 300) ? fb.height - 220 : 180;
    g_filewin.maximized = 0;
    g_filewin.can_minimize = 1;
    g_filewin.can_maximize = 1;
    strncpy(g_filewin.path, g_desktop_dir, sizeof(g_filewin.path) - 1);
    g_filewin.path[sizeof(g_filewin.path) - 1] = 0;
    g_filewin.path_len = (int)strlen(g_filewin.path);
    g_filewin.path_focus = 0;
    g_filewin.selection = -1;
    filewin_refresh_list();
    wm_set_front(g_win_file);
    desktop_mark_dirty();
}

static void filewin_render(void)
{
    if (!g_filewin.open || g_filewin.minimized)
        return;

    int wx = g_filewin.wx, wy = g_filewin.wy;
    int ww = g_filewin.ww, wh = g_filewin.wh;

    const int sidebar_w = FILEWIN_SIDEBAR_W;
    const int header_h = FILEWIN_HEADER_H;

    draw_rect(wx - 2, wy - 24, ww + 4, wh + 26, 0xFF101010);
    ui_draw_hgrad_rect(wx, wy - 22, ww, 20, 0xFF2F3948, 0xFF252E3A);
    draw_text(wx + 8, wy - 20, "File Explorer", COLOR_ACCENT, 0x00000000);
    int close_w = 18, close_h = 14;
    int close_x = wx + ww - close_w - 6;
    int close_y = wy - 20;
    ui_draw_hgrad_rect(close_x, close_y, close_w, close_h, 0xFF6E2B2B, 0xFF581F1F);
    draw_text(close_x + 5, close_y - 1, "X", 0xFFFBECEC, 0x00000000);
    draw_rect(wx, wy, ww, wh, 0xFF0F141C);

    // Header path bar
    ui_draw_hgrad_rect(wx, wy, ww, header_h, 0xFF202A3A, 0xFF1A2331);
    draw_rect(wx + 8, wy + 4, ww - 16, header_h - 8, g_filewin.path_focus ? 0xFF284B7A : 0xFF121925);
    draw_text(wx + 12, wy + 6, g_filewin.path, COLOR_TEXT, g_filewin.path_focus ? 0xFF284B7A : 0xFF121925);

    // Sidebar
    int sb_x = wx;
    int sb_y = wy + header_h;
    int sb_h = wh - header_h;
    draw_rect(sb_x, sb_y, sidebar_w, sb_h, 0xFF151A23);
    int row_h = psf_height() + 6;
    int sy = sb_y + 6;
    draw_text(sb_x + 10, sy, "Favorites", 0xFF7FA5FF, 0xFF15181C);
    sy += row_h;
    draw_text(sb_x + 18, sy, "Desktop", 0xFFFFFFFF, 0xFF15181C);
    sy += row_h;
    draw_text(sb_x + 18, sy, "Wallpaper", 0xFFCCCCCC, 0xFF15181C);
    sy += row_h * 2;
    draw_text(sb_x + 10, sy, "This PC", 0xFF7FA5FF, 0xFF15181C);
    sy += row_h;
    draw_text(sb_x + 18, sy, "ATA0", 0xFFFFFFFF, 0xFF15181C);

    // Main list
    int list_x = sb_x + sidebar_w + 6;
    int list_y = wy + header_h + 4;
    int list_w = ww - sidebar_w - 12;
    int list_h = wh - header_h - 8;
    draw_rect(list_x - 2, list_y - 2, list_w + 4, list_h + 4, 0xFF0E1116);

    int cell_w = 96;
    int cell_h = 80;
    int cols = (list_w / cell_w) > 0 ? (list_w / cell_w) : 1;
    int count = g_file_item_count;
    for (int i = 0; i < count; ++i)
    {
        int col = i % cols;
        int row = i / cols;
        int cx = list_x + col * cell_w;
        int cy = list_y + row * cell_h;
        if (cy + cell_h > list_y + list_h)
            break;
        const desktop_item_t *it = &g_file_items[i];
        uint32_t bg = (i == g_filewin.selection) ? 0xFF233043 : 0xFF0F141C;
        draw_rect(cx, cy, cell_w - 6, cell_h - 6, bg);
        uint32_t ic = (it->attr & 0x10) ? 0xFF2F6FAB : 0xFF5E8C31;
        int icon_sz = 32;
        draw_rect(cx + 8, cy + 8, icon_sz, icon_sz, ic);
        draw_rect(cx + 8, cy + 8, icon_sz, 1, 0xFF000000);
        draw_rect(cx + 8, cy + 8 + icon_sz - 1, icon_sz, 1, 0xFF000000);
        draw_rect(cx + 8, cy + 8, 1, icon_sz, 0xFF000000);
        draw_rect(cx + 8 + icon_sz - 1, cy + 8, 1, icon_sz, 0xFF000000);
        draw_text(cx + 8, cy + icon_sz + 14, it->name, 0xFFFFFFFF, bg);
    }

    if (g_file_status[0])
        draw_text(list_x, list_y + list_h - (psf_height() + 4), g_file_status, 0xFF888888, 0xFF0E1116);
}

static void filewin_taskbar_click(wm_entry_t *win, void *user)
{
    (void)win;
    (void)user;

    if (!g_filewin.open)
        filewin_open();
    else
        g_filewin.minimized = !g_filewin.minimized;

    desktop_mark_dirty();
    desktop_render();
}

// --- File picker helper using File Explorer UI ---
static void file_picker_cancel(void)
{
    g_file_picker.active = 0;
    g_file_picker.cb = NULL;
    g_file_picker.cb_user = NULL;
    g_file_picker.ext[0] = 0;
}

static void file_picker_handle_selection(const char *fullpath, const char *name83)
{
    if (!g_file_picker.active || !g_file_picker.cb)
        return;
    g_file_picker.cb(fullpath, name83, g_file_picker.cb_user);
    file_picker_cancel();
    g_filewin.open = 0;
    desktop_mark_dirty();
    desktop_render();
}

static void file_picker_start(const char *ext_filter,
                              void (*on_select)(const char *fullpath, const char *name83, void *user),
                              void *user)
{
    file_picker_cancel();
    g_file_picker.active = 1;
    g_file_picker.cb = on_select;
    g_file_picker.cb_user = user;
    g_file_picker.ext[0] = 0;
    if (ext_filter && ext_filter[0])
    {
        size_t i = 0;
        for (; ext_filter[i] && i + 1 < sizeof(g_file_picker.ext) - 1; ++i)
        {
            char c = ext_filter[i];
            if (c >= 'a' && c <= 'z')
                c -= 32;
            g_file_picker.ext[i] = c;
        }
        g_file_picker.ext[i] = 0;
    }

    if (!g_filewin.open)
        filewin_open();
    filewin_set_path(g_desktop_dir);
    g_filewin.path_focus = 0;
    wm_set_front(g_win_file);
}

// Simple System Monitor window
typedef struct
{
    int open;
    int minimized;
    int wx, wy, ww, wh;
    int dragging;
    int drag_offx, drag_offy;
    int maximized;
    int prev_x, prev_y, prev_w, prev_h;
    int can_minimize;
    int can_maximize;
} taskmgr_t;

static taskmgr_t g_taskmgr = {0};

static void taskmgr_close(void)
{
    g_taskmgr.open = 0;
    g_taskmgr.minimized = 0;
    g_taskmgr.maximized = 0;
    desktop_mark_dirty();
    desktop_render();
}

static void taskmgr_toggle_maximize(void)
{
    if (!g_taskmgr.open || !g_taskmgr.can_maximize)
        return;

    if (!g_taskmgr.maximized)
    {
        g_taskmgr.prev_x = g_taskmgr.wx;
        g_taskmgr.prev_y = g_taskmgr.wy;
        g_taskmgr.prev_w = g_taskmgr.ww;
        g_taskmgr.prev_h = g_taskmgr.wh;
        int top = 28;
        int bottom = 32;
        g_taskmgr.wx = 6;
        g_taskmgr.wy = top + 4;
        g_taskmgr.ww = fb.width - 12;
        g_taskmgr.wh = fb.height - top - bottom - 8;
        g_taskmgr.maximized = 1;
    }
    else
    {
        g_taskmgr.wx = g_taskmgr.prev_x;
        g_taskmgr.wy = g_taskmgr.prev_y;
        g_taskmgr.ww = g_taskmgr.prev_w;
        g_taskmgr.wh = g_taskmgr.prev_h;
        g_taskmgr.maximized = 0;
    }
    desktop_mark_dirty();
}

static void taskmgr_open(void)
{
    if (g_taskmgr.open && !g_taskmgr.minimized)
        return;
    g_taskmgr.open = 1;
    g_taskmgr.minimized = 0;
    g_taskmgr.wx = 120;
    g_taskmgr.wy = 100;
    g_taskmgr.ww = (fb.width > 480) ? fb.width - 240 : 360;
    g_taskmgr.wh = (fb.height > 260) ? fb.height - 260 : 200;
    g_taskmgr.maximized = 0;
    g_taskmgr.can_minimize = 1;
    g_taskmgr.can_maximize = 1;
    wm_set_front(g_win_taskmgr);
    desktop_mark_dirty();
}

static const char *task_state_str(task_state_t st)
{
    switch (st)
    {
    case TASK_READY:
        return "READY";
    case TASK_RUNNING:
        return "RUN";
    case TASK_BLOCKED:
        return "BLOCK";
    case TASK_ZOMBIE:
        return "ZOMB";
    default:
        return "UNK";
    }
}

static void taskmgr_render(void)
{
    if (!g_taskmgr.open || g_taskmgr.minimized)
        return;

    int wx = g_taskmgr.wx, wy = g_taskmgr.wy;
    int ww = g_taskmgr.ww, wh = g_taskmgr.wh;

    draw_rect(wx - 2, wy - 24, ww + 4, wh + 26, 0xFF101010);
    ui_draw_hgrad_rect(wx, wy - 22, ww, 20, 0xFF2F3948, 0xFF252E3A);
    draw_text(wx + 8, wy - 20, "System Monitor", COLOR_ACCENT, 0x00000000);
    int close_w = 18, close_h = 14;
    int close_x = wx + ww - close_w - 6;
    int close_y = wy - 20;
    ui_draw_hgrad_rect(close_x, close_y, close_w, close_h, 0xFF6E2B2B, 0xFF581F1F);
    draw_text(close_x + 5, close_y - 1, "X", 0xFFFBECEC, 0x00000000);
    draw_rect(wx, wy, ww, wh, 0xFF0F141C);

    int row_h = psf_height() + 4;
    int bar_h = 10;
    int y = wy + 6;

    char line[96];

    // Memory usage
    extern uint32_t pmm_total_count(void);
    extern uint32_t pmm_free_count(void);
    uint32_t total_pages = pmm_total_count();
    uint32_t free_pages = pmm_free_count();
    uint32_t used_pages = (total_pages > free_pages) ? (total_pages - free_pages) : 0;
    int bar_x = wx + 6;
    int bar_y = y + psf_height() + 2;
    int bar_w = ww - 12;
    if (bar_w < 16)
        bar_w = 16;

    if (total_pages == 0)
    {
        draw_text(wx + 6, y, "Memory: tracking unavailable", 0xFFFFFFFF, 0xFF000000);
        y += row_h;
    }
    else
    {
        uint32_t total_kb = total_pages * 4;
        uint32_t used_kb = used_pages * 4;
        uint32_t used_pct = total_pages ? (used_pages * 100u) / total_pages : 0;

        sprintf(line, "Memory: %u / %u KB used (%u%%)",
                used_kb, total_kb, used_pct);
        draw_text(wx + 6, y, line, 0xFFFFFFFF, 0xFF000000);

        if (bar_y + bar_h <= wy + wh - 4)
        {
            draw_rect(bar_x, bar_y, bar_w, bar_h, 0xFF202020);
            uint32_t mem_color = ui_usage_color(used_pct);
            int mem_fill = (int)(bar_w * used_pct / 100u);
            if (mem_fill > 0)
                ui_draw_bar_soft(bar_x, bar_y, mem_fill, bar_h, mem_color);
        }
        y = bar_y + bar_h + 6;
    }

    // Uptime (no width formatting; sprintf is minimal)
    extern volatile uint64_t jiffies;
    uint64_t ticks = jiffies;
    uint32_t sec = (uint32_t)(ticks / 100u);
    uint32_t min = sec / 60u;
    uint32_t hr = min / 60u;
    sec %= 60u;
    min %= 60u;

    sprintf(line, "Uptime: %u:%u:%u", hr, min, sec);
    draw_text(wx + 6, y, line, 0xFFFFFFFF, 0xFF000000);
    y += row_h;

    // CPU usage (simple idle-ratio based)
    extern uint32_t task_cpu_usage_percent(void);
    uint32_t cpu_pct = task_cpu_usage_percent();
    sprintf(line, "CPU: %u%%", cpu_pct);
    draw_text(wx + 6, y, line, 0xFFFFFFFF, 0xFF000000);

    // CPU usage bar
    bar_y = y + psf_height() + 2;
    if (bar_y + bar_h <= wy + wh - 4)
    {
        draw_rect(bar_x, bar_y, bar_w, bar_h, 0xFF202020);
        uint32_t cpu_color = ui_usage_color(cpu_pct);
        int cpu_fill = (int)(bar_w * cpu_pct / 100u);
        if (cpu_fill > 0)
            ui_draw_bar_soft(bar_x, bar_y, cpu_fill, bar_h, cpu_color);
    }
}

static void taskmgr_taskbar_click(wm_entry_t *win, void *user)
{
    (void)win;
    (void)user;

    if (!g_taskmgr.open)
        taskmgr_open();
    else
        g_taskmgr.minimized = !g_taskmgr.minimized;

    desktop_mark_dirty();
    desktop_render();
}

// Simple Display Settings window
typedef struct
{
    int open;
    int minimized;
    int wx, wy, ww, wh;
    int dragging;
    int drag_offx, drag_offy;
    int maximized;
    int prev_x, prev_y, prev_w, prev_h;
    int can_minimize;
    int can_maximize;
} display_t;

static display_t g_display = {0};

static void display_close(void)
{
    g_display.open = 0;
    g_display.minimized = 0;
    g_display.maximized = 0;
    desktop_mark_dirty();
    desktop_render();
}

static void display_open(void)
{
    if (g_display.open && !g_display.minimized)
        return;
    g_display.open = 1;
    g_display.minimized = 0;
    g_display.wx = 140;
    g_display.wy = 120;
    g_display.ww = 360;
    g_display.wh = 160;
    g_display.maximized = 0;
    g_display.can_minimize = 1;
    g_display.can_maximize = 0;
    wm_set_front(g_win_display);
    desktop_mark_dirty();
}

static void display_render(void)
{
    if (!g_display.open || g_display.minimized)
        return;

    int wx = g_display.wx, wy = g_display.wy;
    int ww = g_display.ww, wh = g_display.wh;

    draw_rect(wx - 2, wy - 24, ww + 4, wh + 26, 0xFF101010);
    ui_draw_hgrad_rect(wx, wy - 22, ww, 20, 0xFF2F3948, 0xFF252E3A);
    draw_text(wx + 8, wy - 20, "Display Settings", COLOR_ACCENT, 0x00000000);
    int close_w = 18, close_h = 14;
    int close_x = wx + ww - close_w - 6;
    int close_y = wy - 20;
    ui_draw_hgrad_rect(close_x, close_y, close_w, close_h, 0xFF6E2B2B, 0xFF581F1F);
    draw_text(close_x + 5, close_y - 1, "X", 0xFFFBECEC, 0x00000000);
    draw_rect(wx, wy, ww, wh, 0xFF0F141C);

    int row_h = psf_height() + 4;
    int y = wy + 6;

    char line[96];
    sprintf(line, "Current resolution: %ux%u, %u bpp",
            g_bootinfo.fb_w, g_bootinfo.fb_h, g_bootinfo.fb_bpp);
    draw_text(wx + 6, y, line, 0xFFFFFFFF, 0xFF000000);
    y += row_h;

    sprintf(line, "Framebuffer pitch: %u bytes/line", g_bootinfo.fb_pitch);
    draw_text(wx + 6, y, line, 0xFFCCCCCC, 0xFF000000);
    y += row_h;

    draw_text(wx + 6, y, "Runtime resolution change is not implemented yet.",
              0xFFAAAAAA, 0xFF000000);
    y += row_h;

    draw_text(wx + 6, y, "Change video mode via bootloader/firmware settings.",
              0xFF888888, 0xFF000000);
}

static void display_taskbar_click(wm_entry_t *win, void *user)
{
    (void)win;
    (void)user;

    if (!g_display.open)
        display_open();
    else
        g_display.minimized = !g_display.minimized;

    desktop_mark_dirty();
    desktop_render();
}

// Simple Terminal stub window
typedef struct
{
    int open;
    int minimized;
    int wx, wy, ww, wh;
    int dragging;
    int drag_offx, drag_offy;
    int maximized;
    int prev_x, prev_y, prev_w, prev_h;
    int can_minimize;
    int can_maximize;
} terminal_t;

static terminal_t g_terminal = {0};

static void terminal_open(void)
{
    if (g_terminal.open && !g_terminal.minimized)
        return;
    g_terminal.open = 1;
    g_terminal.minimized = 0;
    g_terminal.wx = 180;
    g_terminal.wy = 120;
    g_terminal.ww = (fb.width > 420) ? fb.width - 260 : 360;
    g_terminal.wh = (fb.height > 260) ? fb.height - 260 : 220;
    g_terminal.maximized = 0;
    g_terminal.can_minimize = 1;
    g_terminal.can_maximize = 1;
    wm_set_front(g_win_terminal);
    desktop_mark_dirty();
}

static void terminal_close(void)
{
    g_terminal.open = 0;
    g_terminal.minimized = 0;
    g_terminal.maximized = 0;
    desktop_mark_dirty();
    desktop_render();
}

static void terminal_toggle_maximize(void)
{
    if (!g_terminal.open || !g_terminal.can_maximize)
        return;
    if (!g_terminal.maximized)
    {
        g_terminal.prev_x = g_terminal.wx;
        g_terminal.prev_y = g_terminal.wy;
        g_terminal.prev_w = g_terminal.ww;
        g_terminal.prev_h = g_terminal.wh;
        int top = 28;
        int bottom = 32;
        g_terminal.wx = 6;
        g_terminal.wy = top + 4;
        g_terminal.ww = fb.width - 12;
        g_terminal.wh = fb.height - top - bottom - 8;
        g_terminal.maximized = 1;
    }
    else
    {
        g_terminal.wx = g_terminal.prev_x;
        g_terminal.wy = g_terminal.prev_y;
        g_terminal.ww = g_terminal.prev_w;
        g_terminal.wh = g_terminal.prev_h;
        g_terminal.maximized = 0;
    }
    desktop_mark_dirty();
}

static void terminal_render(void)
{
    if (!g_terminal.open || g_terminal.minimized)
        return;
    int wx = g_terminal.wx, wy = g_terminal.wy;
    int ww = g_terminal.ww, wh = g_terminal.wh;

    draw_rect(wx - 2, wy - 24, ww + 4, wh + 26, 0xFF101010);
    ui_draw_hgrad_rect(wx, wy - 22, ww, 20, 0xFF2F3948, 0xFF252E3A);
    draw_text(wx + 8, wy - 20, "Terminal", COLOR_ACCENT, 0x00000000);
    int close_w = 18, close_h = 14;
    int close_x = wx + ww - close_w - 6;
    int close_y = wy - 20;
    ui_draw_hgrad_rect(close_x, close_y, close_w, close_h, 0xFF6E2B2B, 0xFF581F1F);
    draw_text(close_x + 5, close_y - 1, "X", 0xFFFBECEC, 0x00000000);
    draw_rect(wx, wy, ww, wh, 0xFF0C111A);

    int y = wy + 8;
    draw_text(wx + 8, y, "ParanOS Terminal (stub)", COLOR_ACCENT, 0xFF0C111A); y += psf_height() + 4;
    draw_text(wx + 8, y, "Shell not implemented yet.", COLOR_TEXT, 0xFF0C111A); y += psf_height() + 2;
    draw_text(wx + 8, y, "Future: command runner / system shell.", 0xFF9AA5B8, 0xFF0C111A);
}

static void terminal_taskbar_click(wm_entry_t *win, void *user)
{
    (void)win;
    (void)user;
    if (!g_terminal.open)
        terminal_open();
    else
        g_terminal.minimized = !g_terminal.minimized;
    desktop_mark_dirty();
    desktop_render();
}

// Simple Image Viewer window (BMP)
typedef struct
{
    int open;
    int minimized;
    int wx, wy, ww, wh;
    int dragging;
    int drag_offx, drag_offy;
    int resizing;
    int resize_offx, resize_offy;
    int maximized;
    int prev_x, prev_y, prev_w, prev_h;
    int can_minimize;
    int can_maximize;
    uint32_t *img;
    int img_w, img_h;
    char path[96];
} imgview_t;

static imgview_t g_imgview = {0};

static void imgview_free_image(void)
{
    // no free available; keep simple leak avoidance by reusing single buffer
    g_imgview.img = NULL;
    g_imgview.img_w = g_imgview.img_h = 0;
}

static int imgview_load_bmp(const char *fullpath, uint32_t **out_img, int *ow, int *oh)
{
    if (!fullpath || !out_img || !ow || !oh)
        return -1;
    uint8_t hdr[128];
    uint32_t n = 0;
    if (fat32_read_file_path(&g_vol, ata_read28, fullpath, hdr, sizeof(hdr), &n) != 0 || n < 54)
        return -1;
    if (hdr[0] != 'B' || hdr[1] != 'M')
        return -1;
    uint32_t file_size = *(uint32_t *)&hdr[2];
    uint32_t data_off = *(uint32_t *)&hdr[10];
    int32_t w = *(int32_t *)&hdr[18];
    int32_t h_raw = *(int32_t *)&hdr[22];
    uint16_t planes = *(uint16_t *)&hdr[26];
    uint16_t bpp = *(uint16_t *)&hdr[28];
    uint32_t comp = *(uint32_t *)&hdr[30];
    if (file_size == 0 || file_size > 8 * 1024 * 1024)
        return -1;
    if (planes != 1 || (bpp != 24 && bpp != 32) || comp != 0 || w <= 0 || h_raw == 0)
        return -1;
    uint8_t *buf = kmalloc(file_size);
    if (!buf)
        return -1;
    uint32_t full = 0;
    if (fat32_read_file_path(&g_vol, ata_read28, fullpath, buf, file_size, &full) != 0 || full < file_size)
        return -1;
    if (data_off >= file_size)
        return -1;
    uint32_t h = (h_raw < 0) ? (uint32_t)(-h_raw) : (uint32_t)h_raw;
    int top_down = (h_raw < 0);
    uint32_t row_bytes_raw = ((uint32_t)w * (uint32_t)bpp + 31) / 32 * 4;
    uint32_t *img = (uint32_t *)kmalloc((size_t)w * (size_t)h * sizeof(uint32_t));
    if (!img)
        return -1;
    const uint8_t *src = buf + data_off;
    for (uint32_t y = 0; y < h; ++y)
    {
        uint32_t sy = top_down ? y : (h - 1 - y);
        const uint8_t *row = src + sy * row_bytes_raw;
        for (uint32_t x = 0; x < (uint32_t)w; ++x)
        {
            uint8_t b = row[x * (bpp / 8) + 0];
            uint8_t g = row[x * (bpp / 8) + 1];
            uint8_t r = row[x * (bpp / 8) + 2];
            uint8_t a = (bpp == 32) ? row[x * 4 + 3] : 0xFF;
            img[y * (uint32_t)w + x] = ((uint32_t)a << 24) | ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
        }
    }
    *out_img = img;
    *ow = w;
    *oh = (int)h;
    return 0;
}

static void imgview_render(void)
{
    if (!g_imgview.open || g_imgview.minimized)
        return;
    int wx = g_imgview.wx, wy = g_imgview.wy;
    int ww = g_imgview.ww, wh = g_imgview.wh;
    draw_rect(wx - 2, wy - 24, ww + 4, wh + 26, 0xFF101010);
    ui_draw_hgrad_rect(wx, wy - 22, ww, 20, 0xFF2F3948, 0xFF252E3A);
    draw_text(wx + 8, wy - 20, "Image Viewer", COLOR_ACCENT, 0x00000000);
    int close_w = 18, close_h = 14;
    int close_x = wx + ww - close_w - 6;
    int close_y = wy - 20;
    ui_draw_hgrad_rect(close_x, close_y, close_w, close_h, 0xFF6E2B2B, 0xFF581F1F);
    draw_text(close_x + 5, close_y - 1, "X", 0xFFFBECEC, 0x00000000);
    draw_rect(wx, wy, ww, wh, 0xFF0F141C);

    if (g_imgview.img && g_imgview.img_w > 0 && g_imgview.img_h > 0)
    {
        int avail_w = ww - 16;
        int avail_h = wh - 16;
        if (avail_w > 0 && avail_h > 0)
        {
            int draw_w = avail_w;
            int draw_h = (int)(((int64_t)g_imgview.img_h * draw_w) / g_imgview.img_w);
            if (draw_h > avail_h)
            {
                draw_h = avail_h;
                draw_w = (int)(((int64_t)g_imgview.img_w * draw_h) / g_imgview.img_h);
            }
            int dx = wx + (ww - draw_w) / 2;
            int dy = wy + (wh - draw_h) / 2;
            if (dx < wx) dx = wx;
            if (dy < wy) dy = wy;
            for (int y = 0; y < draw_h; ++y)
            {
                int sy = (int)((int64_t)y * g_imgview.img_h / draw_h);
                for (int x = 0; x < draw_w; ++x)
                {
                    int sx = (int)((int64_t)x * g_imgview.img_w / draw_w);
                    uint32_t px = g_imgview.img[sy * g_imgview.img_w + sx];
                    fb_putpixel(dx + x, dy + y, px);
                }
            }
        }
    }
    else
    {
        draw_text(wx + 8, wy + 8, "No image loaded", 0xFFB0B6BF, 0xFF0C1016);
    }
}

static void imgview_close(void)
{
    g_imgview.open = 0;
    g_imgview.minimized = 0;
    g_imgview.maximized = 0;
    g_imgview.resizing = 0;
    imgview_free_image();
    desktop_mark_dirty();
    desktop_render();
}

static void imgview_toggle_maximize(void)
{
    if (!g_imgview.open || !g_imgview.can_maximize)
        return;
    if (!g_imgview.maximized)
    {
        g_imgview.prev_x = g_imgview.wx;
        g_imgview.prev_y = g_imgview.wy;
        g_imgview.prev_w = g_imgview.ww;
        g_imgview.prev_h = g_imgview.wh;
        int top = 28;
        int bottom = 32;
        g_imgview.wx = 6;
        g_imgview.wy = top + 4;
        g_imgview.ww = fb.width - 12;
        g_imgview.wh = fb.height - top - bottom - 8;
        g_imgview.maximized = 1;
    }
    else
    {
        g_imgview.wx = g_imgview.prev_x;
        g_imgview.wy = g_imgview.prev_y;
        g_imgview.ww = g_imgview.prev_w;
        g_imgview.wh = g_imgview.prev_h;
        g_imgview.maximized = 0;
    }
    desktop_mark_dirty();
}

static void imgview_open_path(const char *fullpath)
{
    if (!fullpath || !fullpath[0])
        return;
    if (!ensure_volume_mounted())
        return;

    uint32_t *img = NULL;
    int w = 0, h = 0;
    if (imgview_load_bmp(fullpath, &img, &w, &h) != 0)
    {
        serial_printf("[IMG] load failed: %s\n", fullpath);
        return;
    }

    imgview_free_image();
    g_imgview.img = img;
    g_imgview.img_w = w;
    g_imgview.img_h = h;
    strncpy(g_imgview.path, fullpath, sizeof(g_imgview.path) - 1);
    g_imgview.path[sizeof(g_imgview.path) - 1] = 0;

    g_imgview.open = 1;
    g_imgview.minimized = 0;
    g_imgview.maximized = 0;
    g_imgview.wx = 160;
    g_imgview.wy = 110;
    g_imgview.ww = (fb.width > 520) ? fb.width - 220 : 360;
    g_imgview.wh = (fb.height > 360) ? fb.height - 260 : 220;
    g_imgview.can_minimize = 1;
    g_imgview.can_maximize = 1;

    wm_set_front(g_win_imgview);
    desktop_mark_dirty();
}

static void imgview_taskbar_click(wm_entry_t *win, void *user)
{
    (void)win;
    (void)user;
    if (!g_imgview.open)
    {
        // Try system wallpaper first, else first BMP in wallpaper dir
        imgview_open_path(g_path_wallpaper);
        if (!g_imgview.open)
        {
            char wp_name[13] = "WALLPAPR.BMP";
            if (find_first_wallpaper_name(wp_name, sizeof(wp_name)) == 0)
            {
                char full[96];
                path_join(full, sizeof(full), "PARANOS/WALLPAPR", wp_name);
                imgview_open_path(full);
            }
        }
    }
    else
        g_imgview.minimized = !g_imgview.minimized;
    desktop_mark_dirty();
    desktop_render();
}

// Simple WAV player window
static void wavplay_open(void)
{
    if (g_wavplay.open && !g_wavplay.minimized)
        return;
    g_wavplay.open = 1;
    g_wavplay.minimized = 0;
    g_wavplay.wx = 200;
    g_wavplay.wy = 140;
    g_wavplay.ww = 360;
    g_wavplay.wh = 160;
    g_wavplay.dragging = 0;
    g_wavplay.can_minimize = 1;
    g_wavplay.can_maximize = 0;
    g_wavplay.path[0] = 0;
    g_wavplay.name[0] = 0;
    wm_set_front(g_win_wavplay);
    desktop_mark_dirty();
}

static void wavplay_close(void)
{
    g_wavplay.open = 0;
    g_wavplay.minimized = 0;
    g_wavplay.dragging = 0;
    desktop_mark_dirty();
    desktop_render();
}

static void wavplay_pick_cb(const char *fullpath, const char *name83, void *user)
{
    (void)user;
    if (!fullpath || !name83)
        return;
    strncpy(g_wavplay.path, fullpath, sizeof(g_wavplay.path) - 1);
    g_wavplay.path[sizeof(g_wavplay.path) - 1] = 0;
    strncpy(g_wavplay.name, name83, sizeof(g_wavplay.name) - 1);
    g_wavplay.name[sizeof(g_wavplay.name) - 1] = 0;
    g_wavplay.open = 1;
    g_wavplay.minimized = 0;
    wm_set_front(g_win_wavplay);
    desktop_mark_dirty();
}

static void wavplay_render(void)
{
    if (!g_wavplay.open || g_wavplay.minimized)
        return;
    int wx = g_wavplay.wx, wy = g_wavplay.wy;
    int ww = g_wavplay.ww, wh = g_wavplay.wh;

    draw_rect(wx - 2, wy - 24, ww + 4, wh + 26, 0xFF101010);
    ui_draw_hgrad_rect(wx, wy - 22, ww, 20, 0xFF2F3948, 0xFF252E3A);
    draw_text(wx + 8, wy - 20, "WAV Player", COLOR_ACCENT, 0x00000000);
    int close_w = 18, close_h = 14;
    int close_x = wx + ww - close_w - 6;
    int close_y = wy - 20;
    ui_draw_hgrad_rect(close_x, close_y, close_w, close_h, 0xFF6E2B2B, 0xFF581F1F);
    draw_text(close_x + 5, close_y - 1, "X", 0xFFFBECEC, 0x00000000);
    draw_rect(wx, wy, ww, wh, 0xFF0F141C);

    int btn_w = 96, btn_h = 22;
    int btn_y = wy + 12;
    ui_draw_hgrad_rect(wx + 12, btn_y, btn_w, btn_h, 0xFF33455D, 0xFF2A3546);
    draw_text(wx + 12 + 10, btn_y + 4, "Open WAV", COLOR_TEXT, 0x00000000);
    ui_draw_hgrad_rect(wx + 12 + btn_w + 10, btn_y, btn_w, btn_h, 0xFF33455D, 0xFF2A3546);
    draw_text(wx + 12 + btn_w + 10 + 14, btn_y + 4, "Play", COLOR_TEXT, 0x00000000);

    int y = btn_y + btn_h + 10;
    draw_text(wx + 12, y, "File:", COLOR_ACCENT, 0x00000000); y += psf_height() + 4;
    draw_text(wx + 12, y, g_wavplay.name[0] ? g_wavplay.name : "(none)", COLOR_TEXT, 0x00000000);
}

static void wavplay_taskbar_click(wm_entry_t *win, void *user)
{
    (void)win;
    (void)user;
    if (!g_wavplay.open)
        wavplay_open();
    else
        g_wavplay.minimized = !g_wavplay.minimized;
    desktop_mark_dirty();
    desktop_render();
}

// --- Top menu (global bar between branding and clock) ---
static int topmenu_active_window(void)
{
    int front = wm_get_front();
    return (front == g_win_notepad && g_notepad.open && !g_notepad.minimized);
}

static void topmenu_get_menu(int menu_idx,
                             const char ***labels,
                             const ctx_action_t **actions,
                             int *count)
{
    if (menu_idx == 0)
    {
        *labels = g_topmenu_file_labels;
        *actions = g_topmenu_file_actions;
        *count = (int)(sizeof(g_topmenu_file_labels) / sizeof(g_topmenu_file_labels[0]));
    }
    else
    {
        *labels = g_topmenu_window_labels;
        *actions = g_topmenu_window_actions;
        *count = (int)(sizeof(g_topmenu_window_labels) / sizeof(g_topmenu_window_labels[0]));
    }
}

static void topmenu_apply_action(ctx_action_t act)
{
    switch (act)
    {
    case CTX_ACT_NOTEPAD_NEW:
        notepad_new();
        break;
    case CTX_ACT_NOTEPAD_OPEN:
        file_picker_start("TXT", notepad_file_pick_cb, NULL);
        break;
    case CTX_ACT_NOTEPAD_SAVE:
        if (g_notepad.name[0])
            notepad_save_current(NULL);
        else
            start_name_prompt(PROMPT_SAVE_AS, g_notepad.name[0] ? g_notepad.name : "UNTITLED.TXT", "Save As:");
        break;
    case CTX_ACT_NOTEPAD_SAVE_AS:
        start_name_prompt(PROMPT_SAVE_AS, g_notepad.name[0] ? g_notepad.name : "UNTITLED.TXT", "Save As:");
        break;
    case CTX_ACT_NOTEPAD_CLOSE:
        notepad_close();
        break;
    case CTX_ACT_WIN_MINIMIZE:
    {
        int front = wm_get_front();
        if (front == g_win_notepad && g_notepad.can_minimize)
            g_notepad.minimized = 1;
        else if (front == g_win_file && g_filewin.can_minimize)
            g_filewin.minimized = 1;
        else if (front == g_win_taskmgr && g_taskmgr.can_minimize)
            g_taskmgr.minimized = 1;
        else if (front == g_win_display && g_display.can_minimize)
            g_display.minimized = 1;
        else if (front == g_win_terminal && g_terminal.can_minimize)
            g_terminal.minimized = 1;
        else if (front == g_win_imgview && g_imgview.can_minimize)
            g_imgview.minimized = 1;
        desktop_mark_dirty();
        break;
    }
    case CTX_ACT_WIN_TOGGLE_MAX:
    {
        int front = wm_get_front();
        if (front == g_win_notepad)
            notepad_toggle_maximize();
        else if (front == g_win_file)
            filewin_toggle_maximize();
        else if (front == g_win_taskmgr)
            taskmgr_toggle_maximize();
        else if (front == g_win_terminal)
            terminal_toggle_maximize();
        else if (front == g_win_imgview)
            imgview_toggle_maximize();
        desktop_mark_dirty();
        break;
    }
    default:
        break;
    }
}

static void topmenu_layout_titles(void)
{
    const char *titles[] = { "File", "Window" };
    int count = 2;
    int x = 96; // leave room for "ParanOS" branding at the left
    int pad = 14;
    for (int i = 0; i < count; ++i)
    {
        int w = (int)strlen(titles[i]) * psf_width() + 12;
        g_topmenu_titles[i].title = titles[i];
        g_topmenu_titles[i].x = x;
        g_topmenu_titles[i].w = w;
        x += w + pad;
    }
}

static void topmenu_draw(void)
{
    if (!topmenu_active_window())
    {
        g_topmenu_open = -1;
        g_topmenu_hover = -1;
        g_topmenu_item_hover = -1;
        return;
    }

    topmenu_layout_titles();

    int count = 2;
    int y = 6;
    int mx = mouse_get_x();
    int my = mouse_get_y();
    g_topmenu_hover = -1;
    for (int i = 0; i < count; ++i)
    {
        uint32_t bg = COLOR_SURFACE;
        if (my >= 0 && my < TOPBAR_H &&
            mx >= g_topmenu_titles[i].x && mx < g_topmenu_titles[i].x + g_topmenu_titles[i].w)
            g_topmenu_hover = i;
        if (g_topmenu_open == i || g_topmenu_hover == i)
            bg = 0xFFE3EBF8;
        draw_rect(g_topmenu_titles[i].x, 4, g_topmenu_titles[i].w, TOPBAR_H - 8, bg);
        draw_text(g_topmenu_titles[i].x + 6, y, g_topmenu_titles[i].title, COLOR_TEXT_DARK, bg);
    }

    if (g_topmenu_open >= 0)
    {
        const char **labels = NULL;
        const ctx_action_t *acts = NULL;
        int item_count = 0;
        topmenu_get_menu(g_topmenu_open, &labels, &acts, &item_count);

        int max_len = 0;
        for (int i = 0; i < item_count; ++i)
        {
            int len = (int)strlen(labels[i]);
            if (len > max_len)
                max_len = len;
        }
        int item_h = 20;
        int menu_w = max_len * psf_width() + 24;
        int menu_x = g_topmenu_titles[g_topmenu_open].x;
        int menu_y = TOPBAR_H;
        if (menu_x + menu_w > (int)fb.width)
            menu_x = (int)fb.width - menu_w - 6;
        draw_rect(menu_x, menu_y, menu_w, item_count * item_h + 8, COLOR_BORDER);
        draw_rect(menu_x + 1, menu_y + 1, menu_w - 2, item_count * item_h + 6, COLOR_SURFACE);

        int mx2 = mouse_get_x();
        int my2 = mouse_get_y();
        g_topmenu_item_hover = -1;
        for (int i = 0; i < item_count; ++i)
        {
            int iy = menu_y + 4 + i * item_h;
            uint32_t bg = COLOR_SURFACE;
            if (mx2 >= menu_x && mx2 < menu_x + menu_w && my2 >= iy && my2 < iy + item_h)
            {
                bg = 0xFFE8F0FB;
                g_topmenu_item_hover = i;
            }
            draw_rect(menu_x + 2, iy, menu_w - 4, item_h - 2, bg);
            draw_text(menu_x + 8, iy + 3, labels[i], COLOR_TEXT_DARK, bg);
        }
    }
}

static int topmenu_handle_click(int mx, int my)
{
    if (!topmenu_active_window())
    {
        g_topmenu_open = -1;
        g_topmenu_hover = -1;
        g_topmenu_item_hover = -1;
        return 0;
    }

    topmenu_layout_titles();

    // Drop-down selection first
    if (g_topmenu_open >= 0)
    {
        const char **labels = NULL;
        const ctx_action_t *acts = NULL;
        int item_count = 0;
        topmenu_get_menu(g_topmenu_open, &labels, &acts, &item_count);
        int max_len = 0;
        for (int i = 0; i < item_count; ++i)
            if ((int)strlen(labels[i]) > max_len)
                max_len = (int)strlen(labels[i]);
        int item_h = 20;
        int menu_w = max_len * psf_width() + 24;
        int menu_x = g_topmenu_titles[g_topmenu_open].x;
        int menu_y = TOPBAR_H;
        if (menu_x + menu_w > (int)fb.width)
            menu_x = (int)fb.width - menu_w - 6;

        if (mx >= menu_x && mx < menu_x + menu_w &&
            my >= menu_y && my < menu_y + item_count * item_h + 8)
        {
            int idx = (my - menu_y - 4) / item_h;
            if (idx >= 0 && idx < item_count)
            {
                topmenu_apply_action(acts[idx]);
                g_topmenu_open = -1;
                desktop_mark_dirty();
                desktop_render();
                return 1;
            }
        }
        else
        {
            // clicked outside open menu closes it
            g_topmenu_open = -1;
            desktop_mark_dirty();
            return 1;
        }
    }

    // Title hit
    if (my >= 0 && my < TOPBAR_H)
    {
        int count = 2;
        for (int i = 0; i < count; ++i)
        {
            int x = g_topmenu_titles[i].x;
            int w = g_topmenu_titles[i].w;
            if (mx >= x && mx < x + w)
            {
                g_topmenu_open = (g_topmenu_open == i) ? -1 : i;
                desktop_mark_dirty();
                return 1;
            }
        }
    }

    return 0;
}

// Taskbar quick launch rendering and clicks
static void taskbar_draw_shortcuts(int x, int y, int h, int *next_start_x)
{
    int bx = x + 6;
    int by = y + (h - QL_BTN) / 2;
    for (int i = 0; i < QL_COUNT; ++i)
    {
        uint32_t c = g_quick_launch[i].color;
        ui_fill_round_rect(bx, by, QL_BTN, QL_BTN, 8, COLOR_SURFACE);
        draw_rect(bx, by, QL_BTN, 1, COLOR_BORDER);
        draw_rect(bx, by + QL_BTN - 1, QL_BTN, 1, COLOR_BORDER);
        draw_rect(bx, by, 1, QL_BTN, COLOR_BORDER);
        draw_rect(bx + QL_BTN - 1, by, 1, QL_BTN, COLOR_BORDER);
        int icon_size = QL_BTN - 12;
        ui_fill_round_rect(bx + 6, by + 6, icon_size, icon_size, 6, c);
        bx += QL_BTN + QL_PAD;
    }
    if (next_start_x)
        *next_start_x = bx + 6;
}

static int taskbar_handle_shortcut_click(int mx, int my, int x, int y, int h)
{
    int bx = x + 6;
    int by = y + (h - QL_BTN) / 2;
    for (int i = 0; i < QL_COUNT; ++i)
    {
        if (mx >= bx && mx < bx + QL_BTN && my >= by && my < by + QL_BTN)
        {
            switch (g_quick_launch[i].id)
            {
            case QL_FILE:
                filewin_open();
                wm_set_front(g_win_file);
                break;
            case QL_TASKMGR:
                taskmgr_open();
                wm_set_front(g_win_taskmgr);
                break;
            case QL_TERMINAL:
                terminal_open();
                wm_set_front(g_win_terminal);
                break;
            case QL_WAVPLAY:
                wavplay_open();
                wm_set_front(g_win_wavplay);
                break;
            default:
                break;
            }
            desktop_mark_dirty();
            desktop_render();
            return 1;
        }
        bx += QL_BTN + QL_PAD;
    }
    return 0;
}

void kmain(void)
{
    __asm__ __volatile__("cli");
    //volatile uint16_t *vga = (uint16_t *)0xB8000;
    //vga[1] = 0x074D;
    //gdt_install_with_tss((uint32_t)&stack_top);
    init_fpu_sse();

    serial_init(COM1);

    enable_io_iopl3();
    enable_io_full();

    //kheap_init();
    
    
    serial_printf("\nSTEP >> GDT and heap initialized.");

    for (uint32_t i = 0; i < 1024; i++)
        page_table0[i] = (i * 0x1000) | 0x3;

    serial_printf("\nSTEP >> PMM/VMM init Starting...\n");
    serial_printf(" cr3=%p", (void *)(uintptr_t)read_cr3());
    vmm_init(); // Grab current CR3/pagetables before we start mapping
    serial_printf("\nSTEP >> vmm init OK.\n");
    kheap_init();  
    // Limine framebuffer 정보를 우선 사용
    memset(&g_bootinfo, 0, sizeof(g_bootinfo));
    limine_fill_bootinfo_from_fb();
    // Multiboot2 경로는 g_mbinfo_phys가 채워져 있을 때만 사용
    if (g_mbinfo_phys) {
        bootinfo_parse(g_mbinfo_phys);
    }

    // PMM/커널 힙 초기화는 Limine의 HHDM + ext_mem_alloc에 의존하고,
    // 여기서는 추가적인 저수준 매핑은 수행하지 않는다.
    serial_printf("\nSTEP >> VMM initialized successfully.\n");
    psf_init();

    serial_printf("\nSTEP >> VMM (no custom PMM) initialized successfully.\n");
    
    serial_printf("[dbg] before fb_map check\n");
    g_fb_ready = 0;
    if (g_bootinfo.fb_phys && g_bootinfo.fb_w && g_bootinfo.fb_h &&
        g_bootinfo.fb_pitch && (g_bootinfo.fb_bpp == 24 || g_bootinfo.fb_bpp == 32))
    {
        serial_printf("[dbg] calling fb_map phys=%x w=%u h=%u pitch=%u bpp=%u\n",
                      (uint32_t)g_bootinfo.fb_phys, g_bootinfo.fb_w, g_bootinfo.fb_h,
                      g_bootinfo.fb_pitch, g_bootinfo.fb_bpp);
        if (fb_map(g_bootinfo.fb_phys, g_bootinfo.fb_w, g_bootinfo.fb_h,
                   g_bootinfo.fb_pitch, g_bootinfo.fb_bpp) == 0)
        {
            serial_printf("[fb] w=%d h=%d pitch=%d bpp=%d\n",
                          fb.width, fb.height, fb.pitch, fb.bpp);

            serial_printf("[mb2-fb] w=%u h=%u bpp=%u\n",
                g_bootinfo.fb_w,
                g_bootinfo.fb_h,
                g_bootinfo.fb_bpp);
            // Softer desktop background with vertical gradient
            uint32_t top_bg = 0xFF20262E;
            uint32_t bot_bg = 0xFF0E1116;
            serial_printf("[dbg] before draw_hgrad_rect\n");
            ui_draw_hgrad_rect(0, 0, fb.width, fb.height, top_bg, bot_bg);
            serial_printf("[dbg] before draw_text\n");
        draw_text(20, 20, "Kernel starting...", 0xFFFFFFFF, top_bg);
        serial_printf("[dbg] before fb_flush\n");
        fb_flush();
        serial_printf("[dbg] after fb_flush\n");
        g_fb_ready = 1;
        cursor_use_default();
    }
        else
        {
            serial_printf("Framebuffer mapping failed.\n");
        }
    }
    serial_printf("[dbg] after fb_map block fb_ready=%d\n", g_fb_ready);

    serial_printf("[dbg] before idt_install_core\n");
    idt_install_core();
    serial_printf("[dbg] after idt_install_core\n");

    // Install C-side ISR handler table before registering any device IRQs.
    extern void isr_install(void);
    isr_install();
    serial_printf("[dbg] after isr_install\n");

    pic_remap();
    serial_printf("[dbg] after pic_remap\n");
    pit_init(100);
    serial_printf("[dbg] after pit_init\n");
    keyboard_init();
    serial_printf("[dbg] after keyboard_init\n");
    probe_back_tail();
    mouse_init(fb.width, fb.height);
    serial_printf("[dbg] after mouse_init\n");
    // Mouse IRQ handler is registered via isr_register_handler inside mouse_init.
    // Unmask cascade + mouse IRQ (IRQ2 = PIC2 cascade, IRQ12 = PS/2 mouse).
    pic_clear_mask(2);
    pic_clear_mask(12);
    desktop_config_frame_rate();

    serial_write(COM1, "[serial] kernel up: IDT/PIC/PIT/KBD ready\r\n");

    pic_clear_mask(0);
    pic_clear_mask(1);

    serial_printf("[dbg] before sti\n");
    __asm__ __volatile__("sti"); // Enable interrupts
    serial_printf("[dbg] after sti\n");

    //write_center("-- All Drivers Initialized Successfully --", 0x0A, VGA_ROWS - 2);

    extern void tasking_init(void);
    extern void start_scheduler(void);
    serial_printf("DEBUG: before tasking_init\n");
    tasking_init();
    serial_printf("DEBUG: after tasking_init\n");
    start_scheduler();
    serial_printf("DEBUG: after start_scheduler\n");
    syscall_init();
    serial_printf("[Kernel] syscall ready.\n");
    serial_printf("[user] entry=%x stack=%x\n", U_ENTRY, U_STACK_TOP);
    if (map_user_minimal() == 0)
    {
        load_user_stub();
        serial_printf("[user] minimal user mapping ready.\n");
    }
    else
    {
        serial_printf("[user] failed to prepare user mapping.\n");
    }

    // Init simple window manager (for GUI taskbar)
    wm_init();
    desktop_init();
    ac97_init();
    // Register Text Editor window for taskbar listing
    g_win_notepad = wm_register_window("Text Editor", 0xFF5E8C31,
                                       &g_notepad.open,
                                       &g_notepad.minimized,
                                       notepad_taskbar_click,
                                       NULL);
    // Register File Explorer window
    g_win_file = wm_register_window("Explorer", 0xFF2F6FAB,
                                    &g_filewin.open,
                                    &g_filewin.minimized,
                                    filewin_taskbar_click,
                                    NULL);
    // Register System Monitor window
    g_win_taskmgr = wm_register_window("SysMon", 0xFFAA8844,
                                       &g_taskmgr.open,
                                       &g_taskmgr.minimized,
                                       taskmgr_taskbar_click,
                                       NULL);
    // Register Display Settings window
    g_win_display = wm_register_window("Display", 0xFF4488CC,
                                       &g_display.open,
                                       &g_display.minimized,
                                       display_taskbar_click,
                                       NULL);
    // Register Terminal window
    g_win_terminal = wm_register_window("Terminal", 0xFF8844AA,
                                        &g_terminal.open,
                                        &g_terminal.minimized,
                                        terminal_taskbar_click,
                                        NULL);
    // Register Image Viewer window
    g_win_imgview = wm_register_window("ImageView", 0xFF44AA88,
                                       &g_imgview.open,
                                       &g_imgview.minimized,
                                       imgview_taskbar_click,
                                       NULL);
    // Register WAV Player window
    g_win_wavplay = wm_register_window("WAV Player", 0xFF8899DD,
                                       &g_wavplay.open,
                                       &g_wavplay.minimized,
                                       wavplay_taskbar_click,
                                       NULL);
    wm_set_front(g_win_taskmgr);

    if (!g_fb_ready)
    {
        //write_center("-- GUI (FB not available) --", 0x0A, VGA_ROWS - 2);
    }
    rtc_time_t t;
    rtc_read_time(&t);
    serial_printf("[RTC] %02d:%02d:%02d\n", t.hh, t.mm, t.ss);
    // jump_to_user_real(U_ENTRY, U_STACK_TOP);

    // --- Disk / FS probe ---
    ata_init();
    ata_identify_t id;
    int idr = ata_identify(&id);
    if (idr != 0 || !id.present)
    {
        serial_printf("[ATA] no primary master (idr=%d)\n", idr);
    }
    else
    {
        uint8_t sec0[512];
        if (ata_read28(0, 1, sec0) != 0)
        {
            serial_printf("[ATA] read LBA0 failed\n");
        }
        else
        {
            mbr_t m;
            int mbr_r = mbr_parse(sec0, &m);
            uint32_t mount_lba = 0;
            int found = 0;
            if (mbr_r == 0 && m.valid)
            {
                for (int i = 0; i < 4; ++i)
                {
                    if (m.parts[i].type != 0 && m.parts[i].sectors)
                    {
                        serial_printf("[MBR] part%d type=0x%02x lba=%u size=%u\n",
                                      i, m.parts[i].type, m.parts[i].lba_first, m.parts[i].sectors);
                    }
                }
                for (int i = 0; i < 4; ++i)
                {
                    if (m.parts[i].type == 0x0B || m.parts[i].type == 0x0C)
                    {
                        mount_lba = m.parts[i].lba_first;
                        found = 1;
                        break;
                    }
                }
            }
            else
            {
                serial_printf("[MBR] invalid or missing (mbr_r=%d), trying superfloppy at LBA0\n", mbr_r);
            }

            if (!found)
                mount_lba = 0; // superfloppy fallback

            int mnt = fat32_mount(&g_vol, ata_read28, mount_lba);
            if (mnt == 0)
            {
                g_vol_mounted = 1;
                serial_printf("[FAT32] mounted at LBA %u\n", mount_lba);
                fat32_ensure_dir_path(&g_vol, ata_read28, ata_write28, g_path_base);
                ensure_user_dirs_on_disk(g_logged_in_user);
                desktop_refresh_from_path();
                if (desktop_load_wallpaper_path(&g_vol, ata_read28, g_path_wallpaper) == 0)
                {
                    serial_printf("[WALLPAPER] loaded system wallpaper\n");
                }
                else
                {
                    serial_printf("[WALLPAPER] system wallpaper missing; trying root fallback\n");
                    char wp_name[13] = "WALLPAPR.BMP";
                    if (find_first_wallpaper_name(wp_name, sizeof(wp_name)) == 0 &&
                        desktop_load_wallpaper(&g_vol, ata_read28, wp_name) == 0)
                {
                    serial_printf("[WALLPAPER] loaded fallback %s\n", wp_name);
                }
                else
                {
                    serial_printf("[WALLPAPER] no wallpaper or failed to load\n");
                }
            }
                cursor_load_from_disk();
                cursor_use_default();
            }
            else
            {
                serial_printf("[FAT32] mount failed at LBA %u (err=%d)\n", mount_lba, mnt);
            }
        }
    }
    ensure_user_store();
    g_boot_anim_start = jiffies;

    int last_mouse_x = -1;
    int last_mouse_y = -1;

    for (;;)
    {
        static uint64_t last_rtc_update = 0;

        if (jiffies - last_rtc_update >= 100)
        {
            last_rtc_update = jiffies;
            // serial_printf("[clock] tick=%llu\n", jiffies);
            rtc_time_t now;
            rtc_read_time(&now);
            char buf[9];
            rtc_format(buf, &now);
            (void)buf;
        }

        // Boot animation before login
        if (g_boot_anim)
        {
            boot_anim_render();
            if (jiffies - g_boot_anim_start > 300)
            {
                g_boot_anim = 0;
            }
            __asm__ volatile("sti; hlt");
            continue;
        }

        // Keyboard input dispatch
        uint8_t sc;
        int has_sc = kbd_get_scancode(&sc);
        int release = 0;
        uint8_t scode = sc & 0x7F;
        if (has_sc && (sc & 0x80))
            release = 1;
        if (has_sc && release)
        {
            if (scode == 0x38)
                g_alt_down = 0;
            if (scode == 0x5B)
                g_win_down = 0;
            if (scode == 0x2A || scode == 0x36)
                g_shift_down = (g_shift_down > 0) ? g_shift_down - 1 : 0;
            has_sc = 0;
        }
        if (has_sc && !release)
        {
            if (scode == 0x38)
                g_alt_down = 1;
            if (scode == 0x5B)
            {
                g_win_down = 1;
                launch_toggle(!g_launch_visible);
                desktop_render();
                has_sc = 0;
            }
            if (scode == 0x2A || scode == 0x36)
                g_shift_down++;
            if (scode == 0x3A) // Caps Lock toggle
                g_caps_on ^= 1;
            if (scode == 0x0F && g_alt_down) // Alt+Tab
            {
                wm_cycle_next();
                desktop_mark_dirty();
                desktop_render();
                has_sc = 0;
            }
            if (scode == 0x53) // Delete
            {
                if (g_filewin.open && wm_get_front() == g_win_file)
                    filewin_delete_selection();
                else
                    desktop_delete_selection();
                has_sc = 0;
            }
            if (scode == 0x48 || scode == 0x50 || scode == 0x4B || scode == 0x4D) // Arrow keys
            {
                int handled = 0;
                int front = wm_get_front();
                if (front == g_win_file && g_filewin.open && !g_filewin.minimized)
                {
                    if (scode == 0x48 || scode == 0x4B)
                        filewin_move_selection(-1);
                    else
                        filewin_move_selection(+1);
                    handled = 1;
                }
                else
                {
                    if (scode == 0x48 || scode == 0x4B)
                        desktop_move_selection(-1);
                    else
                        desktop_move_selection(+1);
                    handled = 1;
                }
                if (handled)
                    has_sc = 0;
            }
        }

        // Mouse click handling (desktop / windows / popup)
        static int prev_btn = 0;
        int btn = mouse_get_buttons();

        if (g_login_active)
        {
            if (!g_fb_ready)
            {
                prev_btn = btn;
                __asm__ volatile("sti; hlt");
                continue;
            }

            login_layout_t lo;
            login_layout_compute(&lo);

            int mx = mouse_get_x();
            int my = mouse_get_y();

            int hover = -1;
            for (int i = 0; i < g_user_count; ++i)
            {
                int cy = lo.list_y + i * (lo.card_h + lo.card_gap);
                if (mx >= lo.card_x && mx < lo.card_x + lo.card_w &&
                    my >= cy && my < cy + lo.card_h)
                {
                    hover = i;
                    break;
                }
            }
            if (hover != g_login_hover)
            {
                g_login_hover = hover;
                login_mark_dirty();
            }

            if ((btn & 1) && !(prev_btn & 1))
            {
                if (hover >= 0)
                {
                    g_login_selected = hover;
                    g_login_pw_focus = 1;
                    login_mark_dirty();
                }

                if (mx >= lo.pw_x && mx < lo.pw_x + lo.pw_w &&
                    my >= lo.pw_y && my < lo.pw_y + lo.pw_h)
                {
                    g_login_pw_focus = 1;
                    login_mark_dirty();
                }
                else if (mx >= lo.btn_x && mx < lo.btn_x + lo.btn_w &&
                         my >= lo.btn_y && my < lo.btn_y + lo.btn_h)
                {
                    login_attempt();
                }
                else
                {
                    g_login_pw_focus = 0;
                }
            }
            prev_btn = btn;

            if (has_sc && g_login_pw_focus)
            {
                if (sc == 0x1C) // Enter
                {
                    login_attempt();
                    has_sc = 0;
                }
                else if (sc == 0x0E) // Backspace
                {
                    if (g_login_pw_len > 0)
                    {
                        g_login_pw[--g_login_pw_len] = 0;
                        login_mark_dirty();
                    }
                    has_sc = 0;
                }
                else if (sc < 128)
                {
                    char c = key_to_char(sc);
                    if (c && g_login_pw_len < (int)sizeof(g_login_pw) - 1)
                    {
                        g_login_pw[g_login_pw_len++] = c;
                        g_login_pw[g_login_pw_len] = 0;
                        login_mark_dirty();
                    }
                    has_sc = 0;
                }
            }

            if (g_fb_ready)
            {
                static uint64_t login_last_frame = 0;
                uint64_t frame_ticks = desktop_frame_ticks_value();
                uint64_t now = jiffies;
                if ((now - login_last_frame >= frame_ticks) || g_login_dirty)
                {
                    login_render();
                    login_last_frame = now;
                }
            }

            __asm__ volatile("sti; hlt");
            continue;
        }

        if (name_prompt_active && btn == 0)
            name_prompt_ready_click = 1;
        if ((btn & 1) && !(prev_btn & 1))
        {
            int mx = mouse_get_x(), my = mouse_get_y();

            // Context menu selection
            if (ctx_menu_visible)
            {
                int x = ctx_menu_x, y = ctx_menu_y;
                if (x + ctx_menu_w > (int)fb.width)  x = fb.width - ctx_menu_w;
                if (y + ctx_menu_h > (int)fb.height) y = fb.height - ctx_menu_h;
                if (point_in_rect(mx, my, x, y, ctx_menu_w, ctx_menu_h))
                {
                    int idx = (my - y - 4) / ctx_menu_item_h;
                    if (idx >= 0 && idx < g_ctx_count)
                        ctx_menu_handle_action(g_ctx_items[idx].act);
                }
                else
                {
                    ctx_menu_hide();
                }
                goto after_left_click;
            }

            // Commit name prompt on click anywhere (after initial arm)
            if (name_prompt_active && name_prompt_ready_click)
            {
                name_prompt_commit();
                goto after_left_click;
            }

            // Context menu selection
            if (ctx_menu_visible)
            {
                int x = ctx_menu_x, y = ctx_menu_y;
                if (x + ctx_menu_w > (int)fb.width)  x = fb.width - ctx_menu_w;
                if (y + ctx_menu_h > (int)fb.height) y = fb.height - ctx_menu_h;
                if (mx >= x && mx < x + ctx_menu_w && my >= y && my < y + ctx_menu_h)
                {
                    ctx_menu_visible = 0;
                    start_name_prompt(PROMPT_CREATE_TXT, "NEWFILE.TXT", "New TXT filename:");
                    goto after_left_click;
                }
                else
                {
                    ctx_menu_visible = 0;
                }
            }

            // Taskbar click: route to window manager before other UI
            int taskbar_h = 32;
            int taskbar_y = fb.height - taskbar_h;
            if (my >= taskbar_y && my < fb.height)
            {
                if (mx < 90)
                {
                    launch_toggle(!g_launch_visible);
                    goto after_left_click;
                }
                else
                {
                    g_launch_visible = 0;
                }
                if (taskbar_handle_shortcut_click(mx, my, 80, taskbar_y, taskbar_h))
                    goto after_left_click;
                wm_handle_taskbar_click(mx, my, taskbar_y, taskbar_h, 80);
                goto after_left_click;
            }

            if (g_launch_visible)
            {
                int lx = g_launch_x;
                int ly = g_launch_y;
                int lw = 220;
                int lh = g_launch_item_h * g_launch_count + 12;
                if (mx >= lx && mx < lx + lw && my >= ly && my < ly + lh)
                    {
                        int idx = (my - ly - 6) / g_launch_item_h;
                        if (idx >= 0 && idx < g_launch_count)
                            launch_activate(idx);
                    }
                else
                {
                    g_launch_visible = 0;
                    desktop_mark_dirty();
                }
                goto after_left_click;
            }

            // Top menu bar clicks (between branding and clock)
            if (topmenu_handle_click(mx, my))
                goto after_left_click;

            // Window focus: bring clicked window to front
            int front_id = wm_get_front();
            int clicked = -1;

            // Check front-most window first
            if (front_id == g_win_notepad && g_notepad.open && !g_notepad.minimized)
            {
                int wx = g_notepad.wx, wy = g_notepad.wy, ww = g_notepad.ww, wh = g_notepad.wh;
                if (mx >= wx - 2 && mx < wx - 2 + ww + 4 && my >= wy - 24 && my < wy - 24 + wh + 26)
                    clicked = g_win_notepad;
            }
            else if (front_id == g_win_file && g_filewin.open && !g_filewin.minimized)
            {
                int wx = g_filewin.wx, wy = g_filewin.wy, ww = g_filewin.ww, wh = g_filewin.wh;
                if (mx >= wx - 2 && mx < wx - 2 + ww + 4 && my >= wy - 24 && my < wy - 24 + wh + 26)
                    clicked = g_win_file;
            }
            else if (front_id == g_win_taskmgr && g_taskmgr.open && !g_taskmgr.minimized)
            {
                int wx = g_taskmgr.wx, wy = g_taskmgr.wy, ww = g_taskmgr.ww, wh = g_taskmgr.wh;
                if (mx >= wx - 2 && mx < wx - 2 + ww + 4 && my >= wy - 24 && my < wy - 24 + wh + 26)
                    clicked = g_win_taskmgr;
            }
            else if (front_id == g_win_display && g_display.open && !g_display.minimized)
            {
                int wx = g_display.wx, wy = g_display.wy, ww = g_display.ww, wh = g_display.wh;
                if (mx >= wx - 2 && mx < wx - 2 + ww + 4 && my >= wy - 24 && my < wy - 24 + wh + 26)
                    clicked = g_win_display;
            }
            else if (front_id == g_win_wavplay && g_wavplay.open && !g_wavplay.minimized)
            {
                int wx = g_wavplay.wx, wy = g_wavplay.wy, ww = g_wavplay.ww, wh = g_wavplay.wh;
                if (mx >= wx - 2 && mx < wx - 2 + ww + 4 && my >= wy - 24 && my < wy - 24 + wh + 26)
                    clicked = g_win_wavplay;
            }
            else if (front_id == g_win_terminal && g_terminal.open && !g_terminal.minimized)
            {
                int wx = g_terminal.wx, wy = g_terminal.wy, ww = g_terminal.ww, wh = g_terminal.wh;
                if (mx >= wx - 2 && mx < wx - 2 + ww + 4 && my >= wy - 24 && my < wy - 24 + wh + 26)
                    clicked = g_win_terminal;
            }
            else if (front_id == g_win_imgview && g_imgview.open && !g_imgview.minimized)
            {
                int wx = g_imgview.wx, wy = g_imgview.wy, ww = g_imgview.ww, wh = g_imgview.wh;
                if (mx >= wx - 2 && mx < wx - 2 + ww + 4 && my >= wy - 24 && my < wy - 24 + wh + 26)
                    clicked = g_win_imgview;
            }

            if (clicked == -1)
            {
                if (g_notepad.open && !g_notepad.minimized && front_id != g_win_notepad)
                {
                    int wx = g_notepad.wx, wy = g_notepad.wy, ww = g_notepad.ww, wh = g_notepad.wh;
                    if (mx >= wx - 2 && mx < wx - 2 + ww + 4 && my >= wy - 24 && my < wy - 24 + wh + 26)
                        clicked = g_win_notepad;
                }
                if (clicked == -1 && g_filewin.open && !g_filewin.minimized && front_id != g_win_file)
                {
                    int wx = g_filewin.wx, wy = g_filewin.wy, ww = g_filewin.ww, wh = g_filewin.wh;
                    if (mx >= wx - 2 && mx < wx - 2 + ww + 4 && my >= wy - 24 && my < wy - 24 + wh + 26)
                        clicked = g_win_file;
                }
                if (clicked == -1 && g_taskmgr.open && !g_taskmgr.minimized && front_id != g_win_taskmgr)
                {
                    int wx = g_taskmgr.wx, wy = g_taskmgr.wy, ww = g_taskmgr.ww, wh = g_taskmgr.wh;
                    if (mx >= wx - 2 && mx < wx - 2 + ww + 4 && my >= wy - 24 && my < wy - 24 + wh + 26)
                        clicked = g_win_taskmgr;
                }
                if (clicked == -1 && g_display.open && !g_display.minimized && front_id != g_win_display)
                {
                    int wx = g_display.wx, wy = g_display.wy, ww = g_display.ww, wh = g_display.wh;
                    if (mx >= wx - 2 && mx < wx - 2 + ww + 4 && my >= wy - 24 && my < wy - 24 + wh + 26)
                        clicked = g_win_display;
                }
                if (clicked == -1 && g_terminal.open && !g_terminal.minimized && front_id != g_win_terminal)
                {
                    int wx = g_terminal.wx, wy = g_terminal.wy, ww = g_terminal.ww, wh = g_terminal.wh;
                    if (mx >= wx - 2 && mx < wx - 2 + ww + 4 && my >= wy - 24 && my < wy - 24 + wh + 26)
                        clicked = g_win_terminal;
                }
                if (clicked == -1 && g_wavplay.open && !g_wavplay.minimized && front_id != g_win_wavplay)
                {
                    int wx = g_wavplay.wx, wy = g_wavplay.wy, ww = g_wavplay.ww, wh = g_wavplay.wh;
                    if (mx >= wx - 2 && mx < wx - 2 + ww + 4 && my >= wy - 24 && my < wy - 24 + wh + 26)
                        clicked = g_win_wavplay;
                }
                if (clicked == -1 && g_imgview.open && !g_imgview.minimized && front_id != g_win_imgview)
                {
                    int wx = g_imgview.wx, wy = g_imgview.wy, ww = g_imgview.ww, wh = g_imgview.wh;
                    if (mx >= wx - 2 && mx < wx - 2 + ww + 4 && my >= wy - 24 && my < wy - 24 + wh + 26)
                        clicked = g_win_imgview;
                }
            }

            if (clicked != -1 && clicked != front_id)
            {
                wm_set_front(clicked);
                desktop_mark_dirty();
                desktop_render();
            }

            // Hide right-click popup if visible and clicked outside
            if (info_visible)
            {
                if (!(mx >= info_x && mx < info_x + info_w && my >= info_y && my < info_y + info_h))
                {
                    info_visible = 0;
                }
            }

            // Titlebar button clicks (Notepad)
            if (g_notepad.open && !g_notepad.minimized && wm_get_front() == g_win_notepad)
            {
                int menu_hit = notepad_menu_hit(mx, my);
                if (menu_hit == 0)
                {
                    notepad_new();
                    goto after_left_click;
                }
                else if (menu_hit == 1)
                {
                    file_picker_start("TXT", notepad_file_pick_cb, NULL);
                    goto after_left_click;
                }
                else if (menu_hit == 2)
                {
                    if (g_notepad.name[0])
                        notepad_save_current(NULL);
                    else
                        start_name_prompt(PROMPT_SAVE_AS, g_notepad.name[0] ? g_notepad.name : "UNTITLED.TXT", "Save As:");
                    goto after_left_click;
                }

                int bx = g_notepad.wx + g_notepad.ww - 18 - 18 - 18 - 6;
                int by = g_notepad.wy - 20;
                int bw = 18;
                int bh = 14;
                // minimize
                if (mx >= bx && mx < bx + bw && my >= by && my < by + bh)
                {
                    g_notepad.minimized = 1;
                    goto after_left_click;
                }
                // maximize toggle
                if (mx >= bx + bw + 2 && mx < bx + 2 * bw + 2 && my >= by && my < by + bh)
                {
                    notepad_toggle_maximize();
                    goto after_left_click;
                }
                // close
                if (mx >= bx + 2 * (bw + 2) && mx < bx + 3 * (bw + 2) && my >= by && my < by + bh)
                {
                    notepad_close();
                    goto after_left_click;
                }
            }

            // Titlebar button click (File Explorer close)
            if (g_filewin.open && !g_filewin.minimized && wm_get_front() == g_win_file)
            {
                int bw = 18, bh = 14;
                int bx = g_filewin.wx + g_filewin.ww - bw - 6;
                int by = g_filewin.wy - 20;
                if (mx >= bx && mx < bx + bw && my >= by && my < by + bh)
                {
                    filewin_close();
                    goto after_left_click;
                }
            }

            // Titlebar button click (System Monitor close)
            if (g_taskmgr.open && !g_taskmgr.minimized && wm_get_front() == g_win_taskmgr)
            {
                int bw = 18, bh = 14;
                int bx = g_taskmgr.wx + g_taskmgr.ww - bw - 6;
                int by = g_taskmgr.wy - 20;
                if (mx >= bx && mx < bx + bw && my >= by && my < by + bh)
                {
                    taskmgr_close();
                    goto after_left_click;
                }
            }

            // Titlebar button click (Display Settings close)
            if (g_display.open && !g_display.minimized && wm_get_front() == g_win_display)
            {
                int bw = 18, bh = 14;
                int bx = g_display.wx + g_display.ww - bw - 6;
                int by = g_display.wy - 20;
                if (mx >= bx && mx < bx + bw && my >= by && my < by + bh)
                {
                    display_close();
                    goto after_left_click;
                }
            }

            // Titlebar button click (Terminal close)
            if (g_terminal.open && !g_terminal.minimized && wm_get_front() == g_win_terminal)
            {
                int bw = 18, bh = 14;
                int bx = g_terminal.wx + g_terminal.ww - bw - 6;
                int by = g_terminal.wy - 20;
                if (mx >= bx && mx < bx + bw && my >= by && my < by + bh)
                {
                    terminal_close();
                    goto after_left_click;
                }
            }
            // Titlebar button click (Image Viewer close)
            if (g_imgview.open && !g_imgview.minimized && wm_get_front() == g_win_imgview)
            {
                int bw = 18, bh = 14;
                int bx = g_imgview.wx + g_imgview.ww - bw - 6;
                int by = g_imgview.wy - 20;
                if (mx >= bx && mx < bx + bw && my >= by && my < by + bh)
                {
                    imgview_close();
                    goto after_left_click;
                }
            }
            // Titlebar button click (WAV Player close)
            if (g_wavplay.open && !g_wavplay.minimized && wm_get_front() == g_win_wavplay)
            {
                int bw = 18, bh = 14;
                int bx = g_wavplay.wx + g_wavplay.ww - bw - 6;
                int by = g_wavplay.wy - 20;
                if (mx >= bx && mx < bx + bw && my >= by && my < by + bh)
                {
                    wavplay_close();
                    goto after_left_click;
                }
            }

            // File Explorer interactions (path bar, sidebar, list)
            if (g_filewin.open && !g_filewin.minimized && wm_get_front() == g_win_file)
            {
                int header_h = FILEWIN_HEADER_H;
                int sidebar_w = FILEWIN_SIDEBAR_W;
                int row_h = psf_height() + 6;
                int wx = g_filewin.wx, wy = g_filewin.wy;
                int ww = g_filewin.ww, wh = g_filewin.wh;

                // Path bar focus
                if (my >= wy && my < wy + header_h)
                {
                    if (mx >= wx + 8 && mx < wx + ww - 8)
                    {
                        g_filewin.path_focus = 1;
                        desktop_mark_dirty();
                        goto after_left_click;
                    }
                }

                // Sidebar hits
                int sb_x = wx;
                int sb_y = wy + header_h;
                int sy = sb_y + 6 + row_h; // first item (Desktop)
                const char *new_path = NULL;
                if (mx >= sb_x + 8 && mx < sb_x + sidebar_w - 8 && my >= sy && my < sy + row_h)
                {
                    new_path = g_desktop_dir;
                }
                sy += row_h;
                if (!new_path && mx >= sb_x + 8 && mx < sb_x + sidebar_w - 8 && my >= sy && my < sy + row_h)
                {
                    new_path = g_path_wall_dir;
                }
                sy += row_h * 2; // skip blank row and "This PC"
                sy += row_h;     // ATA0 row
                if (!new_path && mx >= sb_x + 8 && mx < sb_x + sidebar_w - 8 && my >= sy && my < sy + row_h)
                {
                    new_path = g_path_base;
                }
                if (new_path)
                {
                    g_filewin.path_focus = 0;
                    filewin_set_path(new_path);
                    goto after_left_click;
                }

                // List hit
                int list_x = sb_x + sidebar_w + 6;
                int list_y = wy + header_h + 4;
                int list_w = ww - sidebar_w - 12;
                int list_h = wh - header_h - 8;
                if (mx >= list_x && mx < list_x + list_w && my >= list_y && my < list_y + list_h)
                {
                    int cell_w = 96;
                    int cell_h = 80;
                    int cols = (list_w / cell_w) > 0 ? (list_w / cell_w) : 1;
                    int rel_x = mx - list_x;
                    int rel_y = my - list_y;
                    int col = rel_x / cell_w;
                    int row = rel_y / cell_h;
                    int idx = row * cols + col;
                    if (idx >= 0 && idx < g_file_item_count)
                    {
                        static uint64_t file_last_click = 0;
                        static int file_last_idx = -1;
                        uint64_t now = jiffies;
                        int double_click = (idx == file_last_idx) && (now - file_last_click <= 50);
                        file_last_click = now;
                        file_last_idx = idx;

                        g_filewin.selection = idx;
                        g_filewin.path_focus = 0;
                        desktop_mark_dirty();

                        if (double_click)
                        {
                            const desktop_item_t *it = &g_file_items[idx];
                            if (it->attr & 0x10)
                            {
                                char new_path[96];
                                path_join(new_path, sizeof(new_path), g_filewin.path, it->name);
                                filewin_set_path(new_path);
                            }
                            else
                            {
                                if (g_file_picker.active)
                                {
                                    char full[128];
                                    path_join(full, sizeof(full), g_filewin.path, it->name);
                                    if (g_file_picker.ext[0] == 0 || str_ieq_ext(it->name, g_file_picker.ext))
                                        file_picker_handle_selection(full, it->name);
                                }
                                else if (str_ieq_ext(it->name, "TXT"))
                                {
                                    notepad_open(it->name);
                                }
                                else if (str_ieq_ext(it->name, "BMP"))
                                {
                                    char full[128];
                                    path_join(full, sizeof(full), g_filewin.path, it->name);
                                    imgview_open_path(full);
                                }
                            }
                        }
                    }
                    goto after_left_click;
                }
            }

            // WAV Player interactions
            if (g_wavplay.open && !g_wavplay.minimized && wm_get_front() == g_win_wavplay)
            {
                int btn_w = 96, btn_h = 22;
                int btn_y = g_wavplay.wy + 12;
                int open_x = g_wavplay.wx + 12;
                int play_x = g_wavplay.wx + 12 + btn_w + 10;
                if (mx >= open_x && mx < open_x + btn_w && my >= btn_y && my < btn_y + btn_h)
                {
                    file_picker_start("WAV", wavplay_pick_cb, NULL);
                    goto after_left_click;
                }
                if (mx >= play_x && mx < play_x + btn_w && my >= btn_y && my < btn_y + btn_h)
                {
                    if (g_wavplay.path[0])
                        sound_play_wav_path(g_wavplay.path);
                    goto after_left_click;
                }
            }

        if (g_vol_mounted)
        {
            int hit = desktop_hit_test(mouse_get_x(), mouse_get_y());
            if (hit >= 0)
            {
                // Selection / Double-click
                desktop_set_selection(hit);
                const desktop_item_t *it = desktop_get_item(hit);
                if (it && desktop_is_double_click(hit))
                {
                    if (it->attr & 0x10)
                    {
                        serial_printf("[OPEN] folder not supported yet: %s\n", it->name);
                    }
                    else
                    {
                        if (str_ieq_ext(it->name, "TXT"))
                        {
                            notepad_open(it->name);
                        }
                        else if (str_ieq_ext(it->name, "BMP"))
                        {
                            char fullpath[96];
                            desktop_path_for_name(fullpath, sizeof(fullpath), it->name);
                            imgview_open_path(fullpath);
                        }
                        else
                        {
                            serial_printf("[OPEN] no app for: %s\n", it->name);
                        }
                    }
                }
            }
        }
        }
        // Right click: open context menu
        if ((btn & 2) && !(prev_btn & 2))
        {
            int mx = mouse_get_x(), my = mouse_get_y();
            ctx_target_t target = CTX_DESKTOP;
            // Prefer window under cursor
            if (g_notepad.open && !g_notepad.minimized)
            {
                int wx = g_notepad.wx, wy = g_notepad.wy;
                int ww = g_notepad.ww, wh = g_notepad.wh;
                if (point_in_rect(mx, my, wx - 2, wy - 24, ww + 4, wh + 26))
                    target = CTX_NOTEPAD;
            }
            if (g_filewin.open && !g_filewin.minimized)
            {
                int wx = g_filewin.wx, wy = g_filewin.wy;
                int ww = g_filewin.ww, wh = g_filewin.wh;
                if (point_in_rect(mx, my, wx - 2, wy - 24, ww + 4, wh + 26))
                    target = CTX_FILEWIN;
            }
            ctx_menu_show(target, mx, my);
            info_visible = 0;
        }
        if (!(btn & 2) && (prev_btn & 2))
        {
            // allow name prompt clicks after button release
            name_prompt_ready_click = 1;
        }
    after_left_click:
        prev_btn = btn;

        // Name prompt input
        if (name_prompt_active && has_sc)
        {
            char c = 0;
            if (sc == 0x1C)
            { // Enter
                create_txt_file_from_prompt();
                has_sc = 0;
            }
            else if (sc == 0x0E)
            { // Backspace
                if (name_prompt_len > 0)
                {
                    name_prompt_len--;
                    name_prompt_buf[name_prompt_len] = 0;
                }
                desktop_mark_dirty();
                has_sc = 0;
            }
            else if (sc == 0x01)
            { // ESC cancels
                name_prompt_active = 0;
                desktop_mark_dirty();
                has_sc = 0;
            }
            else if (sc < 128)
            {
                c = key_to_char(sc);
                if (c && name_prompt_len < (int)sizeof(name_prompt_buf) - 1)
                {
                    name_prompt_buf[name_prompt_len++] = c;
                    name_prompt_buf[name_prompt_len] = 0;
                    desktop_mark_dirty();
                }
                has_sc = 0;
            }
        }

        // File Explorer path input (focused)
        if (g_filewin.open && wm_get_front() == g_win_file && g_filewin.path_focus && has_sc)
        {
            if (sc == 0x1C) // Enter
            {
                filewin_set_path(g_filewin.path);
                has_sc = 0;
            }
            else if (sc == 0x0E) // Backspace
            {
                if (g_filewin.path_len > 0)
                {
                    g_filewin.path[--g_filewin.path_len] = 0;
                    desktop_mark_dirty();
                }
                has_sc = 0;
            }
            else if (sc == 0x01) // ESC
            {
                g_filewin.path_focus = 0;
                desktop_mark_dirty();
                has_sc = 0;
            }
            else if (sc < 128)
            {
                char c = key_to_char(sc);
                if (c)
                {
                    if (g_filewin.path_len + 1 < (int)sizeof(g_filewin.path))
                    {
                        g_filewin.path[g_filewin.path_len++] = c;
                        g_filewin.path[g_filewin.path_len] = 0;
                        desktop_mark_dirty();
                    }
                }
                has_sc = 0;
            }
        }

        // Text Editor input + dragging
        if (g_notepad.open && wm_get_front() == g_win_notepad)
        {
            if (has_sc)
            {
                char c = 0;
                if (sc == 0x1C)
                    c = '\n'; // Enter
                else if (sc == 0x0E)
                { // Backspace
                    if (g_notepad.len > 0)
                    {
                        g_notepad.len--;
                        g_notepad.buf[g_notepad.len] = 0;
                    }
                    notepad_render();
                    goto after_keys;
                }
                else if (sc == 0x01)
                { // ESC close
                    notepad_close();
                    goto after_keys;
                }
                else if (sc < 128)
                    c = key_to_char(sc);
                if (c && g_notepad.len < (int)sizeof(g_notepad.buf) - 1)
                {
                    g_notepad.buf[g_notepad.len++] = c;
                    g_notepad.buf[g_notepad.len] = 0;
                    notepad_render();
                }
            }

            // Dragging by title bar
            int mx = mouse_get_x(), my = mouse_get_y();
            int left_down = (btn & 1);
            int title_x = g_notepad.wx, title_y = g_notepad.wy - 22, title_w = g_notepad.ww, title_h = 20;
            if (left_down && !g_notepad.dragging)
            {
                if (mx >= title_x && mx < title_x + title_w && my >= title_y && my < title_y + title_h)
                {
                    g_notepad.dragging = 1;
                    g_notepad.drag_offx = mx - g_notepad.wx;
                    g_notepad.drag_offy = my - g_notepad.wy;
                }
            }
            if (g_notepad.dragging)
            {
                if (!left_down)
                {
                    g_notepad.dragging = 0;
                }
                else
                {
                    int nx = mx - g_notepad.drag_offx;
                    int ny = my - g_notepad.drag_offy;

                    // clamp
                    if (nx < 0)
                        nx = 0;
                    if (ny < 24)
                        ny = 24;
                    if (nx + g_notepad.ww > (int)fb.width)
                        nx = fb.width - g_notepad.ww;
                    if (ny + g_notepad.wh > (int)fb.height)
                        ny = fb.height - g_notepad.wh;

                    // 좌표만 업데이트
                    g_notepad.wx = nx;
                    g_notepad.wy = ny;

                    // Mark dirty; main loop will refresh
                    desktop_mark_dirty();
                }
            }
        }

        // Dragging by title bar (File Explorer)
        if (g_filewin.open && !g_filewin.minimized && wm_get_front() == g_win_file)
        {
            int mx = mouse_get_x(), my = mouse_get_y();
            int left_down = (btn & 1);
            int title_x = g_filewin.wx, title_y = g_filewin.wy - 22, title_w = g_filewin.ww, title_h = 20;
            if (left_down && !g_filewin.dragging)
            {
                if (mx >= title_x && mx < title_x + title_w && my >= title_y && my < title_y + title_h)
                {
                    g_filewin.dragging = 1;
                    g_filewin.drag_offx = mx - g_filewin.wx;
                    g_filewin.drag_offy = my - g_filewin.wy;
                }
            }
            if (g_filewin.dragging)
            {
                if (!left_down)
                {
                    g_filewin.dragging = 0;
                }
                else
                {
                    int nx = mx - g_filewin.drag_offx;
                    int ny = my - g_filewin.drag_offy;

                    if (nx < 0)
                        nx = 0;
                    if (ny < 24)
                        ny = 24;
                    if (nx + g_filewin.ww > (int)fb.width)
                        nx = fb.width - g_filewin.ww;
                    if (ny + g_filewin.wh > (int)fb.height)
                        ny = fb.height - g_filewin.wh;

                    g_filewin.wx = nx;
                    g_filewin.wy = ny;

                    desktop_mark_dirty();
                }
            }
        }

        // Dragging by title bar (System Monitor)
        if (g_taskmgr.open && !g_taskmgr.minimized && wm_get_front() == g_win_taskmgr)
        {
            int mx = mouse_get_x(), my = mouse_get_y();
            int left_down = (btn & 1);
            int title_x = g_taskmgr.wx, title_y = g_taskmgr.wy - 22, title_w = g_taskmgr.ww, title_h = 20;
            if (left_down && !g_taskmgr.dragging)
            {
                if (mx >= title_x && mx < title_x + title_w && my >= title_y && my < title_y + title_h)
                {
                    g_taskmgr.dragging = 1;
                    g_taskmgr.drag_offx = mx - g_taskmgr.wx;
                    g_taskmgr.drag_offy = my - g_taskmgr.wy;
                }
            }
            if (g_taskmgr.dragging)
            {
                if (!left_down)
                {
                    g_taskmgr.dragging = 0;
                }
                else
                {
                    int nx = mx - g_taskmgr.drag_offx;
                    int ny = my - g_taskmgr.drag_offy;

                    if (nx < 0)
                        nx = 0;
                    if (ny < 24)
                        ny = 24;
                    if (nx + g_taskmgr.ww > (int)fb.width)
                        nx = fb.width - g_taskmgr.ww;
                    if (ny + g_taskmgr.wh > (int)fb.height)
                        ny = fb.height - g_taskmgr.wh;

                    g_taskmgr.wx = nx;
                    g_taskmgr.wy = ny;

                    desktop_mark_dirty();
                }
            }
        }

        // Dragging by title bar (Display Settings)
        if (g_display.open && !g_display.minimized && wm_get_front() == g_win_display)
        {
            int mx = mouse_get_x(), my = mouse_get_y();
            int left_down = (btn & 1);
            int title_x = g_display.wx, title_y = g_display.wy - 22, title_w = g_display.ww, title_h = 20;
            if (left_down && !g_display.dragging)
            {
                if (mx >= title_x && mx < title_x + title_w && my >= title_y && my < title_y + title_h)
                {
                    g_display.dragging = 1;
                    g_display.drag_offx = mx - g_display.wx;
                    g_display.drag_offy = my - g_display.wy;
                }
            }
            if (g_display.dragging)
            {
                if (!left_down)
                {
                    g_display.dragging = 0;
                }
                else
                {
                    int nx = mx - g_display.drag_offx;
                    int ny = my - g_display.drag_offy;

                    if (nx < 0)
                        nx = 0;
                    if (ny < 24)
                        ny = 24;
                    if (nx + g_display.ww > (int)fb.width)
                        nx = fb.width - g_display.ww;
                    if (ny + g_display.wh > (int)fb.height)
                        ny = fb.height - g_display.wh;

                    g_display.wx = nx;
                    g_display.wy = ny;

                    desktop_mark_dirty();
                }
            }
        }

        // Dragging/resizing (Image Viewer)
        if (g_imgview.open && !g_imgview.minimized && wm_get_front() == g_win_imgview)
        {
            int mx = mouse_get_x(), my = mouse_get_y();
            int left_down = (btn & 1);
            int title_x = g_imgview.wx, title_y = g_imgview.wy - 22, title_w = g_imgview.ww, title_h = 20;
            int corner_x = g_imgview.wx + g_imgview.ww;
            int corner_y = g_imgview.wy + g_imgview.wh;

            if (!g_imgview.resizing && left_down &&
                mx >= corner_x - 10 && mx <= corner_x && my >= corner_y - 10 && my <= corner_y)
            {
                g_imgview.resizing = 1;
                g_imgview.resize_offx = corner_x - mx;
                g_imgview.resize_offy = corner_y - my;
            }

            if (g_imgview.resizing)
            {
                if (!left_down)
                {
                    g_imgview.resizing = 0;
                }
                else
                {
                    int new_w = mx + g_imgview.resize_offx - g_imgview.wx;
                    int new_h = my + g_imgview.resize_offy - g_imgview.wy;
                    if (new_w < 180) new_w = 180;
                    if (new_h < 140) new_h = 140;
                    if (g_imgview.wx + new_w > (int)fb.width)
                        new_w = fb.width - g_imgview.wx;
                    if (g_imgview.wy + new_h > (int)fb.height)
                        new_h = fb.height - g_imgview.wy;
                    g_imgview.ww = new_w;
                    g_imgview.wh = new_h;
                    desktop_mark_dirty();
                }
            }
            else
            {
                if (left_down && !g_imgview.dragging)
                {
                    if (mx >= title_x && mx < title_x + title_w && my >= title_y && my < title_y + title_h)
                    {
                        g_imgview.dragging = 1;
                        g_imgview.drag_offx = mx - g_imgview.wx;
                        g_imgview.drag_offy = my - g_imgview.wy;
                    }
                }
                if (g_imgview.dragging)
                {
                    if (!left_down)
                    {
                        g_imgview.dragging = 0;
                    }
                    else
                    {
                        int nx = mx - g_imgview.drag_offx;
                        int ny = my - g_imgview.drag_offy;

                        if (nx < 0) nx = 0;
                        if (ny < 24) ny = 24;
                        if (nx + g_imgview.ww > (int)fb.width)
                            nx = fb.width - g_imgview.ww;
                        if (ny + g_imgview.wh > (int)fb.height)
                            ny = fb.height - g_imgview.wh;

                        g_imgview.wx = nx;
                        g_imgview.wy = ny;
                        desktop_mark_dirty();
                    }
                }
            }
        }

        // Dragging (WAV Player)
        if (g_wavplay.open && !g_wavplay.minimized && wm_get_front() == g_win_wavplay)
        {
            int mx = mouse_get_x(), my = mouse_get_y();
            int left_down = (btn & 1);
            int title_x = g_wavplay.wx, title_y = g_wavplay.wy - 22, title_w = g_wavplay.ww, title_h = 20;
            if (left_down && !g_wavplay.dragging)
            {
                if (mx >= title_x && mx < title_x + title_w && my >= title_y && my < title_y + title_h)
                {
                    g_wavplay.dragging = 1;
                    g_wavplay.drag_offx = mx - g_wavplay.wx;
                    g_wavplay.drag_offy = my - g_wavplay.wy;
                }
            }
            if (g_wavplay.dragging)
            {
                if (!left_down)
                {
                    g_wavplay.dragging = 0;
                }
                else
                {
                    int nx = mx - g_wavplay.drag_offx;
                    int ny = my - g_wavplay.drag_offy;

                    if (nx < 0) nx = 0;
                    if (ny < 24) ny = 24;
                    if (nx + g_wavplay.ww > (int)fb.width)
                        nx = fb.width - g_wavplay.ww;
                    if (ny + g_wavplay.wh > (int)fb.height)
                        ny = fb.height - g_wavplay.wh;

                    g_wavplay.wx = nx;
                    g_wavplay.wy = ny;
                    desktop_mark_dirty();
                }
            }
        }

        // Dragging by title bar (Terminal)
        if (g_terminal.open && !g_terminal.minimized && wm_get_front() == g_win_terminal)
        {
            int mx = mouse_get_x(), my = mouse_get_y();
            int left_down = (btn & 1);
            int title_x = g_terminal.wx, title_y = g_terminal.wy - 22, title_w = g_terminal.ww, title_h = 20;
            if (left_down && !g_terminal.dragging)
            {
                if (mx >= title_x && mx < title_x + title_w && my >= title_y && my < title_y + title_h)
                {
                    g_terminal.dragging = 1;
                    g_terminal.drag_offx = mx - g_terminal.wx;
                    g_terminal.drag_offy = my - g_terminal.wy;
                }
            }
            if (g_terminal.dragging)
            {
                if (!left_down)
                {
                    g_terminal.dragging = 0;
                }
                else
                {
                    int nx = mx - g_terminal.drag_offx;
                    int ny = my - g_terminal.drag_offy;

                    if (nx < 0)
                        nx = 0;
                    if (ny < 24)
                        ny = 24;
                    if (nx + g_terminal.ww > (int)fb.width)
                        nx = fb.width - g_terminal.ww;
                    if (ny + g_terminal.wh > (int)fb.height)
                        ny = fb.height - g_terminal.wh;

                    g_terminal.wx = nx;
                    g_terminal.wy = ny;

                    desktop_mark_dirty();
                }
            }
        }
    after_keys:
        if (g_fb_ready)
        {
        static uint64_t last_frame_tick = 0;
        uint64_t frame_ticks = desktop_frame_ticks_value();
        uint64_t now = jiffies;

        int mx = mouse_get_x();
        int my = mouse_get_y();

        int moved = (mx != last_mouse_x) || (my != last_mouse_y);

        int want_resize_cursor = 0;
        if (g_imgview.open && !g_imgview.minimized)
        {
            int hx = g_imgview.wx + g_imgview.ww;
            int hy = g_imgview.wy + g_imgview.wh;
            if ((mx >= hx - 10 && mx <= hx) && (my >= hy - 10 && my <= hy))
                want_resize_cursor = 1;
            if (g_imgview.resizing)
                want_resize_cursor = 1;
        }
        if (want_resize_cursor && !g_cursor_resize_active)
            cursor_use_resize();
        else if (!want_resize_cursor && g_cursor_resize_active)
            cursor_use_default();

        if (moved)
            desktop_mark_dirty();

        // Redraw on timer or when desktop dirty; cursor/time updated in desktop_render
        if ((now - last_frame_tick >= frame_ticks) || desktop_dirty())
        {
            last_mouse_x = mx;
            last_mouse_y = my;
            desktop_render();
            last_frame_tick = now;
        }
    }

        __asm__ volatile("sti; hlt");
    }
}
