[BITS 32]
global isr_common_stub
; extern isr_handler_c
extern isr_common_handler

; 공통 스텁 — 딱 한 번만 정의!
isr_common_stub:
    pusha
    ; call isr_handler_c
    call isr_common_handler
    popa
    add  esp, 8      ; IRQ 스텁에서 push한 error code + interrupt number
    iretd

