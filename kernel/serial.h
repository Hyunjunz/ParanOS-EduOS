#pragma once
#include <stdint.h>
#define COM1 0x3F8
void serial_init(uint16_t base);
int  serial_ready_tx(uint16_t base);
void serial_putc(uint16_t base, char c);
void serial_write(uint16_t base, const char* s);
void serial_printf(const char* fmt, ...);
