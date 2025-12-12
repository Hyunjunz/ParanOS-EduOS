#include <stdint.h>

struct idt_ptr {
    uint16_t limit;
    uint64_t base;
} __attribute__((packed));

static uint8_t dummy_idt[256 * 16] __attribute__((aligned(16)));

static struct idt_ptr idt_desc = {
    .limit = sizeof(dummy_idt) - 1,
    .base = (uint64_t)dummy_idt
};

void idt_init64_d(void) {
    __asm__ volatile("lidt %0" : : "m"(idt_desc));
}