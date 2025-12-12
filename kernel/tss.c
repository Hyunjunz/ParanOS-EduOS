// kernel/tss.c
#include <stdint.h>
#include "gdt.h"   // struct tss64, extern struct tss64 g_tss;
#include "tss.h"

/*
 * Long mode TSS 초기화.
 * gdt_init() 안에서 기본적인 초기화는 이미 해주고 있으니
 * 여기서는 필요한 추가 세팅만 합니다.
 */
void tss_init_fields(void) {
    // I/O bitmap 오프셋: TSS 구조체 끝으로 설정
    g_tss.iopb_offset = sizeof(struct tss64);
}

/*
 * 스케줄러(task.c)에서 쓰는 커널 스택 설정 함수.
 * 현재 실행 중인 태스크에 맞게 RSP0를 바꿉니다.
 */
void tss_set_kernel_stack(uint64_t stack_top) {
    g_tss.rsp0 = stack_top;
}
