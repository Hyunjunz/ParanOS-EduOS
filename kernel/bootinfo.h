#pragma once
#include <stdint.h>

struct bootinfo {
    uint64_t fb_phys;   // 물리 주소 (GRUB에서 받은)
    uint32_t fb_w;      // 가로 픽셀 수
    uint32_t fb_h;      // 세로 픽셀 수
    uint32_t fb_pitch;  // 한 줄당 바이트 수
    uint32_t fb_bpp;    // 비트당 픽셀 수
    void*    fb_virt;   // 매핑 후 커널에서 사용할 가상 주소
};

extern struct bootinfo g_bootinfo;
extern uint32_t g_mbinfo_phys;


extern char __kernel_high_start[];
extern char __kernel_phys_start[];

#define PA2VA(pa) ((uintptr_t)(pa) - (uintptr_t)__kernel_phys_start + (uintptr_t)__kernel_high_start)
#define VA2PA(va) ((uintptr_t)(va) - (uintptr_t)__kernel_high_start + (uintptr_t)__kernel_phys_start)

/* ─────────────────────────────── */
void bootinfo_parse(uint32_t mb_info_phys);
