#include "serial.h"
#include "io.h"
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>

#ifndef COM1
#define COM1 0x3F8
#endif

static inline void serial_wait_tx(uint16_t base) {
    // LSR(5) bit5=1 → THR empty (전송 가능)
    while ((inb(base + 5) & 0x20) == 0) { }
}

void serial_init(uint16_t base) {
    outb(base + 1, 0x00);      // Disable interrupts
    outb(base + 3, 0x80);      // Enable DLAB
    outb(base + 0, 0x03);      // Divisor low  (115200/3=38400)
    outb(base + 1, 0x00);      // Divisor high
    outb(base + 3, 0x03);      // 8N1, DLAB=0
    outb(base + 2, 0xC7);      // Enable FIFO, clear, 14-byte threshold
    outb(base + 4, 0x0B);      // OUT2|OUT1|DTR|RTS (IRQ 라우팅/모뎀 제어)
}

void serial_putc(uint16_t base, char c) {
    if (c == '\n') serial_putc(base, '\r');
    serial_wait_tx(base);
    outb(base + 0, (uint8_t)c);
}

void serial_puts(uint16_t base, const char* s) {
    while (*s) serial_putc(base, *s++);
}

void serial_write(uint16_t base, const char* s) {
    serial_puts(base, s);
}

static void u32_to_dec(uint32_t v, char* buf) {
    char t[11]; int n=0; if (!v) { buf[0]='0'; buf[1]=0; return; }
    while (v) { t[n++] = '0' + (v%10); v/=10; }
    for (int i=0;i<n;i++) buf[i]=t[n-1-i];
    buf[n]=0;
}
static void u32_to_hex(uint32_t v, char* buf, int width, int upper) {
    const char* H = upper ? "0123456789ABCDEF" : "0123456789abcdef";
    char t[8]; for (int i=0;i<8;i++){ t[7-i]=H[v&0xF]; v>>=4; }
    int start = 8-width; if (start<0) start=0;
    int j=0; for (int i=start;i<8;i++) buf[j++]=t[i];
    buf[j]=0;
}

static void u64_to_hex(uint64_t v, char *buf, int width, int upper) {
    const char *H = upper ? "0123456789ABCDEF" : "0123456789abcdef";
    char t[16];
    for (int i = 0; i < 16; i++) {
        t[15 - i] = H[v & 0xF];
        v >>= 4;
    }
    int start = 16 - width;
    if (start < 0) start = 0;
    int j = 0;
    for (int i = start; i < 16; i++) buf[j++] = t[i];
    buf[j] = 0;
}

static void u64_to_dec(uint64_t v, char *buf) {
    char t[21];
    int n = 0;
    if (!v) { buf[0] = '0'; buf[1] = 0; return; }
    while (v) { t[n++] = '0' + (v % 10); v /= 10; }
    for (int i = 0; i < n; i++) buf[i] = t[n - 1 - i];
    buf[n] = 0;
}

void serial_printf(const char* fmt, ...) {
    va_list ap; va_start(ap, fmt);
    for (const char* p=fmt; *p; ++p) {
        if (*p != '%') { serial_putc(COM1, *p); continue; }
        ++p;
        int zero_pad = 0, width = 0, upper=1;
        int len_mod = 0; // 0=default(int),1=long,2=long long,3=size_t
        if (*p=='0') { zero_pad=1; ++p; }
        while (*p>='0' && *p<='9') { width = width*10 + (*p-'0'); ++p; }
        if (*p=='l') { len_mod = 1; ++p; if (*p=='l') { len_mod = 2; ++p; } }
        else if (*p=='z') { len_mod = 3; ++p; }

        char tmp[32];
        switch (*p) {
            case 'c': {
                char c=(char)va_arg(ap,int);
                serial_putc(COM1,c);
            } break;
            case 's': {
                const char* s=va_arg(ap,const char*);
                serial_puts(COM1,s?s:"(null)");
            } break;
            case 'u': {
                uint64_t v = 0;
                if (len_mod == 2) v = va_arg(ap, unsigned long long);
                else if (len_mod == 1 || len_mod == 3) v = va_arg(ap, unsigned long);
                else v = va_arg(ap, unsigned int);
                u64_to_dec(v,tmp);
                int len=0; while(tmp[len]) len++;
                for (int i=len;i<width;i++) serial_putc(COM1, zero_pad?'0':' ');
                serial_puts(COM1,tmp);
            } break;
            case 'x': case 'X': {
                upper = (*p=='X');
                uint64_t v = 0;
                if (len_mod == 2) v = va_arg(ap, unsigned long long);
                else if (len_mod == 1 || len_mod == 3) v = va_arg(ap, unsigned long);
                else v = va_arg(ap, unsigned int);
                if (width==0) width = (len_mod >= 1) ? 16 : 8;
                u64_to_hex(v,tmp,width,upper);
                int len=0; while(tmp[len]) len++;
                for (int i=len;i<width;i++) serial_putc(COM1,'0');
                serial_puts(COM1,tmp);
            } break;
            case 'p': {
                uint64_t v = (uint64_t)(uintptr_t)va_arg(ap, void *);
                if (width == 0) width = 16;
                u64_to_hex(v, tmp, width, 0);
                int len = 0; while (tmp[len]) len++;
                for (int i = len; i < width; i++) serial_putc(COM1, '0');
                serial_puts(COM1, tmp);
            } break;
            case 'd': {
                int64_t v;
                if (len_mod == 2) v = va_arg(ap, long long);
                else if (len_mod == 1 || len_mod == 3) v = va_arg(ap, long);
                else v = va_arg(ap, int);
                uint64_t uv;
                if (v < 0) {
                    serial_putc(COM1, '-');
                    uv = (uint64_t)(-v);
                } else {
                    uv = (uint64_t)v;
                }
                u64_to_dec(uv, tmp);
                int len = 0; while (tmp[len]) len++;
                for (int i = len; i< width; i++)
                    serial_putc(COM1, zero_pad ? '0' : ' ');
                serial_puts(COM1, tmp);
            } break;
            case '%':
                serial_putc(COM1,'%');
                break;
            default:
                serial_putc(COM1,'%'); serial_putc(COM1,*p);
                break;
        }
    }
    va_end(ap);
}

static inline void serial_putc_raw(char c) {
    while (!(inb(0x3F8 + 5) & 0x20)); // wait for empty
    outb(0x3F8, c);
}

static inline void serial_puts_raw(const char *s) {
    while (*s) serial_putc_raw(*s++);
}
