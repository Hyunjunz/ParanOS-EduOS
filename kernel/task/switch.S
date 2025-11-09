.intel_syntax noprefix
.global ctx_switch
/* void ctx_switch(uint32_t *prev_esp, uint32_t *next_esp) */
ctx_switch:
    /* 저장 프레임: ebp, ebx, esi, edi (총 16바이트) */
    push ebp
    push ebx
    push esi
    push edi

    /* 인자 위치 (cdecl): [esp+20] = &prev_esp, [esp+24] = &next_esp
       (16바이트 push + 4바이트 ret = 20) */
    mov eax, [esp + 20]      /* &prev_esp */
    mov [eax], esp           /* *prev_esp = esp */

    mov edx, [esp + 24]      /* &next_esp */
    mov esp, [edx]           /* esp = *next_esp */

    /* 다음 태스크의 저장 프레임 복구 순서와 prepare_*와 일치해야 함 */
    pop edi
    pop esi
    pop ebx
    pop ebp
    ret
