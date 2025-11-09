#include "gdt.h"
#include "tss.h"

typedef struct {
    uint16_t limit_low;
    uint16_t base_low;
    uint8_t  base_mid;
    uint8_t  access;
    uint8_t  gran;
    uint8_t  base_hi;
} __attribute__((packed)) gdt_entry_t;

typedef struct {
    uint16_t limit;
    uint32_t base;
} __attribute__((packed)) gdt_ptr_t;

static gdt_entry_t gdt[8];
static gdt_ptr_t   gp;

tss_t g_tss;

void gdt_set_gate(int idx, uint32_t base, uint32_t limit, uint8_t access, uint8_t gran)
{
    gdt[idx].base_low  = base & 0xFFFF;
    gdt[idx].base_mid  = (base >> 16) & 0xFF;
    gdt[idx].base_hi   = (base >> 24) & 0xFF;
    gdt[idx].limit_low = limit & 0xFFFF;
    gdt[idx].gran      = ((limit >> 16) & 0x0F) | (gran & 0xF0);
    gdt[idx].access    = access;
}

extern void gdt_flush(uint32_t gdt_ptr);
extern void tss_flush(void);

#define ACC_P      0x80
#define ACC_DPL0   0x00
#define ACC_DPL3   0x60
#define ACC_S      0x10   /* descriptor type (1=code/data) */
#define ACC_EXEC   0x08   /* executable */
#define ACC_RW     0x02   /* readable/writable */
#define ACC_TSS    0x09   /* 32-bit available TSS */
#define GRAN_4K    0x80
#define GRAN_32    0x40

#define KERNEL_CS  0x08
#define KERNEL_DS  0x10
#define USER_CS    0x1B   // index=3, TI=0, RPL=3
#define USER_DS    0x23   // index=4, TI=0, RPL=3

#define GDT_ENTRY(base, limit, access, flags)

#define ACC_CODE_K (ACC_P|ACC_DPL0|ACC_S|ACC_EXEC|ACC_RW)  /* 0x9A */
#define ACC_DATA_K (ACC_P|ACC_DPL0|ACC_S|ACC_RW)           /* 0x92 */
#define ACC_CODE_U (ACC_P|ACC_DPL3|ACC_S|ACC_EXEC|ACC_RW)  /* 0xFA */
#define ACC_DATA_U (ACC_P|ACC_DPL3|ACC_S|ACC_RW)           /* 0xF2 */

void gdt_install_with_tss(uint32_t kstack_top)
{
    gp.limit = sizeof(gdt) - 1;
    gp.base  = (uint32_t)&gdt[0];

    /* 0: null */
    gdt_set_gate(0, 0, 0, 0, 0);

    /* 1: kernel code (0x08) */
    gdt_set_gate(1, 0, 0xFFFFF, ACC_CODE_K, GRAN_4K | GRAN_32);

    /* 2: kernel data (0x10) */
    gdt_set_gate(2, 0, 0xFFFFF, ACC_DATA_K, GRAN_4K | GRAN_32);

    /* 5: TSS (0x28) */
    for (uint32_t *p = (uint32_t*)&g_tss; p < (uint32_t*)(&g_tss + 1); ++p)
        *p = 0;

    g_tss.ss0  = 0x10; /* kernel data */
    g_tss.esp0 = kstack_top;
    g_tss.iomap_base = sizeof(tss_t); 

    uint32_t base  = (uint32_t)&g_tss;
    uint32_t limit = sizeof(tss_t) - 1;   // ← 꼭 -1
    gdt_set_gate(5, base, limit, ACC_P | ACC_TSS, 0);

    gdt_set_gate(3, 0x00000000, 0xFFFFFFFF, 0xFA, 0xCF);
    gdt_set_gate(4, 0x00000000, 0xFFFFFFFF, 0xF2, 0xCF);

    /* LGDT + reload segment registers + LTR */
    gdt_flush((uint32_t)&gp);
    tss_flush();
}

void gdt_set_entry(uint32_t i, uint32_t base, uint32_t limit, uint8_t access, uint8_t gran)
    __attribute__((alias("gdt_set_gate")));