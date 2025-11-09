#pragma once
#include <stdint.h>

void pic_remap(void);
void pic_clear_mask(uint8_t irq);
void pic_set_mask(uint8_t irq);
void pic_send_eoi(uint8_t irq);
void pic_eoi(uint8_t irq);