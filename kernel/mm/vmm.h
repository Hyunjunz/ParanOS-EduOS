// kernel/mm/vmm.h
#pragma once
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 페이징 엔트리 플래그 (x86 32-bit) */
enum {
    VMM_P   = 1u << 0,  /* Present */
    VMM_RW  = 1u << 1,  /* Read/Write */
    VMM_US  = 1u << 2,  /* User/Supervisor (1=user) */
    VMM_PWT = 1u << 3,  /* Page-level Write-Through */
    VMM_PCD = 1u << 4,  /* Page-level Cache Disable */
    VMM_A   = 1u << 5,  /* Accessed */
    VMM_D   = 1u << 6,  /* Dirty (PT entry only) */
    VMM_PS  = 1u << 7,  /* Page Size (PD entry only, 0 for 4KiB) */
    VMM_G   = 1u << 8,  /* Global (PT entry only if CR4.PGE) */
};

/* 커널 가상 베이스 (하이하프). 기존 링커/부트 설정에 맞추어 조정 */
#ifndef KERNEL_BASE
#define KERNEL_BASE 0xC0000000u
#endif

/* 초기화: CR3 재적재(옵션), 페이지 폴트 핸들러 등록(선택) */
void vmm_init(void);

/* 기본 맵/언맵/조회 API */
int  vmm_map(uintptr_t virt, uintptr_t phys, uint32_t flags);
int  vmm_unmap(uintptr_t virt);
int  vmm_query(uintptr_t virt, uintptr_t* phys_out, uint32_t* flags_out);

/* 프레임을 새로 할당해서 가상주소에 맵핑 (pmm_alloc 사용) */
void* vmm_alloc_page(uintptr_t virt, uint32_t flags);

/* TLB 관련 */
void vmm_invlpg(void* addr);
void vmm_reload_cr3(void);

/* (선택) 페이지 폴트 핸들러 — 당신의 ISR 디스패처에서 호출만 해주면 됨 */
void vmm_page_fault_handler(uint32_t errcode, uintptr_t cr2);

#ifdef __cplusplus
}
#endif
