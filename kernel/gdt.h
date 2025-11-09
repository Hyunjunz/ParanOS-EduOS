#pragma once
#include <stdint.h>

#define ACC_P     0x80
#define ACC_DPL0  0x00
#define ACC_DPL3  0x60
#define ACC_S     0x10
#define ACC_EXEC  0x08
#define ACC_DC    0x04
#define ACC_RW    0x02
#define ACC_AC    0x01

/* 타입 조합 */
#define ACC_CODE_K  (ACC_P|ACC_DPL0|ACC_S|ACC_EXEC|ACC_RW)  /* 0x9A */
#define ACC_DATA_K  (ACC_P|ACC_DPL0|ACC_S|ACC_RW)           /* 0x92 */
#define ACC_CODE_U  (ACC_P|ACC_DPL3|ACC_S|ACC_EXEC|ACC_RW)  /* 0xFA */
#define ACC_DATA_U  (ACC_P|ACC_DPL3|ACC_S|ACC_RW)           /* 0xF2 */

/* granularity */
#define GRAN_4K  0x80
#define GRAN_32  0x40


/* ────────────────────────────────
 * Segment Selectors
 * ──────────────────────────────── */
#define GDT_KCODE 0x08
#define GDT_KDATA 0x10
#define GDT_UCODE 0x1B
#define GDT_UDATA 0x23
#define GDT_TSS   0x28

#define GDT_IDX_TSS      5
#define GDT_SEL_TSS     ((GDT_IDX_TSS) << 3)

#define GDT_IDX_DF_TSS   6
#define GDT_SEL_DF_TSS  ((GDT_IDX_DF_TSS) << 3)

void gdt_install_with_tss(uint32_t kernel_stack_top);


#define ACC_CODE_KERN  ACC_CODE_K
#define ACC_DATA_KERN  ACC_DATA_K
#define ACC_CODE_USER  ACC_CODE_U
#define ACC_DATA_USER  ACC_DATA_U

#define GDT_USER_CODE 0x1B  // selector index 3, RPL=3
#define GDT_USER_DATA 0x23 