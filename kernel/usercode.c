#include "serial.h"

void user_mode_start(void) {
    const char *msg = "TEST USER MODE";
    __asm__ volatile(
        "movl $0, %%eax\n"
        "movl %[msg], %%ebx\n"
        "int $0x80\n"
        :
        : [msg]"r"(msg)
        : "eax", "ebx"
    );

    for (;;) __asm__ volatile("hlt");
}