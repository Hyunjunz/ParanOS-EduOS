#include <stdbool.h>
#include <pic.h>
#include <io.h>

// Flush all pending PIC IRQs so LAPIC-only mode works reliably
void pic_flush(void) {
    for (int i = 0; i < 16; i++) {
        pic_eoi(i);
    }
}

// Mask every PIC line (used by the Limine protocol bring-up)
void pic_mask_all(void) {
    outb(0xa1, 0xff);
    outb(0x21, 0xff);
}
