#pragma once
#include <stdint.h>

typedef void (*isr_t)(void);

void isr_install(void);
void isr_register_handler(uint8_t n, isr_t handler);
void isr_handler_c(uint32_t int_no);
void isr_common_handler(uint32_t vector, uint32_t error_code, uint64_t *frame);

typedef struct __attribute__((packed)) {
    uint32_t ds;        // data segment selector
    uint32_t edi, esi, ebp, esp, ebx, edx, ecx, eax;  // pushed by pusha
    uint32_t int_no;    // interrupt number
    uint32_t err_code;  // error code (or 0 if none)
    uint32_t eip, cs, eflags, useresp, ss;            // pushed automatically by CPU
} isr_regs_t;
