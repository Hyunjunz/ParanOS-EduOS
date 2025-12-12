#include "rtc.h"

#define CMOS_ADDR 0x70
#define CMOS_DATA 0x71

static inline void outb(uint16_t port, uint8_t val) {
    __asm__ __volatile__("outb %0,%1" :: "a"(val), "Nd"(port));
}

static inline uint8_t inb(uint16_t port) {
    uint8_t v;
    __asm__ __volatile__("inb %1,%0" : "=a"(v) : "Nd"(port));
    return v;
}

static inline uint8_t cmos_read(uint8_t reg) {
    outb(CMOS_ADDR, (uint8_t)(reg | 0x80)); // NMI 비활성화
    return inb(CMOS_DATA);
}

static inline int rtc_update_in_progress(void) {
    return (cmos_read(0x0A) & 0x80) != 0;
}

static inline uint8_t bcd2bin(uint8_t x) {
    return (uint8_t)((x & 0x0F) + ((x >> 4) * 10));
}

void rtc_read_time(rtc_time_t *t) {
    while (rtc_update_in_progress())
        __asm__ __volatile__("pause");

    uint8_t sec = cmos_read(0x00);
    uint8_t min = cmos_read(0x02);
    uint8_t hour = cmos_read(0x04);
    uint8_t regB = cmos_read(0x0B);

    int bin = (regB & 0x04) != 0;
    int is24 = (regB & 0x02) != 0;

    if (!bin) {
        sec = bcd2bin(sec);
        min = bcd2bin(min);
        hour = bcd2bin(hour);
    }

    if (!is24) {
        int pm = (hour & 0x80) != 0;
        hour &= 0x7F;
        if (pm && hour < 12) hour += 12;
        if (!pm && hour == 12) hour = 0;
    }

    t->hh = hour % 24;
    t->mm = min % 60;
    t->ss = sec % 60;
}

void rtc_format(char *buf, const rtc_time_t *t) {
    buf[0] = '0' + (t->hh / 10);
    buf[1] = '0' + (t->hh % 10);
    buf[2] = ':';
    buf[3] = '0' + (t->mm / 10);
    buf[4] = '0' + (t->mm % 10);
    buf[5] = ':';
    buf[6] = '0' + (t->ss / 10);
    buf[7] = '0' + (t->ss % 10);
    buf[8] = 0;
}
