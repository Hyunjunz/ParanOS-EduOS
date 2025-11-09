#include "idt.h"
#include "serial.h"
#include "isr.h"
#include "tss_panic/df_tss.h"
#include "gdt.h"

#ifndef COM1
#define COM1 0x3F8
#endif

#define KERNEL_CS 0x08

#define IDT_FLAG_INTGATE_32  0x8E
#define IDT_FLAG_TRAPGATE_32 0x8F

static idt_entry_t idt[256];
idt_ptr_t idtp;

extern void idt_load(void);

/* ISR/IRQ 스텁 선언 (필요한 만큼) */
extern void isr_stub0(void);  extern void isr_stub1(void);
extern void isr_stub2(void);  extern void isr_stub3(void);
extern void isr_stub4(void);  extern void isr_stub5(void);
extern void isr_stub6(void);  extern void isr_stub7(void);
extern void isr_stub8(void);  extern void isr_stub9(void);
extern void isr_stub10(void); extern void isr_stub11(void);
extern void isr_stub12(void); extern void isr_stub13(void);
extern void isr_stub14(void); extern void isr_stub15(void);
extern void isr_stub16(void); extern void isr_stub17(void);
extern void isr_stub18(void); extern void isr_stub19(void);
extern void isr_stub20(void); extern void isr_stub21(void);
extern void isr_stub22(void); extern void isr_stub23(void);
extern void isr_stub24(void); extern void isr_stub25(void);
extern void isr_stub26(void); extern void isr_stub27(void);
extern void isr_stub28(void); extern void isr_stub29(void);
extern void isr_stub30(void); extern void isr_stub31(void);

extern void irq_stub0(void);  extern void irq_stub1(void);
extern void irq_stub2(void);  extern void irq_stub3(void);
extern void irq_stub4(void);  extern void irq_stub5(void);
extern void irq_stub6(void);  extern void irq_stub7(void);
extern void irq_stub8(void);  extern void irq_stub9(void);
extern void irq_stub10(void); extern void irq_stub11(void);
extern void irq_stub12(void); extern void irq_stub13(void);
extern void irq_stub14(void); extern void irq_stub15(void);

/* 일반 게이트 설정 */
void idt_set_gate(int n, uint32_t handler, uint16_t sel, uint8_t type_attr) {
    idt[n].offset_low  = handler & 0xFFFF;
    idt[n].offset_high = (handler >> 16) & 0xFFFF;
    idt[n].selector    = sel;
    idt[n].always0     = 0;
    idt[n].flags       = type_attr;
}

/* ✅ Task Gate 설정 (더블 폴트용) */
void idt_set_task_gate(int n, uint16_t tss_selector)
{
    idt[n].offset_low  = 0;
    idt[n].selector    = tss_selector;
    idt[n].always0     = 0;
    idt[n].flags       = 0x85; // P=1, DPL=0, Type=0101b (Task Gate)
    idt[n].offset_high = 0;
}

/* 초기화용 */
static void idt_zero(void)
{
    for (int i = 0; i < 256; ++i) {
        idt[i].offset_low  = 0;
        idt[i].selector    = KERNEL_CS;
        idt[i].always0     = 0;
        idt[i].flags       = 0;
        idt[i].offset_high = 0;
    }
}

/* ✅ 메인 설치 함수 */
void idt_install_core(void)
{
    // 1) IDT 포인터 설정
    idtp.limit = (uint16_t)(sizeof(idt) - 1);
    idtp.base  = (uint32_t)(&idt[0]);

    // 2) IDT 초기화
    idt_zero();

    // 3) 예외 벡터 0~7 등록
    idt_set_gate(0, (uint32_t)isr_stub0,  KERNEL_CS, IDT_FLAG_INTGATE_32);
    idt_set_gate(1, (uint32_t)isr_stub1,  KERNEL_CS, IDT_FLAG_INTGATE_32);
    idt_set_gate(2, (uint32_t)isr_stub2,  KERNEL_CS, IDT_FLAG_INTGATE_32);
    idt_set_gate(3, (uint32_t)isr_stub3,  KERNEL_CS, IDT_FLAG_INTGATE_32);
    idt_set_gate(4, (uint32_t)isr_stub4,  KERNEL_CS, IDT_FLAG_INTGATE_32);
    idt_set_gate(5, (uint32_t)isr_stub5,  KERNEL_CS, IDT_FLAG_INTGATE_32);
    idt_set_gate(6, (uint32_t)isr_stub6,  KERNEL_CS, IDT_FLAG_INTGATE_32);
    idt_set_gate(7, (uint32_t)isr_stub7,  KERNEL_CS, IDT_FLAG_INTGATE_32);

    // 4) ✅ 더블 폴트 (#DF = 벡터 8)
    serial_printf("[IDT] Installing DF TSS...\n");
    df_tss_init();                          // DF TSS 초기화
    idt_set_task_gate(8, GDT_SEL_DF_TSS);   // Task Gate 등록

    // 5) 예외 9~31
    idt_set_gate(9,  (uint32_t)isr_stub9,  KERNEL_CS, IDT_FLAG_INTGATE_32);
    idt_set_gate(10, (uint32_t)isr_stub10, KERNEL_CS, IDT_FLAG_INTGATE_32);
    idt_set_gate(11, (uint32_t)isr_stub11, KERNEL_CS, IDT_FLAG_INTGATE_32);
    idt_set_gate(12, (uint32_t)isr_stub12, KERNEL_CS, IDT_FLAG_INTGATE_32);
    idt_set_gate(13, (uint32_t)isr_stub13, KERNEL_CS, IDT_FLAG_INTGATE_32);
    idt_set_gate(14, (uint32_t)isr_stub14, KERNEL_CS, IDT_FLAG_INTGATE_32);
    idt_set_gate(15, (uint32_t)isr_stub15, KERNEL_CS, IDT_FLAG_INTGATE_32);
    idt_set_gate(16, (uint32_t)isr_stub16, KERNEL_CS, IDT_FLAG_INTGATE_32);
    idt_set_gate(17, (uint32_t)isr_stub17, KERNEL_CS, IDT_FLAG_INTGATE_32);
    idt_set_gate(18, (uint32_t)isr_stub18, KERNEL_CS, IDT_FLAG_INTGATE_32);
    idt_set_gate(19, (uint32_t)isr_stub19, KERNEL_CS, IDT_FLAG_INTGATE_32);
    idt_set_gate(20, (uint32_t)isr_stub20, KERNEL_CS, IDT_FLAG_INTGATE_32);
    idt_set_gate(21, (uint32_t)isr_stub21, KERNEL_CS, IDT_FLAG_INTGATE_32);
    idt_set_gate(22, (uint32_t)isr_stub22, KERNEL_CS, IDT_FLAG_INTGATE_32);
    idt_set_gate(23, (uint32_t)isr_stub23, KERNEL_CS, IDT_FLAG_INTGATE_32);
    idt_set_gate(24, (uint32_t)isr_stub24, KERNEL_CS, IDT_FLAG_INTGATE_32);
    idt_set_gate(25, (uint32_t)isr_stub25, KERNEL_CS, IDT_FLAG_INTGATE_32);
    idt_set_gate(26, (uint32_t)isr_stub26, KERNEL_CS, IDT_FLAG_INTGATE_32);
    idt_set_gate(27, (uint32_t)isr_stub27, KERNEL_CS, IDT_FLAG_INTGATE_32);
    idt_set_gate(28, (uint32_t)isr_stub28, KERNEL_CS, IDT_FLAG_INTGATE_32);
    idt_set_gate(29, (uint32_t)isr_stub29, KERNEL_CS, IDT_FLAG_INTGATE_32);
    idt_set_gate(30, (uint32_t)isr_stub30, KERNEL_CS, IDT_FLAG_INTGATE_32);
    idt_set_gate(31, (uint32_t)isr_stub31, KERNEL_CS, IDT_FLAG_INTGATE_32);

    // 6) IRQ 0~15 (32~47)
    idt_set_gate(32, (uint32_t)irq_stub0,  KERNEL_CS, IDT_FLAG_INTGATE_32);
    idt_set_gate(33, (uint32_t)irq_stub1,  KERNEL_CS, IDT_FLAG_INTGATE_32);
    idt_set_gate(34, (uint32_t)irq_stub2,  KERNEL_CS, IDT_FLAG_INTGATE_32);
    idt_set_gate(35, (uint32_t)irq_stub3,  KERNEL_CS, IDT_FLAG_INTGATE_32);
    idt_set_gate(36, (uint32_t)irq_stub4,  KERNEL_CS, IDT_FLAG_INTGATE_32);
    idt_set_gate(37, (uint32_t)irq_stub5,  KERNEL_CS, IDT_FLAG_INTGATE_32);
    idt_set_gate(38, (uint32_t)irq_stub6,  KERNEL_CS, IDT_FLAG_INTGATE_32);
    idt_set_gate(39, (uint32_t)irq_stub7,  KERNEL_CS, IDT_FLAG_INTGATE_32);
    idt_set_gate(40, (uint32_t)irq_stub8,  KERNEL_CS, IDT_FLAG_INTGATE_32);
    idt_set_gate(41, (uint32_t)irq_stub9,  KERNEL_CS, IDT_FLAG_INTGATE_32);
    idt_set_gate(42, (uint32_t)irq_stub10, KERNEL_CS, IDT_FLAG_INTGATE_32);
    idt_set_gate(43, (uint32_t)irq_stub11, KERNEL_CS, IDT_FLAG_INTGATE_32);
    idt_set_gate(44, (uint32_t)irq_stub12, KERNEL_CS, IDT_FLAG_INTGATE_32);
    idt_set_gate(45, (uint32_t)irq_stub13, KERNEL_CS, IDT_FLAG_INTGATE_32);
    idt_set_gate(46, (uint32_t)irq_stub14, KERNEL_CS, IDT_FLAG_INTGATE_32);
    idt_set_gate(47, (uint32_t)irq_stub15, KERNEL_CS, IDT_FLAG_INTGATE_32);

    // 7) 로드
    idt_load();

    serial_printf("[IDT] base=%08x limit=%04x (DF=#8 TaskGate)\n",
                  idtp.base, idtp.limit);
}
