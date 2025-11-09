#pragma once
#include <stdint.h>
void syscall_init(void);      
void syscall_dispatch(uint32_t eax, uint32_t ebx, uint32_t ecx, uint32_t edx);
void syscall_handler(uint32_t num, uint32_t a, uint32_t b, uint32_t c, uint32_t d);