#include "syscall.h"
#include "idt.h"
#include "serial.h"

#ifndef COM1
#define COM1 0x3F8
#endif


extern void isr_stub80(void);
void keyboard_handler(void) {

}

// kernel/syscall.c
#include <stdint.h>

void syscall_handler(uint32_t num, uint32_t a, uint32_t b, uint32_t c, uint32_t d) {
    (void)b;
    (void)c;
    (void)d;
    switch (num)
    {
    case 0:
        serial_printf("[syscall] write %s\n", (char*)(uintptr_t)a);
        break;
    
    default:
        break;
    }
}

void isr_syscall(void* frame) {
    (void)frame;
    uint32_t num, a, b, c, d;
    __asm__ volatile (
        "movl %%eax, %0\n"
        "movl %%ebx, %1\n"
        "movl %%ecx, %2\n"
        "movl %%edx, %3\n"
        : "=r"(num), "=r"(a), "=r"(b), "=r"(c), "=r"(d)
    );

    syscall_handler(num, a, b, c, d);
}




void syscall_init(void){
    idt_set_gate(0x80, (uint64_t)isr_syscall, 0x08, 0xEE, 0);

    serial_printf("[syscall] int 0x80 handler installed.\n"); 
}


void syscall_dispatch(uint32_t eax, uint32_t ebx, uint32_t ecx, uint32_t edx){
    (void)ecx; (void)edx;
    if (eax == 1){
        char buf[64];
        const char H[]="0123456789ABCDEF";
        buf[0]='[';buf[1]='s';buf[2]='y';buf[3]='s';buf[4]='c';buf[5]='a';buf[6]='l';buf[7]=']';buf[8]=' ';
        buf[9]='E';buf[10]='B';buf[11]='X';buf[12]='='; 
        for(int i=0;i<8;i++){ buf[20-1-i]=H[(ebx>>(i*4))&0xF]; }
        buf[20]='\r'; buf[21]='\n'; buf[22]=0;
        serial_write(COM1, buf);
    }
}
