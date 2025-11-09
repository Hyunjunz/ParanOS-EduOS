#ifndef DF_TSS_H
#define DF_TSS_H
#include <stdint.h>
#include <tss.h>   // 기본 TSS 구조 재사용

/* 더블 폴트 전용 TSS */
extern tss_t df_tss;

/* 초기화 함수: 전용 스택/진입점 설정 */
void df_tss_init(void);

#endif
