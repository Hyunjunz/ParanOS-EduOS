#include "bootinfo.h"
#include "multiboot2.h"
#include <stdint.h>
#include <string.h>

uint32_t g_mbinfo_phys = 0;
struct bootinfo g_bootinfo;

void bootinfo_parse(uint32_t addr) {
    uint32_t total_size = *(uint32_t*)addr;
    uint32_t current = addr + 8;

    while (current < addr + total_size) {
        struct multiboot_tag* tag = (struct multiboot_tag*)current;
        if (tag->type == MULTIBOOT_TAG_TYPE_FRAMEBUFFER) {
            
            break;
        }
        current += (tag->size + 7) & ~7;
    }
}
