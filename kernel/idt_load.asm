; idt_load.asm - x86_64 long mode IDT load

BITS 64
global idt_load
extern idtp         ; C side provides idtp (limit + base)

idt_load:
    lidt    [idtp]  ; load IDTR from idtp
    ret

