#include "pmm.h"
#include "serial.h"
#include <stdint.h>
#include <stddef.h>

/* ------------------------------------------
   환경 심볼 (stage2 / linker가 제공)
   ------------------------------------------ */
#define BOOTINFO_ADDR 0x0009F000u
#define PAGE_SIZE 4096
#define LOW4M_LIMIT 0x00400000 
static uintptr_t low_next = 0x00100000;

uint32_t *pgdir = NULL;
/* linker.ld에서 제공 (권장 심볼) */
extern char __kernel_phys_start[], __kernel_phys_end[]; /* LMA 범위 전체 */
extern char __text_lma[];            /* 텍스트 LMA 시작 (진단용) */
extern char __kernel_high_end[];     /* VMA 끝 (진단/참고용) */

/* ------------------------------------------
   내부 상태
   ------------------------------------------ */
static uint8_t*  bitmap = NULL;      /* 비트맵의 '가상' 시작 주소(하이하프) */
static uint32_t  total_pages = 0;    /* usable로 판정된 총 페이지 수 */
static uint32_t  free_pages  = 0;    /* 현재 free 페이지 수  */
static uint32_t  rover_index = 0;    /* next-fit 스캔 시작 위치 */

/* ------------------------------------------
   비트 매크로 / 유틸
   ------------------------------------------ */
#ifndef PAGE_SIZE
#define PAGE_SIZE 4096u
#endif

#ifndef BITMAP_MAX_PAGES
/* QEMU -m 64 ~ 256 기준 넉넉히. 필요시 Makefile/헤더에서 조정 */
#define BITMAP_MAX_PAGES (256u * 1024u * 1024u / PAGE_SIZE) /* 256MiB */
#endif

#define BIT_SET(a,i)   ((a)[(i) >> 3] |=  (uint8_t)(1u << ((i) & 7u)))
#define BIT_CLR(a,i)   ((a)[(i) >> 3] &= (uint8_t)~(1u << ((i) & 7u)))
#define BIT_TEST(a,i) (((a)[(i) >> 3] >>      ((i) & 7u)) & 1u)

static inline uintptr_t align_up(uintptr_t v, uintptr_t a){ return (v + (a-1)) & ~(a-1); }
static inline uintptr_t align_dn(uintptr_t v, uintptr_t a){ return  v & ~(a-1); }

static inline uint32_t  addr_to_page(uintptr_t pa){ return (uint32_t)(pa >> 12); }
static inline uintptr_t page_to_addr(uint32_t pg){ return ((uintptr_t)pg) << 12; }

/* BootInfo / E820 구조체가 프로젝트에 이미 있다면 아래는 생략하세요. */
#ifndef PMM_HAS_E820_TYPES
#endif

/* ------------------------------------------
   예약/해제(범위)
   ------------------------------------------ */
void pmm_reserve_range(uintptr_t phys_begin, uintptr_t phys_end)
{
    if (!bitmap) return;
    if (phys_end <= phys_begin) return;

    uint32_t s = addr_to_page(align_dn(phys_begin, PAGE_SIZE));
    uint32_t e = addr_to_page(align_up (phys_end,   PAGE_SIZE));
    if (e > BITMAP_MAX_PAGES) e = BITMAP_MAX_PAGES;

    for (uint32_t p = s; p < e; ++p) {
        if (!BIT_TEST(bitmap, p)) {
            BIT_SET(bitmap, p);
            if (free_pages) free_pages--;
        }
    }
}

void pmm_release_range(uintptr_t phys_begin, uintptr_t phys_end)
{
    if (!bitmap) return;
    if (phys_end <= phys_begin) return;

    uint32_t s = addr_to_page(align_dn(phys_begin, PAGE_SIZE));
    uint32_t e = addr_to_page(align_up (phys_end,   PAGE_SIZE));
    if (e > BITMAP_MAX_PAGES) e = BITMAP_MAX_PAGES;

    for (uint32_t p = s; p < e; ++p) {
        if (BIT_TEST(bitmap, p)) {
            BIT_CLR(bitmap, p);
            free_pages++;
        }
    }
}

/* ------------------------------------------
   초기화
   ------------------------------------------ */
void pmm_init(void* heap_top)
{
    /* 비트맵 메모리 배치(하이하프 가상) */
    bitmap = (uint8_t*)align_up((uintptr_t)heap_top, 8);

    /* 전체를 예약(=1)으로 초기화 */
    const uint32_t bitmap_bytes = BITMAP_MAX_PAGES / 8;
    for (uint32_t i=0; i<bitmap_bytes; ++i) bitmap[i] = 0xFF;

    total_pages = 0;
    free_pages  = 0;

    /* ───────── e820 기반 usable 해제(=0) ───────── */
    BootInfo*  bi   = (BootInfo*)BOOTINFO_ADDR;        /* 이 포인터는 1MiB 아래여야 안전 */
    uint32_t   cnt  = 0;
    E820Entry* e820 = NULL;

    if (bi && bi->e820_ptr && bi->e820_count) {
        /* e820_ptr는 '물리' 포인터일 가능성이 큼 → 아이덴티티 매핑 전제 */
        e820 = (E820Entry*)(uintptr_t)bi->e820_ptr;
        cnt  = bi->e820_count;
    }

    if (e820 && cnt) {
        for (uint32_t i=0; i<cnt; ++i) {
            if (e820[i].type != 1) continue;  /* usable only */

            uint64_t start = e820[i].base;
            uint64_t end   = start + e820[i].length;

            uint64_t s_al = align_dn((uintptr_t)start, PAGE_SIZE);
            uint64_t e_al = align_up ((uintptr_t)end,   PAGE_SIZE);

            uint32_t s_pg = addr_to_page((uintptr_t)s_al);
            uint32_t e_pg = addr_to_page((uintptr_t)e_al);
            if (e_pg > BITMAP_MAX_PAGES) e_pg = BITMAP_MAX_PAGES;

            for (uint32_t p = s_pg; p < e_pg; ++p) {
                if (BIT_TEST(bitmap, p)) {
                    BIT_CLR(bitmap, p); /* free */
                    total_pages++;
                    free_pages++;
                }
            }
        }
    } else {
        /* ── 폴백: e820이 없을 때 64MiB 가정 ── */
        total_pages = free_pages = (64u * 1024u * 1024u) / PAGE_SIZE;
        if (total_pages > BITMAP_MAX_PAGES) total_pages = free_pages = BITMAP_MAX_PAGES;

        /* 전부 free로 표기 */
        for (uint32_t p = 0; p < total_pages; ++p) BIT_CLR(bitmap, p);
    }

    /* ───────── 반드시 예약해야 하는 영역들 ───────── */

    /* 저 1MiB(부트/BIOS 공간) */
    pmm_reserve_range(0x00000000u, 0x00100000u);

    /* 커널 물리(LMA) 전체 */
    uintptr_t k_lma_start = (uintptr_t)__kernel_phys_start;
    uintptr_t k_lma_end   = (uintptr_t)__kernel_phys_end;
    if (k_lma_end > k_lma_start)
        pmm_reserve_range(k_lma_start, k_lma_end);

    /* 비트맵 자신이 점유하는 '물리' 구간도 예약 (중요!!) */
    #ifndef KERNEL_BASE
    #define KERNEL_BASE 0xC0000000u
    #endif
    uintptr_t bm_phys = (uintptr_t)bitmap - KERNEL_BASE; /* 하이하프→물리 */
    pmm_reserve_range(bm_phys, bm_phys + align_up(bitmap_bytes, PAGE_SIZE));

    /* next-fit 시작점은 커널 위쪽부터 찾도록 살짝 올려도 됨 */
    rover_index = addr_to_page(k_lma_end);
}

/* ------------------------------------------
   할당/해제
   ------------------------------------------ */
void* pmm_alloc(void)
{
    if (!bitmap || free_pages == 0) return NULL;

    uint32_t start = rover_index;
    /* 1차: rover부터 끝까지 */
    for (uint32_t i = start; i < BITMAP_MAX_PAGES; ++i) {
        if (!BIT_TEST(bitmap, i)) {
            BIT_SET(bitmap, i);
            free_pages--;
            rover_index = i + 1;
            return (void*)page_to_addr(i);
        }
    }
    /* 2차: 0부터 rover 직전까지 */
    for (uint32_t i = 0; i < start; ++i) {
        if (!BIT_TEST(bitmap, i)) {
            BIT_SET(bitmap, i);
            free_pages--;
            rover_index = i + 1;
            return (void*)page_to_addr(i);
        }
    }
    return NULL;
}

void pmm_free(void* phys_addr)
{
    if (!bitmap || !phys_addr) return;
    uintptr_t pa = (uintptr_t)phys_addr;

    if (pa & (PAGE_SIZE - 1)) {
        /* 정렬되지 않은 주소는 무시(혹은 assert/로그) */
        return;
    }
    uint32_t idx = addr_to_page(pa);
    if (idx >= BITMAP_MAX_PAGES) return;

    if (BIT_TEST(bitmap, idx)) {
        BIT_CLR(bitmap, idx);
        free_pages++;
        if (idx < rover_index) rover_index = idx; /* 조기 재사용 유도 */
    }
}

void* pmm_alloc_low_4m(void) {
    if (low_next + PAGE_SIZE > LOW4M_LIMIT)
        return NULL;  // 공간 없음
    void* ret = (void*)low_next;
    low_next += PAGE_SIZE;
    return ret;
}


/* ------------------------------------------
   통계
   ------------------------------------------ */
uint32_t pmm_free_count(void)  { return free_pages; }
uint32_t pmm_total_count(void) { return total_pages; }
