#pragma once
#include <stdint.h>

// 64-bit IDT entry (16 bytes)
typedef struct {
    uint16_t offset_low;   // handler[15:0]
    uint16_t selector;     // code segment selector (e.g. 0x08)
    uint8_t  ist;          // IST index (0–7). Upper 5 bits must be 0.
    uint8_t  type_attr;    // gate type, DPL, present (e.g. 0x8E)
    uint16_t offset_mid;   // handler[31:16]
    uint32_t offset_high;  // handler[63:32]
    uint32_t zero;         // reserved, must be 0
} __attribute__((packed)) idt_entry_t;

// IDTR descriptor
typedef struct {
    uint16_t limit;
    uint64_t base;
} __attribute__((packed)) idt_ptr_t;

// Set a gate in the IDT.
//  n        : vector number (0–255)
//  handler  : 64-bit address of ISR/IRQ stub
//  sel      : code segment selector
//  type_attr: type + flags (0x8E = interrupt gate, 0x8F = trap gate, etc.)
//  ist      : IST index (0 if not using IST)
void idt_set_gate(int n, uint64_t handler,
                  uint16_t sel, uint8_t type_attr, uint8_t ist);

// Initialize 64-bit IDT and load it with lidt.
void idt_init64(void);

// ASM side: loads IDTR from the global idtp symbol.
void idt_load(void);

// Optional: old name kept for compatibility, if other code still calls it.
static inline void idt_install_core(void) {
    idt_init64();
}
