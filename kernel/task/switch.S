.intel_syntax noprefix
.global ctx_switch

/* void ctx_switch(uint64_t *prev_rsp, uint64_t *next_rsp) */
ctx_switch:
    /* Callee-saved registers (System V ABI) */
    push rbp
    push rbx
    push r12
    push r13
    push r14
    push r15

    /* Saved frame total = 48 bytes */

    /* Arguments:
         prev_rsp = [rsp + 48 + 8] = [rsp + 56]
         next_rsp = [rsp + 48 + 16] = [rsp + 64]
    */

    mov rax, [rsp + 56]     /* rax = prev_rsp pointer */
    mov [rax], rsp          /* *prev_rsp = rsp */

    mov rdx, [rsp + 64]     /* rdx = next_rsp pointer */
    mov rsp, [rdx]          /* rsp = *next_rsp */

    /* Restore next task context */
    pop r15
    pop r14
    pop r13
    pop r12
    pop rbx
    pop rbp
    ret
