#include <stddef.h>
#include <stdarg.h>
#include <stdint.h>


/* ─────────────── 기본 libc 대체 구현 ─────────────── */

void* memset(void* dst, int c, size_t n) {
    unsigned char* p = (unsigned char*)dst;
    while (n--) *p++ = (unsigned char)c;
    return dst;
}

void* memcpy(void* dst, const void* src, size_t n) {
    unsigned char* d = (unsigned char*)dst;
    const unsigned char* s = (const unsigned char*)src;
    while (n--) *d++ = *s++;
    return dst;
}

size_t strlen(const char* s) {
    size_t i = 0;
    while (s[i]) ++i;
    return i;
}

static void itoa_dec(char *buf, unsigned int val) {
    char tmp[16];
    int i = 0;
    if (val == 0) {
        buf[0] = '0';
        buf[1] = '\0';
        return;
    }
    while (val > 0) {
        tmp[i++] = '0' + (val % 10);
        val /= 10;
    }
    for (int j = 0; j < i; j++)
        buf[j] = tmp[i - j - 1];
    buf[i] = '\0';
}

static void itoa_hex(char *buf, unsigned int val) {
    const char *digits = "0123456789ABCDEF";
    char tmp[16];
    int i = 0;
    if (val == 0) {
        buf[0] = '0';
        buf[1] = '\0';
        return;
    }
    while (val > 0) {
        tmp[i++] = digits[val & 0xF];
        val >>= 4;
    }
    for (int j = 0; j < i; j++)
        buf[j] = tmp[i - j - 1];
    buf[i] = '\0';
}

int sprintf(char *buf, const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);

    char *p = buf;
    for (; *fmt; fmt++) {
        if (*fmt != '%') {
            *p++ = *fmt;
            continue;
        }

        fmt++; // skip '%'
        switch (*fmt) {
        case 'c': {
            char c = (char)va_arg(args, int);
            *p++ = c;
            break;
        }
        case 's': {
            const char *s = va_arg(args, const char *);
            while (*s)
                *p++ = *s++;
            break;
        }
        case 'd': {
            int v = va_arg(args, int);
            if (v < 0) {
                *p++ = '-';
                v = -v;
            }
            char tmp[16];
            itoa_dec(tmp, (unsigned)v);
            for (char *t = tmp; *t; t++)
                *p++ = *t;
            break;
        }
        case 'u': {
            unsigned int v = va_arg(args, unsigned int);
            char tmp[16];
            itoa_dec(tmp, v);
            for (char *t = tmp; *t; t++)
                *p++ = *t;
            break;
        }
        case 'x': {
            unsigned int v = va_arg(args, unsigned int);
            char tmp[16];
            itoa_hex(tmp, v);
            for (char *t = tmp; *t; t++)
                *p++ = *t;
            break;
        }
        case '%':
            *p++ = '%';
            break;
        default:
            *p++ = '%';
            *p++ = *fmt;
            break;
        }
    }

    *p = '\0';
    va_end(args);
    return (int)(p - buf);
}

