#ifndef RTC_H
#define RTC_H

#include <stdint.h>

typedef struct {
    uint8_t hh, mm, ss;
} rtc_time_t;

/* CMOS RTC 읽기 */
void rtc_read_time(rtc_time_t *t);

/* HH:MM:SS 문자열로 변환 (버퍼는 최소 9바이트) */
void rtc_format(char *buf, const rtc_time_t *t);

#endif
