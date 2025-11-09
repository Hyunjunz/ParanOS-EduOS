#pragma once
#include <stdint.h>

#define MULTIBOOT_TAG_TYPE_FRAMEBUFFER 8

struct multiboot_tag {
    uint32_t type;
    uint32_t size;
};

struct multiboot_tag_framebuffer_common {
    uint64_t framebuffer_addr;
    uint32_t framebuffer_pitch;
    uint32_t framebuffer_width;
    uint32_t framebuffer_height;
    uint8_t  framebuffer_bpp;
    uint8_t  framebuffer_type;
    uint16_t reserved;
};

struct multiboot_tag_framebuffer {
    struct multiboot_tag tag;
    struct multiboot_tag_framebuffer_common common;
    // 색상 팔레트, 모드 등에 따라 추가 필드 있을 수 있음
} __attribute__((packed));
