#include <stdint.h>
#include <stddef.h>
#include <limine.h>

#include "bootinfo.h"

__attribute__((used, section(".limine_requests")))
static volatile struct limine_framebuffer_request framebuffer_request = {
    .id = LIMINE_FRAMEBUFFER_REQUEST_ID,
    .revision = 0
};

void limine_fill_bootinfo_from_fb(void)
{
    if (framebuffer_request.response == NULL)
        return;

    struct limine_framebuffer_response *resp = framebuffer_request.response;
    if (resp->framebuffer_count == 0)
        return;

    struct limine_framebuffer *fb = resp->framebuffers[0];

    g_bootinfo.fb_phys  = (uint64_t)fb->address;
    g_bootinfo.fb_w     = fb->width;
    g_bootinfo.fb_h     = fb->height;
    g_bootinfo.fb_pitch = fb->pitch;
    g_bootinfo.fb_bpp   = fb->bpp;
}
