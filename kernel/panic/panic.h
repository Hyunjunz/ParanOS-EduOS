#pragma once
#include <stdint.h>

typedef struct {
    uint32_t vector;
    uint32_t err_code;
    uint32_t cr2;
    uint32_t eip;
    uint32_t cs;
    uint32_t eflags;
} panic_info_t;

extern volatile panic_info_t g_panic_info;
extern volatile int g_panic_triggered;

void panic_handle(uint32_t vector, uint32_t err_code);
void panic_show(void);
void super_panic(void);
void panic_handle_s(uint32_t vector, uint32_t err_code);