#include "panic.h"
#include <fb.h>
#include <psf.h>
#include <string.h>
#include <io.h>
#include <serial.h>

volatile panic_info_t g_panic_info = {0};
volatile int g_panic_triggered = 0;
extern uint32_t *fb_addr;
extern uint32_t fb_width;
extern uint32_t fb_height;

static void draw_box(uint32_t color_bg)
{
    uint32_t *lfb = fb_get_addr(); // Linear Framebuffer base
    uint32_t w = fb_get_width();
    uint32_t h = fb_get_height();
    for (uint32_t y = 0; y < h; ++y)
    {
        for (uint32_t x = 0; x < w; ++x)
            lfb[y * w + x] = color_bg;
    }
}

void panic_handle(uint32_t vector, uint32_t err_code)
{
    uint32_t cr2, eip, cs, eflags;
    __asm__ volatile("mov %%cr2, %0" : "=r"(cr2));
    __asm__ volatile("pushf; pop %0" : "=r"(eflags));
    // EIP/CS는 레지스터 스택에서 가져오는 게 정석이나, 여기선 단순화

    g_panic_info.vector = vector;
    g_panic_info.err_code = err_code;
    g_panic_info.cr2 = cr2;
    g_panic_info.eip = eip;
    g_panic_info.cs = cs;
    g_panic_info.eflags = eflags;

    g_panic_triggered = 1;
    panic_show(); // 직접 화면 출력

    serial_printf("[PANIC] vector=%u err=%u cr2=0x%x\n", vector, err_code, cr2);
    __asm__ volatile("cli; hlt");
}


void panic_handle_s(uint32_t vector, uint32_t err_code)
{
    uint32_t cr2, eip, cs, eflags;
    __asm__ volatile("mov %%cr2, %0" : "=r"(cr2));
    __asm__ volatile("pushf; pop %0" : "=r"(eflags));
    // EIP/CS는 레지스터 스택에서 가져오는 게 정석이나, 여기선 단순화

    g_panic_info.vector = vector;
    g_panic_info.err_code = err_code;
    g_panic_info.cr2 = cr2;
    g_panic_info.eip = eip;
    g_panic_info.cs = cs;
    g_panic_info.eflags = eflags;

    g_panic_triggered = 1;
    super_panic(); // 직접 화면 출력

    serial_printf("[PANIC] vector=%u err=%u cr2=0x%x\n", vector, err_code, cr2);
    __asm__ volatile("cli; hlt");
}

void panic_show(void)
{
    if (!g_panic_triggered)
        return;
    draw_box(0x0000AA); 
    draw_text(20, 20, ":| Your PC ran into a problem and needs to restart", 0xFFFFFF, 0x0000AA);
    draw_text(20, 60, "We're just collecting some error info, and then we'll restart for you.", 0xFFFFFF, 0x0000AA);

    char buf[128];
    sprintf(buf, "Vector      : %u", g_panic_info.vector);
    draw_text(20, 130, buf, 0xFFFF00, 0x0000AA);

    sprintf(buf, "Error Code  : %u", g_panic_info.err_code);
    draw_text(20, 150, buf, 0xFFFF00, 0x0000AA);

    sprintf(buf, "CR2         : 0x%08x", g_panic_info.cr2);
    draw_text(20, 170, buf, 0xFFFF00, 0x0000AA);

    sprintf(buf, "EFLAGS      : 0x%08x", g_panic_info.eflags);
    draw_text(20, 190, buf, 0xFFFF00, 0x0000AA);

    draw_text(20, 240, "If you call a support person, give them this info.", 0xFFFFFF, 0x0000AA);
    draw_text(20, 260, "Stop Code: KERNEL_PANIC_EXCEPTION", 0xFFFFFF, 0x0000AA);
    draw_text(20, 280, "INFO: ", 0xFFFFFF, 0x0000AA);
    draw_text(20, 300, "    VAM KERNEL BUILD DATE: 2025.10.26 (01H.1)", 0xFFFFFF, 0x0000AA);
}

void super_panic(void) {
    if (!g_panic_triggered)
        return;

    // 붉은 경고 화면 배경
    draw_box(0xeb4034);

    // 상단 장식선
    draw_text(0, 10,
              "===============================================================================================",
              0xFFFFFF, 0xeb4034);

    // 제목 및 안내
    draw_text(80, 40, ":X  A critical system error has occurred!", 0xFFFFFF, 0xeb4034);
    draw_text(80, 60, "Your system encountered a fatal issue and must be reset.", 0xFFFFFF, 0xeb4034);
    draw_text(80, 80, "We recommend formatting your system or checking hardware.", 0xFFFFFF, 0xeb4034);

    // 빈 줄 구분
    draw_text(0, 110,
              "-----------------------------------------------------------------------------------------------",
              0xFFFFFF, 0xeb4034);

    // 시스템 진단 정보
    char buf[128];
    sprintf(buf, "Vector       : %u", g_panic_info.vector);
    draw_text(40, 140, buf, 0xFFFF00, 0xeb4034);

    sprintf(buf, "Error Code   : %u", g_panic_info.err_code);
    draw_text(40, 160, buf, 0xFFFF00, 0xeb4034);

    sprintf(buf, "CR2          : 0x%08x", g_panic_info.cr2);
    draw_text(40, 180, buf, 0xFFFF00, 0xeb4034);

    sprintf(buf, "EFLAGS       : 0x%08x", g_panic_info.eflags);
    draw_text(40, 200, buf, 0xFFFF00, 0xeb4034);


    draw_text(0, 230,
              "-----------------------------------------------------------------------------------------------",
              0xFFFFFF, 0xeb4034);


    draw_text(40, 260, "If you call a support person, give them this info.", 0xFFFFFF, 0xeb4034);
    draw_text(40, 280, "Stop Code : TRIPLE_FAULT", 0xFFFFFF, 0xeb4034);
    draw_text(40, 300, "Build Info: VAM Kernel - 2025.10.26 (01H.2)", 0xFFFFFF, 0xeb4034);
    draw_text(90, 320, "Online Support Code: 001", 0xFFFFFF, 0xeb4034);


    draw_text(0, 350,
              "===============================================================================================",
              0xFFFFFF, 0xeb4034);
}
