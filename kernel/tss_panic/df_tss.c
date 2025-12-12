#include <stdint.h>
#include "gdt.h"

/*
 * Double Fault 전용 IST 스택을 설정하는 코드.
 *  - IST1 에 스택을 박아둠
 *  - IDT 에서 DF 게이트의 IST 인덱스를 1로 설정해주면
 *    Double Fault 발생 시 이 스택으로 자동 전환됨.
 */

#define DF_STACK_SIZE  (0x2000)

static uint8_t df_stack[DF_STACK_SIZE] __attribute__((aligned(16)));

void df_tss_init(void) {
    uint64_t top = (uint64_t)df_stack + DF_STACK_SIZE;
    tss_set_df_ist(top);
}