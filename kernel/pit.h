#pragma once
#include <stdint.h>
void pit_init(uint32_t hz);
extern volatile uint64_t jiffies;
