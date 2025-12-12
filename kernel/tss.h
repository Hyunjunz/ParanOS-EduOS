#pragma once
#include <stdint.h>

void tss_init_fields(void);
void df_tss_init(void);

/* Long mode에서 커널 스택(RSP0) 바꿔줄 함수 */
void tss_set_kernel_stack(uint64_t stack_top);
