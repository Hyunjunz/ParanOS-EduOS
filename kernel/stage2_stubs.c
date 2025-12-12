#include <stdint.h>
#include <stdbool.h>
#include <lib/real.h>
#include <lib/misc.h>
#include <lib/part.h>
#include <lib/fb.h>
#include <fs/file.h>

// Stage2 BIOS placeholders so the 64-bit kernel can link
bool stage3_loaded = false;
void *full_map = NULL;
void *stage2_map = NULL;
struct volume *boot_volume = NULL;

void rm_int(uint8_t int_no, struct rm_regs *out_regs, struct rm_regs *in_regs) {
    (void)int_no; (void)out_regs; (void)in_regs;
}

void rm_hcf(void) {
    panic(false, "rm_hcf stub called");
}

void set_pxe_fp(void) {}
int pxe_call(uint16_t func, uint16_t seg, uint16_t off) {
    (void)func; (void)seg; (void)off;
    return -1;
}

void _pit_sleep_and_quit_on_keypress(void) {}

// Multiboot relocation stubs
void multiboot_reloc_stub(void) {}
void multiboot_reloc_stub_end(void) {}

// Spin-up stubs
void common_spinup(void *fnptr, int args, ...) {
    (void)fnptr; (void)args;
    panic(false, "common_spinup stub called");
}

void limine_spinup_32(void) { panic(false, "limine_spinup_32 stub"); }
void multiboot_spinup_32(void) { panic(false, "multiboot_spinup_32 stub"); }
void linux_spinup(void) { panic(false, "linux_spinup stub"); }

size_t smp_trampoline_size = 0;
void *smp_trampoline_start = NULL;

// Misc helpers expected by callers
void file_read(struct file_handle *fd, void *buf, uint64_t loc, uint64_t count) {
    (void)fd; (void)buf; (void)loc; (void)count;
}

bool gterm_init(struct fb_info **ret, size_t *_fbs_count,
                char *config, size_t width, size_t height) {
    (void)ret; (void)_fbs_count; (void)config; (void)width; (void)height;
    return false;
}

uint64_t __popcountdi2(uint64_t x) {
    uint64_t c = 0;
    while (x) {
        x &= (x - 1);
        c++;
    }
    return c;
}
