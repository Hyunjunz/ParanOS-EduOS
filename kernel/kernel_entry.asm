; ============================================================
; kernel_entry.asm  (NASM, i386, Multiboot2, High-Half Kernel)
; ============================================================

[BITS 32]

; ------------------------------------------------------------
; Multiboot2 header
; ------------------------------------------------------------
SECTION .multiboot2
align 8
MB2_MAGIC        equ 0xE85250D6
MB2_ARCH         equ 0
MB2_HEADER_START:
    dd MB2_MAGIC
    dd MB2_ARCH
    dd MB2_HEADER_END - MB2_HEADER_START
    dd -(MB2_MAGIC + MB2_ARCH + (MB2_HEADER_END - MB2_HEADER_START))
    align 8
    dw 5,0           ; Entry address tag (unused here, size=20)
    dd 20
    dd 0,0,0
    align 8
    dw 0,0           ; End tag
    dd 8
MB2_HEADER_END:

; ------------------------------------------------------------
; Symbols
; ------------------------------------------------------------
GLOBAL _start
GLOBAL page_directory
GLOBAL page_table0
GLOBAL stack_top

EXTERN kmain
EXTERN __text_lma
EXTERN __kernel_high_start
EXTERN __kernel_high_end
EXTERN g_mbinfo_phys

%define KHEAP_PAGES 1024
%define NUM_HH_PTS  8

; VA->PA helper for HIGH-HALF symbols only
; dst = sym - __kernel_high_start + __text_lma
%macro VIRT2PHYS 2
    mov %1, %2
    sub %1, __kernel_high_start
    add %1, __text_lma
%endmacro

; ------------------------------------------------------------
; Bootstrap area (.boot is mapped identity at low addresses)
; ------------------------------------------------------------
SECTION .boot
align 16

; ── Minimal IDT (all zeros, with a stub for error-code ISRs) ──
idt_ptr:  dw idt_end - idt_start - 1
          dd idt_start
align 16
idt_start: times 256 dq 0
idt_end:

global isr_stub_err
isr_stub_err:
    add esp, 4           ; pop error-code
    iret

; set_gate_err(al = vector, edx = handler)
set_gate_err:
    push ebx
    push eax
    mov ebx, idt_start
    movzx eax, al
    shl eax, 3
    add ebx, eax
    mov eax, edx
    ; offset[0:15]
    mov word  [ebx+0], ax
    ; selector
    push eax
    mov ax, cs
    mov [ebx+2], ax
    pop eax

    ; zero
    mov byte  [ebx+4], 0
    ; type/attr (present, DPL=00, 32-bit interrupt gate)
    mov byte  [ebx+5], 0x8E
    ; offset[16:31]
    shr eax, 16
    mov word  [ebx+6], ax
    pop eax
    pop ebx
    ret

; ------------------------------------------------------------
; Entry (GRUB → 32bit, paging Off)
; ------------------------------------------------------------
_start:
    cli
    cld

    ; Use GRUB's GDT as-is: data_sel = cs + 8
    push cs
    pop ax
    add ax, 8                 ; data selector
    mov ds, ax
    mov es, ax
    mov fs, ax
    mov gs, ax
    mov ss, ax

    ; temporary stack (identity)
    mov esp, 0x0090000

    ; IDT load + simple #GP/#PF stubs
    lea eax, [idt_ptr]
    lidt [eax]
    mov al, 13
    mov edx, isr_stub_err
    call set_gate_err
    mov al, 14
    mov edx, isr_stub_err
    call set_gate_err

    ; Save Multiboot2 info physical address to a high-half symbol's PA slot
    VIRT2PHYS edi, g_mbinfo_phys
    mov [edi], ebx

    ; Ensure stack (identity) — 0x90000
    mov esp, 0x0090000

    ; ───── Initialize paging structures (.boot is low/physical) ─────
    ; NOTE: PD/PT symbols here are physical addresses already.

    ; Clear PD
    mov edi, page_directory
    xor eax, eax
    mov ecx, 1024
    rep stosd

    ; Clear PT0
    mov edi, page_table0
    xor eax, eax
    mov ecx, 1024
    rep stosd

%assign I 0
%rep NUM_HH_PTS
    mov edi, page_table_hh%+I
    xor eax, eax
    mov ecx, 1024
    rep stosd
%assign I I+1
%endrep

    ; ───── PT0: identity map 0..4MiB ─────
    xor esi, esi                 ; phys = 0
    mov edi, page_table0
    mov ecx, 1024
.fill_pt0:
    mov eax, esi
    or  eax, 0x03                ; Present|RW
    stosd
    add esi, 0x1000
    loop .fill_pt0

    ; ───── Prepare high-half mapping ─────
    ; remaining_pages = ceil((end-start)/4K) + KHEAP_PAGES
    mov eax, __kernel_high_end
    sub eax, __kernel_high_start
    add eax, 0x0FFF
    shr eax, 12
    add eax, KHEAP_PAGES
    mov esi, eax                 ; ESI = remaining pages

    mov ebx, __text_lma          ; EBX = phys_cursor (kernel LMA start)

    ; PD base (PA) — keep in EDX and NEVER clobber
    mov edx, page_directory      ; EDX = PD base (PA)

    ; PDE[0] = PT0 (identity)
    mov eax, page_table0
    or  eax, 0x03
    mov [edx + 0*4], eax

    xor edi, edi                 ; EDI = i = 0 (index ONLY)

.hh_pt_outer:
    cmp edi, NUM_HH_PTS
    jae .hh_done
    test esi, esi
    jz  .hh_done

    ; EBP = PT base (PA) for this index
    mov ebp, [page_table_hh_list + edi*4]

    ; PDE[768 + i] = pt_phys | 0x3  (map 3GiB+ region)
    mov eax, ebp
    or  eax, 0x03
    mov ecx, edi
    add ecx, 768
    shl ecx, 2
    mov [edx + ecx], eax         ; EDX: PD base constant

    ; cnt = min(remaining, 1024)
    mov ecx, esi
    cmp ecx, 1024
    jbe .cnt_ok
    mov ecx, 1024
.cnt_ok:
    push ecx                     ; save cnt (ECX gets consumed below)

    ; Fill this PT
.fill_this_pt:
    test ecx, ecx
    jz .pt_filled
    mov eax, ebx
    or  eax, 0x03                ; Present|RW
    mov [ebp], eax
    add ebx, 0x1000              ; next physical page
    add ebp, 4
    dec ecx
    jmp .fill_this_pt
.pt_filled:
    pop ecx                      ; cnt
    sub esi, ecx                 ; remaining -= cnt

    inc edi                      ; i++
    jmp .hh_pt_outer

.hh_done:

    ; CR3 <- PD (PA)
    mov eax, page_directory
    mov cr3, eax

    ; Enable paging
    mov eax, cr0
    or  eax, 0x80000000
    mov cr0, eax
    mov eax, cr3                 ; TLB flush
    mov cr3, eax

    mov eax, __kernel_high_end
    add eax, 0x0FFF
    and eax, 0xFFFFF000

    mov dword [eax], 0xDEADBEEF        ; 여기가 #PF면 상위 PDE/PT가 부족
    cmp dword [eax], 0xDEADBEEF
    jne .pf_bug


    jmp high_half_entry     ; enter high-half
.pf_bug:
    cli
    hlt
.ok:


.hang:
    cli
    hlt
    jmp .hang

; ------------------------------------------------------------
; High-half entry (now VA space active)
; ------------------------------------------------------------
SECTION .text
align 16
high_half_entry:
    jmp kmain

; ───────────────── Page structures (.boot = low/physical) ─────────────────
SECTION .boot
align 4096
page_directory:  times 1024 dd 0
align 4096
page_table0:     times 1024 dd 0

%assign I 0
%rep NUM_HH_PTS
align 4096
page_table_hh%+I: times 1024 dd 0
%assign I I+1
%endrep

; PT VA table (indexing aid) — in .boot, so value == PA
page_table_hh_list:
%assign I 0
%rep NUM_HH_PTS
    dd page_table_hh%+I
%assign I I+1
%endrep

; ------------------------------------------------------------
; BSS (low) — temporary stack (identity)
; ------------------------------------------------------------
SECTION .bss
align 16
stack_bottom: resb 4096
stack_top:
