#pragma once
#include <stdint.h>
#include <stddef.h>

#define PAGE_SIZE 4096u
#define BITMAP_MAX_PAGES (1024u * 1024u) /* 4GiB / 4KiB */

/* 부트로더가 채워둔 부트 정보 (stage2) */
typedef struct {
    uint32_t e820_ptr;   /* physical pointer */
    uint32_t e820_count;
} BootInfo;

typedef struct {
    uint64_t base;
    uint64_t length;
    uint32_t type;       /* 1 = Usable RAM */
    uint32_t attr;       /* ACPI 3.0 extended attributes */
} __attribute__((packed)) E820Entry;

#ifdef __cplusplus
extern "C" {
#endif

/* PMM 초기화: bitmap을 heap_top(가상)부터 사용 */
void     pmm_init(void* heap_top);

/* 물리 페이지 한 장(4KiB) 할당/해제 */
void*    pmm_alloc(void);
uint32_t pmm_alloc_phys(void);
void     pmm_free_page(void* phys_addr);

/* 예약/해제(범위) — 페이지 경계 단위 */
void     pmm_reserve_range(uintptr_t phys_begin, uintptr_t phys_end);
void     pmm_release_range(uintptr_t phys_begin, uintptr_t phys_end);

/* 통계 */
uint32_t pmm_free_count(void);
uint32_t pmm_total_count(void);

#ifdef __cplusplus
}
#endif

void* pmm_alloc_low_4m(void);
