// kernel/kernel.c — High-half kernel + VGA + PMM + IDT/PIC/PIT/Serial/Keyboard

#include <stdint.h>
#include <stddef.h>
#include "mm/vmm.h"
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

#define U_ENTRY 0x00800000u
#define U_STACK_TOP 0x00C00000u

extern uint32_t g_mbinfo_phys;
extern uint32_t stack_top;
extern struct multiboot_tag_framebuffer *mb_fb;
extern uint32_t *pgdir;
extern uint32_t page_directory[], page_table0[], page_table_hh[];
extern char __text_lma[], __kernel_high_start[], __kernel_high_end[];
extern uint8_t _kernel_stack_top;

#define VGA_TEXT ((volatile uint16_t *)0xB8000)
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

extern uint32_t pmm_alloc_phys(void); // 물리 페이지 하나 할당 (이미 있으실 겁니다)
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
    size_t i = 0;
    while (s[i] && x + i < VGA_COLS)
        put_cell(x + i++, y, s[i], color);
}

static void write_center(const char *s, uint8_t color, size_t row)
{
    size_t len = 0;
    while (s[len])
        ++len;
    size_t start = (len >= VGA_COLS) ? 0 : (VGA_COLS - len) / 2;
    write_at(s, color, start, row);
}

static void print_hex32_at(uint32_t v, uint8_t color, size_t x, size_t y)
{
    static const char *H = "0123456789ABCDEF";
    char buf[9];
    for (int i = 0; i < 8; i++)
    {
        buf[7 - i] = H[v & 0xF];
        v >>= 4;
    }
    buf[8] = 0;
    write_at(buf, color, x, y);
}

static void print_dec32_at(uint32_t v, uint8_t color, size_t x, size_t y)
{
    char tmp[12];
    int i = 0;

    if (!v)
    {
        write_at("0", color, x, y);
        return;
    }

    while (v)
    {
        tmp[i++] = '0' + (v % 10);
        v /= 10;
    }

    char out[12];
    for (int j = 0; j < i; ++j)
        out[j] = tmp[i - j - 1];
    out[i] = 0;

    write_at(out, color, x, y);
}

static inline uint32_t read_cr0(void)
{
    uint32_t v;
    __asm__ __volatile__("mov %%cr0,%0" : "=r"(v));
    return v;
}

static inline uint64_t rdtsc(void)
{
    uint32_t lo, hi;
    __asm__ __volatile__("rdtsc" : "=a"(lo), "=d"(hi));
    return ((uint64_t)hi << 32) | lo;
}

static inline void enable_io_iopl3(void)
{
    uint32_t eflags;
    __asm__ volatile("pushf; pop %0" : "=r"(eflags));
    eflags |= (3u << 12); // IOPL = 3
    __asm__ volatile("push %0; popf" ::"r"(eflags));
}

static inline void enable_io_full(void)
{
    uint32_t eflags;
    __asm__ volatile("pushf; pop %0" : "=r"(eflags));
    eflags |= (3 << 12);
    __asm__ volatile("push %0; popf" ::"r"(eflags));

    uint32_t new_eflags;
    __asm__ volatile("pushf; pop %0" : "=r"(new_eflags));
    serial_printf("[eflags-after]=%08x\n", new_eflags);
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

static void jump_to_user_real(uint32_t entry, uint32_t user_stack_top)
{
    if (!vmm_alloc_page(U_ENTRY, VMM_P | VMM_RW | VMM_US))
    {
        serial_printf("[user] map failed: entry\n");
        for (;;)
            __asm__ __volatile__("cli; hlt");
    }
    if (!vmm_alloc_page(U_STACK_TOP - 0x1000, VMM_P | VMM_RW | VMM_US))
    {
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
    serial_printf("[user] entry=%08x stack=%08x q=%d phys=%08x fl=%08x\n",
                  U_ENTRY, U_STACK_TOP, q, (unsigned)phys, fl);
    if (q != 0 || (fl & (VMM_P | VMM_US)) != (VMM_P | VMM_US))
    {
        serial_printf("[user] bad mapping (P/US missing)\n");
        for (;;)
            __asm__ __volatile__("cli; hlt");
    }
    __asm__ volatile(
        "cli\n"
        "mov $0x23, %%ax\n" // USER_DS (DPL=3)
        "mov %%ax, %%ds\n"
        "mov %%ax, %%es\n"
        "mov %%ax, %%fs\n"
        "mov %%ax, %%gs\n"
        "pushl $0x23\n"     // SS = USER_DS
        "pushl %[ustack]\n" // ESP
        "pushfl\n"
        "pushl $0x1B\n"    // CS = USER_CS
        "pushl %[entry]\n" // EIP
        "iret\n"
        :
        : [entry] "r"(entry), [ustack] "r"(user_stack_top)
        : "ax");
}

void delay(uint64_t us)
{
    const uint64_t CPU_MHZ = 1000; // Assume 1GHz CPU
    uint64_t start = rdtsc();
    uint64_t ticks = us * CPU_MHZ;
    while (rdtsc() - start < ticks)
        __asm__ __volatile__("pause");
}

#define PAGE_SIZE 4096u
#define KHEAP_PAGES 1024u

static inline uintptr_t align_up(uintptr_t v, uintptr_t a)
{
    return (v + (a - 1)) & ~(a - 1);
}

static uint8_t *kheap_begin, *kheap_end, *kheap_brk;

static void kheap_init(void)
{
    uintptr_t hb = align_up((uintptr_t)__kernel_high_end, PAGE_SIZE);
    kheap_begin = (uint8_t *)hb;
    kheap_end = (uint8_t *)(hb + KHEAP_PAGES * PAGE_SIZE);
    kheap_brk = kheap_begin;
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

extern void pmm_init(void *heap_top);
extern void *pmm_alloc(void);
extern void pmm_free(void *);
extern uint32_t pmm_free_count(void);

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

void kmain(void)
{
    __asm__ __volatile__("cli");

    bootinfo_parse(g_mbinfo_phys);
    gdt_install_with_tss((uint32_t)&stack_top);

    serial_init(COM1);

    enable_io_iopl3();
    enable_io_full();

    kheap_init();
    serial_printf("\nSTEP >> GDT and heap initialized.");

    for (uint32_t i = 0; i < 1024; i++)
        page_table0[i] = (i * 0x1000) | 0x3;

    uintptr_t phys_kernel_start = (uintptr_t)__text_lma;
    uintptr_t phys_kernel_end = phys_kernel_start +
                                ((uintptr_t)__kernel_high_end - (uintptr_t)__kernel_high_start);
    uintptr_t heap_top_phys = align_up(phys_kernel_end, PAGE_SIZE);

    pmm_init((void *)heap_top_phys);
    vmm_init();
    psf_init();

    serial_printf("\nSTEP >> PMM/VMM initialized successfully.");

    int fb_ready = 0;
    if (g_bootinfo.fb_phys && g_bootinfo.fb_w && g_bootinfo.fb_h &&
        g_bootinfo.fb_pitch && (g_bootinfo.fb_bpp == 24 || g_bootinfo.fb_bpp == 32))
    {
        if (fb_map(g_bootinfo.fb_phys, g_bootinfo.fb_w, g_bootinfo.fb_h,
                   g_bootinfo.fb_pitch, g_bootinfo.fb_bpp) == 0)
        {
            fb_clear(0xFF1E1E1E);
            draw_text(20, 20, "Kernel starting...", 0xFFFFFFFF, 0xFF1E1E1E);
            fb_flush();
            fb_ready = 1;
        }
        else
        {
            serial_printf("Framebuffer mapping failed.\n");
        }
    }

    if (!fb_ready)
    {
        clear(0x00);
        write_center("Kernel starting...", 0x0F, 2);
    }

    write_at("CR0 = 0x", 0x0F, 2, 5);
    uint32_t cr0 = read_cr0();
    print_hex32_at(cr0, 0x0F, 11, 5);

    write_at("[PMM] initialized", 0x0E, 2, 9);
    write_at("[VMM] initialized", 0x0F, 2, 12);

    idt_install_core();
    pic_remap();
    pit_init(100);
    keyboard_init();

    serial_write(COM1, "[serial] kernel up: IDT/PIC/PIT/KBD ready\r\n");

    pic_clear_mask(0);
    pic_clear_mask(1);

    __asm__ __volatile__("sti"); // Enable interrupts

    write_center("-- All Drivers Initialized Successfully --", 0x0A, VGA_ROWS - 2);

    /* ── Boot animation ── */
    const char *msg = "Booting OS...";
    int tx = fb.width / 2 - psf_width() * 6;
    int ty = fb.height / 2 - psf_height() * 2;

    draw_text(tx, ty, msg, 0xFFFFFF, 0x000000);
    fb_flush();

    int bar_width = fb.width / 2;
    int bar_height = 12;
    int bar_x = (fb.width - bar_width) / 2;
    int bar_y = ty + psf_height() * 2 + 20;

    draw_text(tx - 40, bar_y + 30, "Booting...", 0x00FF00, 0x000000);
    fb_flush();

    for (int i = 0; i <= bar_width; i += 8)
    {
        draw_rect(bar_x, bar_y, i, bar_height, 0x00A0FF);
        fb_flush();
        delay(90);
    }

    draw_text(tx - 40, bar_y + 30, "Boot Complete", 0x00FF00, 0x000000);

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

    jump_to_user_real(U_ENTRY, U_STACK_TOP);

    uint64_t last_jiffies = 0;

    size_t cursor_x = 0;
    size_t cursor_y = VGA_ROWS - 3;

    for (;;)
    {

        if (jiffies - last_jiffies >= 10)
        {
            last_jiffies = jiffies;
            write_at("ticks: ", 0x0F, 2, VGA_ROWS - 1);
            print_dec32_at((uint32_t)jiffies, 0x0F, 10, VGA_ROWS - 1);
            if (fb_ready)
                fb_flush();
        }

        uint8_t sc;
        if (kbd_get_scancode(&sc))
        {
            if (!(sc & 0x80))
            {
                char c = 0;
                if (sc < 128)
                    c = kbd_us_map[sc];

                if (c)
                {
                    serial_printf("key: %c\n", c);

                    put_cell(cursor_x++, cursor_y, c, 0x0F);

                    if (c == '\n')
                    {
                        cursor_x = 0;
                        cursor_y++;
                    }

                    if (cursor_x >= VGA_COLS)
                    {
                        cursor_x = 0;
                        cursor_y++;
                    }
                    if (cursor_y >= VGA_ROWS - 1)
                    {
                        clear(0x00);
                        cursor_y = VGA_ROWS - 3;
                    }
                }
            }
            else
            {
            }
        }

        __asm__ volatile("sti; hlt");
    }
}
