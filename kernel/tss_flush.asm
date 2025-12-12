[BITS 64]
global tss_flush

; void tss_flush(uint16_t sel);
; Load TR with the selector provided by the caller.
tss_flush:
    mov     ax, di          ; lower 16 bits of first arg
    ltr     ax
    ret
