#ifndef TSS_H
#define TSS_H
#include <stdint.h>
#include <stddef.h>   // offsetof

// 32-bit TSS (Intel SDM Vol.3 기준)
typedef struct __attribute__((packed)) {
    uint16_t prev_tss;         uint16_t _res0;
    uint32_t esp0;             uint16_t ss0;  uint16_t _res1;
    uint32_t esp1;             uint16_t ss1;  uint16_t _res2;
    uint32_t esp2;             uint16_t ss2;  uint16_t _res3;

    uint32_t cr3;
    uint32_t eip;
    uint32_t eflags;

    uint32_t eax, ecx, edx, ebx;
    uint32_t esp, ebp, esi, edi;

    uint16_t es;               uint16_t _res_es;
    uint16_t cs;               uint16_t _res_cs;
    uint16_t ss;               uint16_t _res_ss;
    uint16_t ds;               uint16_t _res_ds;
    uint16_t fs;               uint16_t _res_fs;
    uint16_t gs;               uint16_t _res_gs;

    uint16_t ldt;              uint16_t _res_ldt;

    uint16_t trap;             // 0 또는 1
    uint16_t iomap_base;       // I/O bitmap 시작 오프셋(= this struct 기준)

    // 선택: I/O permission bitmap를 TSS 뒤에 붙여 보관
    // '마지막 바이트'는 end marker 용(항상 0xFF)
    uint8_t  io_bitmap[8192 + 1];
} tss_t;

/* 커널 스택만 바꿀 때 */
void tss_set_kernel_stack(uint32_t stack_top);

/* TSS 전체 초기화(esp0 + 세그먼트 + I/O 비트맵) */
void tss_init_fields(uint32_t kernel_stack_top, uint16_t kernel_data_sel);



/* 전역 인스턴스 (GDT가 이걸 가리키게 할 것으로 가정) */
extern tss_t g_tss;

#endif
