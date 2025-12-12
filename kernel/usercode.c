#include "serial.h"

void user_mode_start(void) {
    const char *msg = "TEST USER MODE\n";

    serial_printf("%s", msg);

    while (1) {
        __asm__ volatile("hlt");
    }
}
