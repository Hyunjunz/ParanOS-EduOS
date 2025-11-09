[BITS 32]
global df_task_entry
global df_stack_top
section .bss align=16
df_stack:      resb 4096
df_stack_top:

section .text align=16
df_task_entry:
    cli
    mov al, 'D'
    mov dx, 0x3F8
    out dx, al
    mov al, 'F'
    mov dx, 0x3F8
    out dx, al
    mov al, '!'
    mov dx, 0x3F8
    out dx, al

.hang:
    hlt
    jmp .hang
