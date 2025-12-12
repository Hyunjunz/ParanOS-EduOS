/* 64비트 ELF 커널 */
OUTPUT_FORMAT("elf64-x86-64")
OUTPUT_ARCH(i386:x86-64)
ENTRY(_start)

/* 원하는 커널 가상주소 (하이하프 커널) */
KERNEL_VMA = 0xFFFFFFFF80000000;

SECTIONS
{
    /* 하이하프 커널 VMA 시작 */
    . = KERNEL_VMA;

    /* ───────────────────────────────
       Limine request 영역 (순서 + AT 중요)
       ─────────────────────────────── */
    .limine_requests_start_marker ALIGN(8) :
        AT(ADDR(.limine_requests_start_marker) - KERNEL_VMA)
    {
        KEEP(*(.limine_requests_start_marker))
    }

    .limine_requests ALIGN(8) :
        AT(ADDR(.limine_requests) - KERNEL_VMA)
    {
        KEEP(*(.limine_requests))
    }

    .limine_requests_end_marker ALIGN(8) :
        AT(ADDR(.limine_requests_end_marker) - KERNEL_VMA)
    {
        KEEP(*(.limine_requests_end_marker))
    }

    /* ───────────────────────────────
       TEXT SECTION
       ─────────────────────────────── */
    .text ALIGN(0x1000) :
        AT(ADDR(.text) - KERNEL_VMA)
    {
        KEEP(*(.text.boot))     /* 초기 부트코드(필요 시) */
        *(.text*)
    }

    /* ───────────────────────────────
       RODATA SECTION
       ─────────────────────────────── */
    .rodata ALIGN(0x1000) :
        AT(ADDR(.rodata) - KERNEL_VMA)
    {
        *(.rodata*)
    }

    /* ───────────────────────────────
       DATA SECTION
       ─────────────────────────────── */
    .data ALIGN(0x1000) :
        AT(ADDR(.data) - KERNEL_VMA)
    {
        data_begin = .;
        s2_data_begin = .;
        *(.data*)
        s2_data_end = .;
        data_end = .;
    }

    /* ───────────────────────────────
       BSS SECTION
       ─────────────────────────────── */
    .bss ALIGN(0x1000) :
        AT(ADDR(.bss) - KERNEL_VMA)
    {
        bss_begin = .;
        *(COMMON)
        *(.bss*)
        bss_end = .;
    }

    /* ───────────────────────────────
       DISCARD (필요 없는 섹션 제거)
       ─────────────────────────────── */
    /DISCARD/ :
    {
        *(.eh_frame*)
        *(.note*)
        *(.comment*)
    }

    /* ───────────────────────────────
       공개 심볼
       ─────────────────────────────── */
    __kernel_start      = ADDR(.text);
    __kernel_end        = ADDR(.bss) + SIZEOF(.bss);

    __kernel_phys_start = LOADADDR(.text);
    __kernel_phys_end   = LOADADDR(.bss) + SIZEOF(.bss);
    __kernel_high_start = ADDR(.text);
    __kernel_high_end   = ADDR(.bss) + SIZEOF(.bss);
    __text_lma          = LOADADDR(.text);
}
