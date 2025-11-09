#pragma once
#include <stdint.h>
void keyboard_init(void);
/* optional: 폴링 API */
int kbd_get_scancode(uint8_t* out);
