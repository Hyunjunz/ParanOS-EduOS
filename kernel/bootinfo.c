#include "bootinfo.h"
#include "multiboot2.h"
#include <stdint.h>
#include <string.h>

uint32_t g_mbinfo_phys = 0;  // ✅ 정의 추가
struct bootinfo g_bootinfo;

void bootinfo_parse(uint32_t addr) {
    uint32_t total_size = *(uint32_t*)addr;
    uint32_t current = addr + 8;

    while (current < addr + total_size) {
        struct multiboot_tag* tag = (struct multiboot_tag*)current;
        if (tag->type == MULTIBOOT_TAG_TYPE_FRAMEBUFFER) {
            struct multiboot_tag_framebuffer* fb = (void*)tag;
            g_bootinfo.fb_phys  = fb->common.framebuffer_addr;
            g_bootinfo.fb_w     = fb->common.framebuffer_width;
            g_bootinfo.fb_h     = fb->common.framebuffer_height;
            g_bootinfo.fb_pitch = fb->common.framebuffer_pitch;
            g_bootinfo.fb_bpp   = fb->common.framebuffer_bpp;
            break;
        }
        current += (tag->size + 7) & ~7;
    }
}
