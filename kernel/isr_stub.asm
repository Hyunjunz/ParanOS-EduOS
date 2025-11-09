; =======================================================
; 32-bit ISR / IRQ Stub
; 통일 규약: vector → error 순서로 push
;  [esp+48] = vector, [esp+52] = error
;  add esp, 8 로 두 인자 제거
; =======================================================
BITS 32
section .text align=16

; ------------------------------
; Extern symbols
; ------------------------------
extern isr_common_handler
extern serial_printf

; ------------------------------
; Global exported stubs
; ------------------------------
%macro GLOB_ISR 1
global isr_stub%1
%endmacro
%macro GLOB_IRQ 1
global irq_stub%1
%endmacro

%assign i 0
%rep 32
GLOB_ISR i
%assign i i + 1
%endrep

%assign i 0
%rep 16
GLOB_IRQ i
%assign i i + 1
%endrep

global isr_stub80

; ------------------------------
; Stack offsets
; ------------------------------
%define VEC_OFF 52
%define ERR_OFF 48

; ------------------------------
; ISR macros
; ------------------------------
%macro ISR_NOERR 1
isr_stub%1:
    push dword %1          ; vector 먼저
    push dword 0           ; error 나중
    jmp isr_common_noerr
%endmacro

%macro ISR_ERR 1
isr_stub%1:
    mov eax, [esp]         ; CPU가 올린 원본 error
    push dword %1          ; vector 먼저
    push eax               ; error 복제본 (나중)
    jmp isr_common_err
%endmacro

; =======================================================
; Common handler (NOERR)
; =======================================================
isr_common_noerr:
    pusha
    push ds
    push es
    push fs
    push gs

    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax

    ; vector/error 전달
    mov eax, [esp + VEC_OFF]   ; vector
    mov edx, [esp + ERR_OFF]   ; error
    push edx                   ; 2nd arg
    push eax                   ; 1st arg
    call isr_common_handler
    add esp, 8

    pop gs
    pop fs
    pop es
    pop ds
    popa

    add esp, 8                 ; 우리가 푸시한 (vector, error) 제거
    iretd

; =======================================================
; Common handler (ERR)
; =======================================================
isr_common_err:
    pusha
    push ds
    push es
    push fs
    push gs

    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax

    mov eax, [esp + VEC_OFF]   ; vector
    mov edx, [esp + ERR_OFF]   ; error 복제본
    push edx
    push eax
    call isr_common_handler
    add esp, 8

    pop gs
    pop fs
    pop es
    pop ds
    popa

    add esp, 8                 ; (vector, error 복제본) 제거
    add esp, 4                 ; CPU가 올린 원래 error 제거
    iretd

; =======================================================
; CPU Exceptions (0–31)
; =======================================================
ISR_NOERR 0
ISR_NOERR 1
ISR_NOERR 2
ISR_NOERR 3
ISR_NOERR 4
ISR_NOERR 5
ISR_NOERR 6
ISR_NOERR 7
ISR_ERR   8
ISR_NOERR 9
ISR_ERR   10
ISR_ERR   11
ISR_ERR   12
ISR_ERR   13
ISR_ERR   14
ISR_NOERR 15
ISR_NOERR 16
ISR_NOERR 17
ISR_NOERR 18
ISR_NOERR 19
ISR_NOERR 20
ISR_NOERR 21
ISR_NOERR 22
ISR_NOERR 23
ISR_NOERR 24
ISR_NOERR 25
ISR_NOERR 26
ISR_NOERR 27
ISR_NOERR 28
ISR_NOERR 29
ISR_ERR   30
ISR_NOERR 31

; =======================================================
; IRQ (PIC 0x20–0x2F)
; =======================================================
%assign i 0
%rep 16
irq_stub %+ i:
    push dword (32 + i)
    push dword 0
    jmp isr_common_noerr
%assign i i + 1
%endrep

; =======================================================
; System call (int 0x80)
; =======================================================
isr_stub80:
    push dword 0x80
    push dword 0
    jmp isr_common_noerr
