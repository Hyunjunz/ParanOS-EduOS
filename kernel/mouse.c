#include <stdint.h>
#include "pic.h"
#include "isr.h"
#include "serial.h"
#include "fb.h"
#include "mouse.h"

#define MOUSE_IRQ 12
#define MOUSE_PORT 0x60
#define MOUSE_CMD  0x64

static int mouse_cycle = 0;
static int8_t mouse_bytes[3];
static int mouse_x = 0, mouse_y = 0;
static int screen_w = 0, screen_h = 0;

static inline void outb(uint16_t port, uint8_t val)
{ __asm__ __volatile__("outb %0,%1"::"a"(val),"Nd"(port)); }
static inline uint8_t inb(uint16_t port)
{ uint8_t r; __asm__ __volatile__("inb %1,%0":"=a"(r):"Nd"(port)); return r; }

static void mouse_wait(uint8_t type)
{
    uint32_t timeout = 100000;
    if (type == 0) { // wait to write
        while (timeout-- && (inb(0x64) & 2));
    } else {
        while (timeout-- && !(inb(0x64) & 1));
    }
}

static void mouse_write(uint8_t val)
{
    mouse_wait(0);
    outb(0x64, 0xD4);
    mouse_wait(0);
    outb(0x60, val);
}

static uint8_t mouse_read(void)
{
    mouse_wait(1);
    return inb(0x60);
}

void mouse_init(uint32_t fb_w, uint32_t fb_h)
{
    screen_w = fb_w;
    screen_h = fb_h;

    outb(0x64, 0xA8);      // Enable mouse port
    mouse_wait(0);
    outb(0x64, 0x20);      // Read command byte
    mouse_wait(1);
    uint8_t status = inb(0x60);
    status |= 2;           // Enable mouse IRQ (bit1)
    mouse_wait(0);
    outb(0x64, 0x60);
    mouse_wait(0);
    outb(0x60, status);

    mouse_write(0xF6); mouse_read(); // default settings
    mouse_write(0xF4); mouse_read(); // enable data reporting

    mouse_x = fb_w / 2;
    mouse_y = fb_h / 2;

    serial_printf("[mouse] Initialized at (%d,%d)\n", mouse_x, mouse_y);

    // Register IRQ12 (vector 32+12=44) handler via common ISR dispatcher
    isr_register_handler(32 + 12, mouse_irq_handler);
}

void mouse_irq_handler(void)
{
    uint8_t data = inb(0x60);

    // Sync to packet start: first byte must have bit 3 set
    if (mouse_cycle == 0 && !(data & 0x08))
        return;

    mouse_bytes[mouse_cycle++] = data;
    if (mouse_cycle == 3)
    {
        mouse_cycle = 0;

        // Keep X as reported, invert Y direction only
        int dx = (int8_t)mouse_bytes[1];
        int dy = -(int8_t)mouse_bytes[2];
        mouse_x += dx;
        mouse_y += dy;

        if (mouse_x < 0) mouse_x = 0;
        if (mouse_y < 0) mouse_y = 0;
        if (mouse_x >= screen_w) mouse_x = screen_w - 1;
        if (mouse_y >= screen_h) mouse_y = screen_h - 1;
    }
    // EOI is sent by the common IRQ handler (isr_common_handler)
}

int mouse_get_x(void) { return mouse_x; }
int mouse_get_y(void) { return mouse_y; }
uint8_t mouse_get_buttons(void) { return (uint8_t)(mouse_bytes[0] & 0x07); }
