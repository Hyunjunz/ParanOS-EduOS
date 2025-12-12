; ============================================================
; gdt_flush.asm - Long Mode 안전 버전
; ============================================================

bits 64
default rel

global gdt_flush

section .text

; ------------------------------------------------------------
; void gdt_flush(struct gdtr *gdtr)
; ------------------------------------------------------------
gdt_flush:
    lgdt [rdi]

    ; Reload data segments
    mov ax, 0x10          ; kernel data segment
    mov ss, ax
    mov ds, ax
    mov es, ax

    ; Far return to reload CS with our code segment (0x08)
    push qword 0x08
    lea  rax, [rel flush_done]
    push rax
    lretq

flush_done:
    ret
