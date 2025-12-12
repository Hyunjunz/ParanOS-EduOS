#include "idt.h"
#include "serial.h"
#include "isr.h"
#include "gdt.h"            // 나중에 TSS/IST 쓸 때를 위해 남겨둠
#include <stddef.h>
#include <string.h>

#ifndef COM1
#define COM1 0x3F8
#endif

// 64-bit interrupt/trap gate flags.
// P=1, DPL=0, Type=1110b (interrupt gate) / 1111b (trap gate).
#define IDT_FLAG_INTGATE   0x8E
#define IDT_FLAG_TRAPGATE  0x8F

// IDT and IDTR
static idt_entry_t idt[256];

// Exported so that idt_load.asm can 'extern idtp'.
idt_ptr_t idtp;

// These stubs must be provided by your ISR/IRQ ASM code.
extern void isr_stub0(void);   extern void isr_stub1(void);
extern void isr_stub2(void);   extern void isr_stub3(void);
extern void isr_stub4(void);   extern void isr_stub5(void);
extern void isr_stub6(void);   extern void isr_stub7(void);
extern void isr_stub8(void);   extern void isr_stub9(void);
extern void isr_stub10(void);  extern void isr_stub11(void);
extern void isr_stub12(void);  extern void isr_stub13(void);
extern void isr_stub14(void);  extern void isr_stub15(void);
extern void isr_stub16(void);  extern void isr_stub17(void);
extern void isr_stub18(void);  extern void isr_stub19(void);
extern void isr_stub20(void);  extern void isr_stub21(void);
extern void isr_stub22(void);  extern void isr_stub23(void);
extern void isr_stub24(void);  extern void isr_stub25(void);
extern void isr_stub26(void);  extern void isr_stub27(void);
extern void isr_stub28(void);  extern void isr_stub29(void);
extern void isr_stub30(void);  extern void isr_stub31(void);

extern void irq_stub0(void);   extern void irq_stub1(void);
extern void irq_stub2(void);   extern void irq_stub3(void);
extern void irq_stub4(void);   extern void irq_stub5(void);
extern void irq_stub6(void);   extern void irq_stub7(void);
extern void irq_stub8(void);   extern void irq_stub9(void);
extern void irq_stub10(void);  extern void irq_stub11(void);
extern void irq_stub12(void);  extern void irq_stub13(void);
extern void irq_stub14(void);  extern void irq_stub15(void);

// If you have a syscall stub (e.g. int 0x80), declare it here.
// extern void isr_stub128(void);

// ---------------------------------------------------------------------
// IDT gate setup (64-bit)
// ---------------------------------------------------------------------
void idt_set_gate(int n, uint64_t handler,
                  uint16_t sel, uint8_t type_attr, uint8_t ist)
{
    idt[n].offset_low  =  handler        & 0xFFFF;
    idt[n].selector    =  sel;
    idt[n].ist         =  ist & 0x7;          // only low 3 bits used
    idt[n].type_attr   =  type_attr;
    idt[n].offset_mid  = (handler >> 16) & 0xFFFF;
    idt[n].offset_high = (uint32_t)(handler >> 32);
    idt[n].zero        =  0;
}

// ---------------------------------------------------------------------
// IDT initialization for x86_64 long mode
// ---------------------------------------------------------------------
void idt_init64(void)
{
    // 1) Clear IDT
    memset(idt, 0, sizeof(idt));

    // Use the current CS selector rather than assuming 0x08.
    uint16_t cs;
    __asm__ volatile ("mov %%cs, %0" : "=r"(cs));

    // 2) Fill IDTR
    idtp.limit = sizeof(idt) - 1;
    idtp.base  = (uint64_t)&idt[0];

    // 3) Exceptions (vectors 0–31)
    idt_set_gate(0,  (uint64_t)isr_stub0,  cs, IDT_FLAG_INTGATE, 0);
    idt_set_gate(1,  (uint64_t)isr_stub1,  cs, IDT_FLAG_INTGATE, 0);
    idt_set_gate(2,  (uint64_t)isr_stub2,  cs, IDT_FLAG_INTGATE, 0);
    idt_set_gate(3,  (uint64_t)isr_stub3,  cs, IDT_FLAG_TRAPGATE, 0); // breakpoint
    idt_set_gate(4,  (uint64_t)isr_stub4,  cs, IDT_FLAG_TRAPGATE, 0); // overflow
    idt_set_gate(5,  (uint64_t)isr_stub5,  cs, IDT_FLAG_INTGATE, 0);
    idt_set_gate(6,  (uint64_t)isr_stub6,  cs, IDT_FLAG_INTGATE, 0);
    idt_set_gate(7,  (uint64_t)isr_stub7,  cs, IDT_FLAG_INTGATE, 0);
    // Double fault: 별도 IST 없이 기존 스택 사용
    idt_set_gate(8,  (uint64_t)isr_stub8,  cs, IDT_FLAG_INTGATE, 0);
    idt_set_gate(9,  (uint64_t)isr_stub9,  cs, IDT_FLAG_INTGATE, 0);
    idt_set_gate(10, (uint64_t)isr_stub10, cs, IDT_FLAG_INTGATE, 0);
    idt_set_gate(11, (uint64_t)isr_stub11, cs, IDT_FLAG_INTGATE, 0);
    idt_set_gate(12, (uint64_t)isr_stub12, cs, IDT_FLAG_INTGATE, 0);
    idt_set_gate(13, (uint64_t)isr_stub13, cs, IDT_FLAG_INTGATE, 0);
    idt_set_gate(14, (uint64_t)isr_stub14, cs, IDT_FLAG_INTGATE, 0);  // page fault
    idt_set_gate(15, (uint64_t)isr_stub15, cs, IDT_FLAG_INTGATE, 0);
    idt_set_gate(16, (uint64_t)isr_stub16, cs, IDT_FLAG_INTGATE, 0);
    idt_set_gate(17, (uint64_t)isr_stub17, cs, IDT_FLAG_INTGATE, 0);
    idt_set_gate(18, (uint64_t)isr_stub18, cs, IDT_FLAG_INTGATE, 0);
    idt_set_gate(19, (uint64_t)isr_stub19, cs, IDT_FLAG_INTGATE, 0);
    idt_set_gate(20, (uint64_t)isr_stub20, cs, IDT_FLAG_INTGATE, 0);
    idt_set_gate(21, (uint64_t)isr_stub21, cs, IDT_FLAG_INTGATE, 0);
    idt_set_gate(22, (uint64_t)isr_stub22, cs, IDT_FLAG_INTGATE, 0);
    idt_set_gate(23, (uint64_t)isr_stub23, cs, IDT_FLAG_INTGATE, 0);
    idt_set_gate(24, (uint64_t)isr_stub24, cs, IDT_FLAG_INTGATE, 0);
    idt_set_gate(25, (uint64_t)isr_stub25, cs, IDT_FLAG_INTGATE, 0);
    idt_set_gate(26, (uint64_t)isr_stub26, cs, IDT_FLAG_INTGATE, 0);
    idt_set_gate(27, (uint64_t)isr_stub27, cs, IDT_FLAG_INTGATE, 0);
    idt_set_gate(28, (uint64_t)isr_stub28, cs, IDT_FLAG_INTGATE, 0);
    idt_set_gate(29, (uint64_t)isr_stub29, cs, IDT_FLAG_INTGATE, 0);
    idt_set_gate(30, (uint64_t)isr_stub30, cs, IDT_FLAG_INTGATE, 0);
    idt_set_gate(31, (uint64_t)isr_stub31, cs, IDT_FLAG_INTGATE, 0);

    // 4) Hardware IRQs (PIC remapped to 32–47 assumed)
    idt_set_gate(32, (uint64_t)irq_stub0,  cs, IDT_FLAG_INTGATE, 0);
    idt_set_gate(33, (uint64_t)irq_stub1,  cs, IDT_FLAG_INTGATE, 0);
    idt_set_gate(34, (uint64_t)irq_stub2,  cs, IDT_FLAG_INTGATE, 0);
    idt_set_gate(35, (uint64_t)irq_stub3,  cs, IDT_FLAG_INTGATE, 0);
    idt_set_gate(36, (uint64_t)irq_stub4,  cs, IDT_FLAG_INTGATE, 0);
    idt_set_gate(37, (uint64_t)irq_stub5,  cs, IDT_FLAG_INTGATE, 0);
    idt_set_gate(38, (uint64_t)irq_stub6,  cs, IDT_FLAG_INTGATE, 0);
    idt_set_gate(39, (uint64_t)irq_stub7,  cs, IDT_FLAG_INTGATE, 0);
    idt_set_gate(40, (uint64_t)irq_stub8,  cs, IDT_FLAG_INTGATE, 0);
    idt_set_gate(41, (uint64_t)irq_stub9,  cs, IDT_FLAG_INTGATE, 0);
    idt_set_gate(42, (uint64_t)irq_stub10, cs, IDT_FLAG_INTGATE, 0);
    idt_set_gate(43, (uint64_t)irq_stub11, cs, IDT_FLAG_INTGATE, 0);
    idt_set_gate(44, (uint64_t)irq_stub12, cs, IDT_FLAG_INTGATE, 0);
    idt_set_gate(45, (uint64_t)irq_stub13, cs, IDT_FLAG_INTGATE, 0);
    idt_set_gate(46, (uint64_t)irq_stub14, cs, IDT_FLAG_INTGATE, 0);
    idt_set_gate(47, (uint64_t)irq_stub15, cs, IDT_FLAG_INTGATE, 0);

    // 5) Optionally: system call vector (e.g. 0x80) if you have one.
    // idt_set_gate(0x80, (uint64_t)isr_stub128, KERNEL_CS,
    //              IDT_FLAG_TRAPGATE | 0x60, 0);
    // (0x60 sets DPL=3 so user mode can call it.)

    // 6) Load IDT
    idt_load();

    serial_printf("[IDT] base=%016lx limit=%04x (64-bit IDT)\n",
                  (unsigned long)idtp.base, idtp.limit);
}
