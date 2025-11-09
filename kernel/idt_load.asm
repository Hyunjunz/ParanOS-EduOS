BITS 32
global idt_load
extern idtp         ; C 코드에서 static 아니어야 함!

idt_load:
    lidt [idtp]     ; IDTR ← idtp 구조체 (limit + base)
    ret