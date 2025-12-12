; ============================================================
; kernel_entry.asm - Limine x86_64용 최소 커널 엔트리
;  - Limine가 이미 Long Mode + 페이징을 설정한 상태로 진입
;  - 여기서는:
;      * 커널 스택 설정
;      * IDT 설치
;      * kmain() 호출
;    (GDT/TSS/IST 초기화는 C 쪽 kmain() 초반에서 수행)
; ============================================================

[BITS 64]
default rel

global _start
global stack_top

extern kmain          ; void kmain(void);
extern idt_init64     ; void idt_init64(void);

section .text
align 16
_start:
    cli
    lea rsp, [rel stack_top]
    and rsp, -16

    ; 64-bit IDT 설치
    call idt_init64

    ; 메인 커널 진입
    call kmain

.hang:
    hlt
    jmp .hang

; ============================================================
; BSS 섹션 - 커널 스택
; ============================================================
section .bss
align 16
stack_bottom:
    resb    16384                ; 16 KiB 스택
stack_top:

