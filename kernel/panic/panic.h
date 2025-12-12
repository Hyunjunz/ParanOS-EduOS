#pragma once
#include <stdint.h>

typedef struct {
    uint64_t vector;
    uint64_t err_code;
    uint64_t cr2;
    uint64_t eip;
    uint64_t cs;
    uint64_t eflags;
    uint64_t rsp;
    uint64_t ss;
} panic_info_t;

extern volatile panic_info_t g_panic_info;
extern volatile int g_panic_triggered;

void panic_handle(uint32_t vector, uint32_t err_code, const uint64_t *frame);
void panic_show(void);
void super_panic(void);
void panic_handle_s(uint32_t vector, uint32_t err_code, const uint64_t *frame);
