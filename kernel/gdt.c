#include <stdint.h>
#include "gdt.h"

/* ───────────────
 * GDT 엔트리 레이아웃
 * ─────────────── */

/* 일반 세그먼트(코드/데이터)용 8바이트 디스크립터 */
static uint64_t make_code64_desc(void) {
    /*
     * 64비트 코드 세그먼트 (base=0, limit=0xFFFFF, G=1, L=1, D=0)
     * 0x00AF9A000000FFFF 패턴 자주 사용됨
     */
    return 0x00AF9A000000FFFFULL;
}

static uint64_t make_data_desc(void) {
    /* 데이터 세그먼트 (base=0, limit=0xFFFFF, G=1, L=0, D=1 or 0)
     * 64비트에서는 D 비트는 무시되지만 일반적으로 0x00AF92000000FFFF 자주 사용
     */
    return 0x00AF92000000FFFFULL;
}

/* TSS 디스크립터 (16바이트) */
struct tss_descriptor {
    uint16_t limit_low;
    uint16_t base_low;
    uint8_t  base_mid1;
    uint8_t  type;       // 0x89: 64-bit available TSS
    uint8_t  limit_high  : 4;
    uint8_t  flags       : 4;
    uint8_t  base_mid2;
    uint32_t base_high;
    uint32_t reserved;
} __attribute__((packed));

/* GDT 전체 – 0:null, 1:code, 2:data, 3:TSS(16B) */
static struct {
    uint64_t        null;
    uint64_t        code;
    uint64_t        data;
    struct tss_descriptor tss_desc;
} __attribute__((packed)) gdt;

/* 전역 TSS 객체 */
struct tss64 g_tss;

/* GDTR */
static struct gdtr gdt_reg;

/* 외부 ASM 함수 */
extern void gdt_flush(struct gdtr *gdtr);
extern void tss_flush(uint16_t sel);

/* TSS 디스크립터 채우기 */
static void set_tss_descriptor(struct tss_descriptor *d, struct tss64 *tss) {
    uint64_t base  = (uint64_t)tss;
    uint32_t limit = sizeof(struct tss64) - 1;

    d->limit_low   = (uint16_t)(limit & 0xFFFF);
    d->base_low    = (uint16_t)(base & 0xFFFF);
    d->base_mid1   = (uint8_t)((base >> 16) & 0xFF);
    d->type        = 0x89;  // 64-bit available TSS
    d->limit_high  = (limit >> 16) & 0xF;
    d->flags       = 0;     // G=0 (byte granularity), AVL=0
    d->base_mid2   = (uint8_t)((base >> 24) & 0xFF);
    d->base_high   = (uint32_t)(base >> 32);
    d->reserved    = 0;
}

void gdt_init(uint64_t kernel_stack_top) {
    /* GDT 엔트리 설정 */
    gdt.null = 0;
    gdt.code = make_code64_desc();
    gdt.data = make_data_desc();

    /* TSS 초기화 */
    for (unsigned i = 0; i < sizeof(g_tss); i++)
        ((uint8_t *)&g_tss)[i] = 0;

    g_tss.rsp0 = kernel_stack_top;
    g_tss.iopb_offset = sizeof(struct tss64);

    set_tss_descriptor(&gdt.tss_desc, &g_tss);

    /* GDTR 설정 */
    gdt_reg.limit = sizeof(gdt) - 1;
    gdt_reg.base  = (uint64_t)&gdt;

    /* GDT 로드 + 세그먼트 재설정 */
    gdt_flush(&gdt_reg);

    /* TSS 로드 */
    tss_flush(GDT_SEL_TSS);
}

/* Double Fault용 IST 스택 설정 (IST1 사용) */
void tss_set_df_ist(uint64_t ist_stack_top) {
    g_tss.ist1 = ist_stack_top;
}
