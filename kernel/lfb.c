#include <stdint.h>
#include "bootinfo.h"
#include "mm/vmm.h"

#define FB_VIRT_BASE 0xE0000000u   // 한 번만 정의하고, 커널 전역과 동일하게 유지

static inline uint32_t align_up(uint32_t x, uint32_t a){ return (x + a - 1) & ~(a - 1); }

/* 프레임버퍼를 FB_VIRT_BASE부터 연속 매핑 */
void map_framebuffer(uint32_t fb_phys, uint32_t fb_pitch, uint32_t fb_w, uint32_t fb_h) {
    // 1. 페이지 기준 정렬
    uint32_t fb_page_base = fb_phys & ~0xFFFu;     // 실제 물리 페이지 시작
    uint32_t offset        = fb_phys & 0xFFFu;     // 페이지 내 오프셋

    // 2. 프레임버퍼 전체 크기를 페이지 단위로 올림
    uint32_t size_bytes = offset + fb_pitch * fb_h;
    size_bytes = align_up(size_bytes, 0x1000u);

    uint32_t n_pages = size_bytes >> 12;

    // 3. 물리 주소를 FB_VIRT_BASE로 연속 매핑
    for (uint32_t i = 0; i < n_pages; ++i) {
        uint32_t va = FB_VIRT_BASE + (i << 12);
        uint32_t pa = fb_page_base + (i << 12);
        vmm_map(va, pa, VMM_RW | VMM_PWT | VMM_PCD);
    }

    // 4. TLB flush
    vmm_reload_cr3();

    // 5. 보정된 가상 framebuffer 시작 주소를 기록
    g_bootinfo.fb_virt = (void*)(FB_VIRT_BASE + offset);
}

/* 이후 그리기: (32bpp 가정) */
static inline volatile uint8_t* fb_ptr(void){ return (volatile uint8_t*)FB_VIRT_BASE; }

void fb_fill(uint32_t rgba){
    volatile uint8_t* fb = fb_ptr();
    for (uint32_t y=0; y<g_bootinfo.fb_h; ++y) {
        uint32_t* row = (uint32_t*)(fb + y * g_bootinfo.fb_pitch);
        for (uint32_t x=0; x<g_bootinfo.fb_w; ++x) row[x] = rgba;
    }
}
