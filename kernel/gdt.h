#pragma once
#include <stdint.h>

/* ───────────────
 * GDT 인덱스 / 셀렉터
 * ─────────────── */
#define GDT_IDX_NULL         0
#define GDT_IDX_KERNEL_CODE  1
#define GDT_IDX_KERNEL_DATA  2
#define GDT_IDX_TSS          3   // TSS descriptor (16 bytes: index 3 and 4 사용)

/* 셀렉터 = 인덱스 << 3 */
#define GDT_SEL_KERNEL_CODE  (GDT_IDX_KERNEL_CODE << 3)
#define GDT_SEL_KERNEL_DATA  (GDT_IDX_KERNEL_DATA << 3)
#define GDT_SEL_TSS          (GDT_IDX_TSS         << 3)

/* 기존 df_tss.c 와의 호환용 이름 */
#define GDT_IDX_DF_TSS       GDT_IDX_TSS
#define GDT_SEL_DF_TSS       GDT_SEL_TSS

/* GDTR 구조체 */
struct gdtr {
    uint16_t limit;
    uint64_t base;
} __attribute__((packed));

/* 64비트 TSS 구조체 (Long Mode용) */
struct tss64 {
    uint32_t reserved0;
    uint64_t rsp0;
    uint64_t rsp1;
    uint64_t rsp2;
    uint64_t reserved1;

    uint64_t ist1;
    uint64_t ist2;
    uint64_t ist3;
    uint64_t ist4;
    uint64_t ist5;
    uint64_t ist6;
    uint64_t ist7;

    uint64_t reserved2;
    uint16_t reserved3;
    uint16_t iopb_offset;
} __attribute__((packed));

/* 전역 TSS 객체 (gdt.c 에서 정의) */
extern struct tss64 g_tss;

/* 초기화 함수들 */
void gdt_init(uint64_t kernel_stack_top);
void tss_set_df_ist(uint64_t ist_stack_top);
