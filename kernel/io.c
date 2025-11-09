#include <stdint.h>

uint32_t read_cr0(void)
{
    uint32_t val;
    __asm__ __volatile__("mov %%cr0, %0" : "=r"(val));
    return val;
}

uint32_t read_cr3(void)
{
    uint32_t val;
    __asm__ __volatile__("mov %%cr3, %0" : "=r"(val));
    return val;
}

uint32_t get_eip(void)
{
    uint32_t eip;
    __asm__ __volatile__(
        "call 1f\n"
        "1: pop %0\n"
        : "=r"(eip)
    );
    return eip;
}

uint32_t get_esp(void)
{
    uint32_t esp;
    __asm__ __volatile__("mov %%esp, %0" : "=r"(esp));
    return esp;
}

static inline void cli(void) {
    __asm__ volatile ("cli");
}

static inline void sti(void) {
    __asm__ volatile ("sti");
}

static inline void hlt(void) {
    __asm__ volatile ("hlt");
}