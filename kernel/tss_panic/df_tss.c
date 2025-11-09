#include "df_tss.h"
#include <tss.h>
#include <gdt.h>
#include <serial.h>
#include <string.h>
#include <stddef.h>

extern void df_task_entry(void);

extern uint8_t df_stack_top[];

tss_t df_tss __attribute__((aligned(16)));

void df_tss_init(void)
{
    memset(&df_tss, 0, sizeof(df_tss));

    df_tss.esp0 = (uint32_t)df_stack_top;
    df_tss.ss0  = 0x10;  
    df_tss.cr3  = 0;     
    df_tss.eip  = (uint32_t)df_task_entry;
    df_tss.eflags = 0x00000202;  // IF=0, bit1=1
    df_tss.cs = 0x08;
    df_tss.ds = df_tss.es = df_tss.fs = df_tss.gs = df_tss.ss = 0x10;
    df_tss.esp = (uint32_t)df_stack_top;
    df_tss.trap = 0;

    memset(df_tss.io_bitmap, 0xFF, sizeof(df_tss.io_bitmap));
    df_tss.io_bitmap[sizeof(df_tss.io_bitmap) - 1] = 0xFF;
    df_tss.iomap_base = (uint16_t)offsetof(tss_t, io_bitmap);

    uint32_t base = (uint32_t)&df_tss;
    uint32_t limit = sizeof(tss_t) - 1;
    gdt_set_entry(GDT_IDX_DF_TSS, base, limit, 0x89, 0x00);  // Available 32-bit TSS

    serial_printf("[DFTSS] base=0x%08x limit=0x%x sel=0x%x\n",
                  base, limit, GDT_SEL_DF_TSS);
}
