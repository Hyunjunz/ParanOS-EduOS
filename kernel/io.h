#pragma once
#include <stdint.h>

static inline void outb(uint16_t port, uint8_t val){
    __asm__ volatile ("outb %0, %1" :: "a"(val), "Nd"(port));
}
static inline uint8_t inb(uint16_t port){
    uint8_t ret;
    __asm__ volatile ("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}
static inline void io_wait(void){
    /* 0x80 dummy 포트 접근으로 약간의 지연 */
    __asm__ volatile ("outb %%al, $0x80" :: "a"(0));
}
uint32_t read_cr0(void);
uint32_t read_cr3(void);
uint32_t get_eip(void);
uint32_t get_esp(void);
uint32_t virt_to_phys(uint32_t *pgdir, uint32_t vaddr);


static inline void cli(void) { __asm__ volatile ("cli"); }
static inline void sti(void) { __asm__ volatile ("sti"); }
static inline void hlt(void) { __asm__ volatile ("hlt"); }
