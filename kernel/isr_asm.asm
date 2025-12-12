; =======================================================
; 64-bit isr_asm.asm
;  - 예전 32비트용 isr_common_stub 레거시를 대체
;  - 현재 코드에서는 사용하지 않으므로, 안전하게 no-op 처리
; =======================================================
BITS 64
section .text align=16

global isr_common_stub
extern isr_common_handler

; 레거시 심볼: 현재는 아무도 호출하지 않지만,
; 혹시 모를 참조를 위해 "그냥 리턴"만 하는 더미 구현.
isr_common_stub:
    ret
