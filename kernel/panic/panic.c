#include <stdint.h>
#include "panic.h"
#include <fb.h>
#include <psf.h>
#include <string.h>
#include <io.h>
#include <serial.h>
#include <mm/vmm.h>

volatile panic_info_t g_panic_info = {0};
volatile int g_panic_triggered = 0;
extern uint32_t *fb_addr;
extern uint32_t fb_width;
extern uint32_t fb_height;

static void draw_box(uint32_t color_bg)
{
    uint32_t *lfb = fb_get_addr();
    uint32_t w = fb_get_width();
    uint32_t h = fb_get_height();
    for (uint32_t y = 0; y < h; ++y) {
        for (uint32_t x = 0; x < w; ++x)
            lfb[y * w + x] = color_bg;
    }
}

static int panic_can_draw(void)
{
    // 이미 한 번 그려서 터졌던 경우 다시는 그리지 않기 위해
    if (!g_panic_triggered)
        return 0;

    // 프레임 버퍼 주소
    uintptr_t fb_ptr = (uintptr_t)fb_get_addr();
    if (!fb_ptr)
        return 0;

    // 페이지 테이블에 실제로 매핑돼 있고, 쓰기 가능한지 확인
    uint32_t fb_flags = 0;
    if (vmm_query(fb_ptr, NULL, &fb_flags) != 0)
        return 0;
    if (!(fb_flags & PAGE_RW))  // 혹은 VMM_RW와 맞게 매크로 사용
        return 0;

    // 폰트도 준비 안 됐으면 텍스트 출력은 안 하는 게 안전
    if (!psf_ready())
        return 0;

    return 1;
}


static void fill_frame_info(const uint64_t *frame, uint64_t *out_rip,
                            uint64_t *out_cs, uint64_t *out_rflags,
                            uint64_t *out_rsp, uint64_t *out_ss)
{
    if (!frame) {
        *out_rip = 0;
        *out_cs = 0;
        *out_rflags = 0;
        *out_rsp = 0;
        *out_ss = 0;
        return;
    }

    // Long mode exception frame layout:
    // [0]=RIP, [1]=CS, [2]=RFLAGS, [3]=RSP (if ring change), [4]=SS (if ring change)
    *out_rip    = frame[0];
    *out_cs     = frame[1];
    *out_rflags = frame[2];

    // Only valid when coming from lower privilege levels; else these slots
    // contain whatever was on the kernel stack.
    *out_rsp = frame[3];
    *out_ss  = frame[4];
}



void panic_handle(uint32_t vector, uint32_t err_code, const uint64_t *frame)
{
    uint64_t cr2, rflags;
    uint64_t eip = 0, cs = 0, rsp = 0, ss = 0;

    __asm__ volatile("mov %%cr2, %0" : "=r"(cr2));
    __asm__ volatile("pushfq; pop %0" : "=r"(rflags));

    fill_frame_info(frame, &eip, &cs, &rflags, &rsp, &ss);

    g_panic_info.vector  = vector;
    g_panic_info.err_code = err_code;
    g_panic_info.cr2     = cr2;
    g_panic_info.eip     = eip;
    g_panic_info.cs      = cs;
    g_panic_info.eflags  = rflags;
    g_panic_info.rsp     = rsp;
    g_panic_info.ss      = ss;

    int nested = g_panic_triggered;   // ★ 이미 패닉 중인지 확인
    g_panic_triggered = 1;

    serial_printf("[PANIC] vector=%llu err=%llu cr2=%p rip=%p cs=%llx rflags=%p rsp=%p ss=%llx\n",
                  (unsigned long long)vector, (unsigned long long)err_code,
                  (void *)(uintptr_t)cr2,
                  (void *)(uintptr_t)eip, (unsigned long long)cs,
                  (void *)(uintptr_t)rflags, (void *)(uintptr_t)rsp, (unsigned long long)ss);

    if (!nested && panic_can_draw()) {
        serial_printf("[PANIC] Show Panic Screen\n");
        super_panic();
    } else if (!nested) {
        serial_printf("[PANIC] framebuffer or font unavailable; skipping draw\n");
    } else {
        // 중첩 패닉이면 그냥 로그만 찍고 멈춤 (그래픽 X)
        serial_printf("[PANIC] nested panic (vector=%llu cr2=%p), skip graphics\n",
                      (unsigned long long)vector, (void *)(uintptr_t)cr2);
    }

    __asm__ volatile("cli; hlt");
}


void panic_handle_s(uint32_t vector, uint32_t err_code, const uint64_t *frame)
{
    uint64_t cr2, rflags;
    uint64_t eip = 0, cs = 0, rsp = 0, ss = 0;

    __asm__ volatile("mov %%cr2, %0" : "=r"(cr2));
    __asm__ volatile("pushfq; pop %0" : "=r"(rflags));

    fill_frame_info(frame, &eip, &cs, &rflags, &rsp, &ss);

    g_panic_info.vector  = vector;
    g_panic_info.err_code = err_code;
    g_panic_info.cr2     = cr2;
    g_panic_info.eip     = eip;
    g_panic_info.cs      = cs;
    g_panic_info.eflags  = rflags;
    g_panic_info.rsp     = rsp;
    g_panic_info.ss      = ss;

    int nested = g_panic_triggered;
    g_panic_triggered = 1;

    serial_printf("[PANIC] vector=%llu err=%llu cr2=%p rip=%p cs=%llx rflags=%p rsp=%p ss=%llx\n",
                  (unsigned long long)vector, (unsigned long long)err_code,
                  (void *)(uintptr_t)cr2,
                  (void *)(uintptr_t)eip, (unsigned long long)cs,
                  (void *)(uintptr_t)rflags, (void *)(uintptr_t)rsp, (unsigned long long)ss);

    if (!nested && panic_can_draw()) {
        serial_printf("[PANIC] Show SuperPanic Screen\n");
        super_panic();
    } else if (!nested) {
        serial_printf("[PANIC] framebuffer or font unavailable; skipping draw\n");
    } else {
        serial_printf("[PANIC] nested panic (vector=%llu cr2=%p), skip graphics\n",
                      (unsigned long long)vector, (void *)(uintptr_t)cr2);
    }

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
    sprintf(buf, "Vector      : %llu", (unsigned long long)g_panic_info.vector);
    draw_text(20, 130, buf, 0xFFFF00, 0x0000AA);

    sprintf(buf, "Error Code  : %llu", (unsigned long long)g_panic_info.err_code);
    draw_text(20, 150, buf, 0xFFFF00, 0x0000AA);

    sprintf(buf, "CR2         : 0x%016llx", (unsigned long long)g_panic_info.cr2);
    draw_text(20, 170, buf, 0xFFFF00, 0x0000AA);

    sprintf(buf, "EFLAGS      : 0x%016llx", (unsigned long long)g_panic_info.eflags);
    draw_text(20, 190, buf, 0xFFFF00, 0x0000AA);

    draw_text(20, 240, "If you call a support person, give them this info.", 0xFFFFFF, 0x0000AA);
    draw_text(20, 260, "Stop Code: KERNEL_PANIC_EXCEPTION", 0xFFFFFF, 0x0000AA);
    draw_text(20, 280, "INFO: ", 0xFFFFFF, 0x0000AA);
    draw_text(20, 300, "    VAM KERNEL BUILD DATE: 2025.10.26 (01H.1)", 0xFFFFFF, 0x0000AA);
}

void super_panic(void) {
    if (!g_panic_triggered)
        return;

    // 배경
    draw_box(0xeb4034);

    // 헤더 선
    draw_text(0, 20,
              "==============================================================",
              0xFFFFFF, 0xeb4034);

    // 제목
    draw_text(100, 50, "[!] CRITICAL SYSTEM FAILURE", 0xFFFFFF, 0xeb4034);
    draw_text(100, 75, "A fatal error has occurred and the system must halt.", 0xFFFFFF, 0xeb4034);
    draw_text(100, 95, "Please review the diagnostic information below.", 0xFFFFFF, 0xeb4034);

    // 구분선
    draw_text(0, 130,
              "--------------------------------------------------------------",
              0xFFFFFF, 0xeb4034);

    // 진단 정보
    char buf[128];

    draw_text(60, 155, "[ Diagnostic Information ]", 0xFFFFAA, 0xeb4034);

    sprintf(buf, "Vector      : %llu", (unsigned long long)g_panic_info.vector);
    draw_text(80, 180, buf, 0xFFFF00, 0xeb4034);

    sprintf(buf, "Error Code  : %llu", (unsigned long long)g_panic_info.err_code);
    draw_text(80, 200, buf, 0xFFFF00, 0xeb4034);

    sprintf(buf, "CR2         : 0x%016llx", (unsigned long long)g_panic_info.cr2);
    draw_text(80, 220, buf, 0xFFFF00, 0xeb4034);

    sprintf(buf, "EFLAGS      : 0x%016llx", (unsigned long long)g_panic_info.eflags);
    draw_text(80, 240, buf, 0xFFFF00, 0xeb4034);

    // 구분선
    draw_text(0, 270,
              "--------------------------------------------------------------",
              0xFFFFFF, 0xeb4034);

    // 하단 안내
    draw_text(60, 300, "If you contact support, provide the information above.", 0xFFFFFF, 0xeb4034);
    draw_text(60, 320, "Stop Code   : TRIPLE_FAULT", 0xFFFFFF, 0xeb4034);
    draw_text(60, 340, "Build Info  : ParanOS Kernel - 2025.12.01 (25Y01.12)", 0xFFFFFF, 0xeb4034);
    draw_text(60, 360, "Support Code: 001 - https://docs.5data.org/paranOS/StopCode", 0xFFFFFF, 0xeb4034);

    // 바닥선
    draw_text(0, 390,
              "==============================================================",
              0xFFFFFF, 0xeb4034);
}
