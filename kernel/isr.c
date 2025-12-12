#include "isr.h"
#include "pic.h"
#include "serial.h"
#include <stdint.h>
#include "fb.h"
#include "panic/panic.h"
#include <stdbool.h>
#include "io.h"
#include "panic/panic.h"
#ifndef COM1
#define COM1 0x3F8
#endif

#define MAX_INTERRUPTS 256
static isr_t handlers[MAX_INTERRUPTS];

static volatile bool handling_exception = false;


/* 초기화: 모든 핸들러 NULL */
void isr_install(void) {
    for (int i = 0; i < MAX_INTERRUPTS; ++i)
        handlers[i] = 0;
}

/* 특정 벡터에 사용자 핸들러 등록 */
void isr_register_handler(uint8_t n, isr_t handler) {
    if (n < MAX_INTERRUPTS)
        handlers[n] = handler;
}

/* 어셈블리에서 호출됨: 단순히 등록된 핸들러 호출 */
void isr_handler_c(uint32_t int_no) {
    if (int_no < MAX_INTERRUPTS && handlers[int_no]) {
        handlers[int_no]();
    }

    /* PIC EOI: IRQ 범위(0x20~0x2F)면 EOI 발신 */
    if (int_no >= 32 && int_no <= 47) {
        uint8_t irq = (uint8_t)(int_no - 32);
        pic_eoi(irq);   
    }
}

/* NASM isr_stub → isr_common_handler() 호출 */
void isr_common_handler(uint32_t vector, uint32_t error_code, uint64_t *frame) {
    /* Uncomment for IRQ/exception debug noise */
    // serial_printf("[ISR] vector=%u, err=%u\n", vector, error_code);

    // 재진입 예외 방지
    if (handling_exception) {
        panic_handle_s(vector, error_code, frame);
        cli();
        hlt();
        for(;;);
    }

    // 사용자 핸들러
    if (vector < MAX_INTERRUPTS && handlers[vector]) {
        handlers[vector]();
    }

    // IRQ (PIC)
    if (vector >= 0x20 && vector <= 0x2F) {
        pic_eoi(vector - 0x20);
        return;
    }

    // CPU 예외
    if (vector < 32) {
        handling_exception = true;
        panic_handle(vector, error_code, frame); // show panic screen + log
        handling_exception = false;
    }
}
