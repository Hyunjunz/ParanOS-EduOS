ENTRY(_start)

PHYS_BASE = 0x00100000;   /* 1 MiB */
VIRT_BASE = 0xC0000000;   /* high-half base */

SECTIONS
{
  /* ── 1) 물리 저주소: GRUB이 직접 읽는 영역 ───────────────── */
  . = PHYS_BASE;

  .multiboot2 ALIGN(8) : {
    KEEP(*(.multiboot2))
  }

  . = ALIGN(4096);
  .boot ALIGN(4096) : {
    KEEP(*(.boot*))
  }

  /* .boot 끝의 '물리' 위치를 고정 상수로 캡처 */
  __KERN_PHYS_BASE = ALIGN(LOADADDR(.boot) + SIZEOF(.boot), 4096);

  /* ── 2) 하이하프(가상) 커널 본체: VMA는 고정, LMA는 공식으로 계산 ─ */
  . = VIRT_BASE;

  /* 코드 */
  .text ALIGN(4096) :
    AT( ADDR(.text) - VIRT_BASE + __KERN_PHYS_BASE )
  {
    *(.text*)
  }

  /* (C/ASM 커널이면 보통 버림) */
  /DISCARD/ : {
    *(.eh_frame) *(.eh_frame.*) *(.eh_frame_hdr)
    *(.gcc_except_table*) *(.gnu.build.attributes*)
  }

  /* 읽기 전용 데이터 */
  .rodata ALIGN(4096) :
    AT( ADDR(.rodata) - VIRT_BASE + __KERN_PHYS_BASE )
  {
    *(.rodata*)
  }

  /* 초기화 데이터 */
  .data ALIGN(4096) :
    AT( ADDR(.data) - VIRT_BASE + __KERN_PHYS_BASE )
  {
    *(.data*)
  }

  /* BSS */
  .bss ALIGN(4096) :
    AT( ADDR(.bss) - VIRT_BASE + __KERN_PHYS_BASE )
  {
    *(COMMON)
    *(.bss*)
  }

  /* (선택) 스택: 이미지에 포함하지 않음 */
  .stack ALIGN(16) (NOLOAD) : {
    stack_bottom = .;
    . += 4096;
    stack_top = .;
  }

  /* ── 공개 심볼 ───────────────────────────────────────────── */
  __text_lma           = LOADADDR(.text);
  __kernel_high_start  = ADDR(.text);
  __kernel_high_end    = ADDR(.bss) + SIZEOF(.bss);
  __kernel_phys_start  = LOADADDR(.text);
  __kernel_phys_end    = LOADADDR(.bss) + SIZEOF(.bss);
}
