#ifndef _STDLIB_H
#define _STDLIB_H

#include <stddef.h>
#include <stdint.h>

/*
 * 커널 환경에서는 malloc/free 를 보통 자체 메모리 관리자에서 구현합니다.
 * 여기서는 프로토타입만 선언해 두고,
 * 실제 구현은 kmalloc/kfree 등 커널 코드에서 작성하세요.
 */

void *malloc(size_t size);
void free(void *ptr);
void abort(void) __attribute__((noreturn));

/* 문자열 → 숫자 변환 함수들 (필요한 경우 직접 구현) */
static inline long atoi(const char *str) {
    long result = 0;
    int sign = 1;

    if (*str == '-') {
        sign = -1;
        str++;
    }

    while (*str >= '0' && *str <= '9') {
        result = result * 10 + (*str - '0');
        str++;
    }

    return result * sign;
}

static inline unsigned long strtoul(const char *nptr, char **endptr, int base) {
    const char *s = nptr;
    unsigned long acc = 0;

    while (*s == ' ') s++;

    while (1) {
        int c = *s;
        int v;

        if (c >= '0' && c <= '9') v = c - '0';
        else if (c >= 'A' && c <= 'Z') v = c - 'A' + 10;
        else if (c >= 'a' && c <= 'z') v = c - 'a' + 10;
        else break;

        if (v >= base) break;

        acc = acc * base + v;
        s++;
    }

    if (endptr) *endptr = (char *)s;

    return acc;
}

#endif
