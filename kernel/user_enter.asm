; ring3 진입용 iret 프레임 생성은 C에서 하고,
; 여기서는 세그먼트 레지스터와 스택 프레임을 밀어놓고 iret만 수행
; void enter_user_mode(uint32_t user_eip, uint32_t user_esp);
BITS 32
GLOBAL enter_user_mode

SECTION .text
enter_user_mode:
    ; 인자: [esp+4]=eip, [esp+8]=esp
    mov eax, [esp+4]     ; user_eip
    mov edx, [esp+8]     ; user_esp

    ; DS/ES/FS/GS를 유저 데이터로 바꿀 준비 (RPL=3)
    mov ax, 0x23         ; GDT_UDATA
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax

    ; iret 프레임: SS, ESP, EFLAGS, CS, EIP
    push dword 0x23      ; SS (user data | RPL=3)
    push edx             ; ESP
    pushfd
    or dword [esp], 0x200 ; IF=1
    push dword 0x1B      ; CS (user code | RPL=3)
    push eax             ; EIP
    iretd
