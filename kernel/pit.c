#include "pit.h"
#include "io.h"
#include "isr.h"
#include "pic.h"

#define PIT_CHANNEL0 0x40
#define PIT_COMMAND  0x43
#define PIT_INPUT_HZ 1193182

volatile uint64_t jiffies = 0;

static void pit_irq(void){
    jiffies++;
    /* EOI는 isr_handler_c에서 공용 처리 중이므로 여기서는 생략 가능 */
}

void pit_init(uint32_t hz){
    if (hz == 0) hz = 100;
    uint16_t div = (uint16_t)(PIT_INPUT_HZ / hz);

    outb(PIT_COMMAND, 0x36);            // ch0, lo/hi, mode 3
    outb(PIT_CHANNEL0, div & 0xFF);
    outb(PIT_CHANNEL0, (div >> 8) & 0xFF);

    isr_register_handler(32, pit_irq);  // IRQ0 = vector 32
}
