; gdt_flush.asm
global gdt_flush

SECTION .text
BITS 32
gdt_flush:
    ; cdecl: [esp+4] = &gp (가상주소)
    mov     eax, [esp+4]
    cli
    lgdt    [eax]            ; 6바이트 의사-디스크립터 (limit:16, base:32)

    ; CS는 far jump로 재적재 (selector=0x08: 커널 코드)
    jmp     0x08:.flush_cs

.flush_cs:
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax
    ; sti ← ❌ 제거
    ret
