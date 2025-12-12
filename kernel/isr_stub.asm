; =======================================================
; 64-bit ISR / IRQ Stub (Long mode)
;  - C 함수 isr_common_handler(uint32_t vector,
;                              uint32_t error_code,
;                              uint64_t *frame) 호출
;  - frame 은 CPU 가 푸시한 예외 프레임(RIP,CS,RFLAGS,...)의 포인터
; =======================================================

BITS 64
section .text align=16

; ------------------------------
; Extern C handler
; ------------------------------
extern isr_common_handler

; ------------------------------
; Exported stubs
; ------------------------------
%macro GLOB_ISR 1
global isr_stub%1
%endmacro

%macro GLOB_IRQ 1
global irq_stub%1
%endmacro

; ISR 0~31
%assign i 0
%rep 32
GLOB_ISR i
%assign i i + 1
%endrep

; IRQ 0~15
%assign i 0
%rep 16
GLOB_IRQ i
%assign i i + 1
%endrep

; System call (int 0x80)
global isr_stub80

; ------------------------------
; 레지스터 세이브/리스토어
; ------------------------------
%macro PUSH_ALL 0
    push r15
    push r14
    push r13
    push r12
    push r11
    push r10
    push r9
    push r8
    push rdi
    push rsi
    push rbp
    push rdx
    push rcx
    push rbx
    push rax
%endmacro

%macro POP_ALL 0
    pop rax
    pop rbx
    pop rcx
    pop rdx
    pop rbp
    pop rsi
    pop rdi
    pop r8
    pop r9
    pop r10
    pop r11
    pop r12
    pop r13
    pop r14
    pop r15
%endmacro

; =======================================================
; 공통 엔트리
;  - 스택 레이아웃 (공통 형식):
;      [rsp]     = vector
;      [rsp+8]   = error_code (실제/가짜)
;      [rsp+16]  = RIP
;      [rsp+24]  = CS
;      [rsp+32]  = RFLAGS
;      ...
; =======================================================
isr_common_entry:
    PUSH_ALL

    ; PUSH_ALL 뒤 스택:
    ;  [rsp           ] = r15
    ;  ...
    ;  [rsp+15*8     ] = vector
    ;  [rsp+15*8 + 8 ] = error_code
    ;  [rsp+15*8 +16 ] = RIP (CPU frame 시작)

    mov rdi, [rsp + 15*8]        ; 1st arg: vector
    mov rsi, [rsp + 15*8 + 8]    ; 2nd arg: error_code
    lea rdx, [rsp + 15*8 + 16]   ; 3rd arg: &CPU frame (RIP 위치)

    call isr_common_handler

    POP_ALL

    ; vector + error_code 제거
    add rsp, 16

    ; CPU가 푸시한 예외 프레임으로 복귀
    iretq

; =======================================================
; 매크로
; =======================================================

; 에러 코드 없는 예외:
;  - CPU는 [RIP][CS][RFLAGS][...]
;  - 우리가 0(error_code) + vector 를 푸시해서 공통 형식 맞춤
%macro ISR_NOERR 1
isr_stub%1:
    push 0              ; fake error_code
    push %1             ; vector
    jmp isr_common_entry
%endmacro

; 에러 코드 있는 예외:
;  - CPU는 [error_code][RIP][CS][...]
;  - 우리는 vector만 추가로 푸시 (error_code는 그대로 보존)
%macro ISR_ERR 1
isr_stub%1:
    push %1             ; vector
    jmp isr_common_entry
%endmacro

; =======================================================
; CPU Exceptions (0–31)
;  - Intel/AMD 매뉴얼 기준 error_code 유무
;    * error_code 있음: 8, 10, 11, 12, 13, 14, 17
; =======================================================

ISR_NOERR 0      ; #DE
ISR_NOERR 1      ; #DB
ISR_NOERR 2      ; NMI
ISR_NOERR 3      ; #BP
ISR_NOERR 4      ; #OF
ISR_NOERR 5      ; #BR
ISR_NOERR 6      ; #UD
ISR_NOERR 7      ; #NM
ISR_ERR   8      ; #DF
ISR_NOERR 9      ; reserved
ISR_ERR   10     ; #TS
ISR_ERR   11     ; #NP
ISR_ERR   12     ; #SS
ISR_ERR   13     ; #GP
ISR_ERR   14     ; #PF
ISR_NOERR 15     ; reserved
ISR_NOERR 16     ; #MF
ISR_ERR   17     ; #AC
ISR_NOERR 18     ; #MC
ISR_NOERR 19     ; #XF
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
ISR_NOERR 30
ISR_NOERR 31

; =======================================================
; IRQ (PIC 0x20–0x2F)
;  - 모두 error_code 없음
; =======================================================
%assign i 0
%rep 16
irq_stub%+i:
    push 0                  ; error_code = 0
    push (32 + i)           ; vector = 0x20 + i
    jmp isr_common_entry
%assign i i + 1
%endrep

; =======================================================
; System call (int 0x80)
;  - error_code 없음
; =======================================================
isr_stub80:
    push 0                  ; error_code = 0
    push 0x80               ; vector = 0x80
    jmp isr_common_entry
