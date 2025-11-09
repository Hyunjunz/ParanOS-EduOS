#include "tss.h"
#include <string.h>
#include <stddef.h>

extern tss_t g_tss;

void tss_set_kernel_stack(uint32_t stack_top) {
    g_tss.esp0 = stack_top;
}

void tss_init_fields(uint32_t kernel_stack_top, uint16_t kernel_data_sel) {
    memset(&g_tss, 0, sizeof(g_tss));

    g_tss.ss0  = kernel_data_sel;     // 예: 0x10
    g_tss.esp0 = kernel_stack_top;

    g_tss.iomap_base = (uint16_t)offsetof(tss_t, io_bitmap);
    memset(g_tss.io_bitmap, 0xFF, sizeof(g_tss.io_bitmap));
    g_tss.io_bitmap[sizeof(g_tss.io_bitmap) - 1] = 0xFF;

    g_tss.trap = 0;
}
