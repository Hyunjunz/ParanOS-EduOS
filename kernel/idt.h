#pragma once
#include <stdint.h>

typedef struct {
    uint16_t offset_low;
    uint16_t selector;
    uint8_t  always0;
    uint8_t  flags;
    uint16_t offset_high;
} __attribute__((packed)) idt_entry_t;

typedef struct {
    uint16_t limit;
    uint32_t base;
} __attribute__((packed)) idt_ptr_t;

void idt_set_gate(int n, uint32_t handler, uint16_t sel, uint8_t type_attr);
void idt_load(void);
void idt_install_core(void);
