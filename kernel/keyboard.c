#include "keyboard.h"
#include "isr.h"
#include "io.h"
#include "pic.h"

#define KBD_DATA 0x60

static volatile uint8_t last_sc = 0;
static volatile int     has_sc  = 0;

static void keyboard_callback(void){
    uint8_t sc = inb(KBD_DATA);
    last_sc = sc;
    has_sc  = 1;
}

void keyboard_init(void){
    pic_clear_mask(1);      
    isr_register_handler(33, keyboard_callback); 
}

int kbd_get_scancode(uint8_t* out){
    if (!has_sc) return 0;
    if (out) *out = last_sc;
    has_sc = 0;
    return 1;
}

