#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <lib/misc.h>
#include <lib/fb.h>
#include <fs/file.h>
#include <mm/pmm.h>

/* Provide missing libc helpers */
void *memmove(void *dst, const void *src, size_t n) {
    unsigned char *d = (unsigned char *)dst;
    const unsigned char *s = (const unsigned char *)src;
    if (d == s || n == 0) {
        return dst;
    }
    if (d < s) {
        while (n--) {
            *d++ = *s++;
        }
    } else {
        d += n;
        s += n;
        while (n--) {
            *--d = *--s;
        }
    }
    return dst;
}

/* GCC expects this for 64-bit popcount when libgcc is absent. */
uint64_t __popcountdi2(uint64_t x) {
    uint64_t c = 0;
    while (x) {
        x &= (x - 1);
        c++;
    }
    return c;
}

/* Minimal time() stub */
long time(long *t) {
    long now = 0;
    if (t) {
        *t = now;
    }
    return now;
}

/* File helpers */
void file_read(struct file_handle *fd, void *buf, uint64_t loc, uint64_t count) {
    if (fd && fd->read) {
        fd->read(fd->fd, buf, loc, count);
    }
}

/* Boot volume placeholder */
struct volume *boot_volume = NULL;

/* ACPI stubs */
void *acpi_get_rsdp(void) { return NULL; }
void *acpi_get_rsdp_v1(void) { return NULL; }
void *acpi_get_smbios(void) { return NULL; }

/* Graphics terminal stub */
bool gterm_init(struct fb_info **ret, size_t *_fbs_count,
                char *config, size_t width, size_t height) {
    (void)ret; (void)_fbs_count; (void)config; (void)width; (void)height;
    return false;
}

/* Spinup stubs */
void limine_spinup_32(void) {
    for (;;);
}
noreturn void common_spinup(void *fnptr, int args, ...) {
    (void)fnptr; (void)args;
    for (;;);
}
noreturn void linux_spinup(void *entry, void *boot_params) {
    (void)entry; (void)boot_params;
    for (;;);
}
noreturn void multiboot_spinup_32(void *entry_point, void *multiboot_info) {
    (void)entry_point; (void)multiboot_info;
    for (;;);
}

/* PIT helper stub */
int pit_sleep_and_quit_on_keypress(int seconds) {
    (void)seconds;
    return 0;
}

/* SMP trampoline placeholders */
void *smp_trampoline_start = NULL;
size_t smp_trampoline_size = 0;
