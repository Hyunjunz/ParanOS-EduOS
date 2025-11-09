[bits 32]
global tss_flush
tss_flush:
    mov ax, 0x28        ; ← TSS descriptor selector, RPL=0 꼭!
    ltr ax
    ret