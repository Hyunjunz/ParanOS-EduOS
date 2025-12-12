#include "pmm.h"
#include <mm/pmm.h>  // Limine PMM prototypes (ext_mem_alloc 등)
#include <mm/vmm.h>  // HHDM offset helpers
#include "string.h"

// 커널에서 기대하는 전역 pgdir (현재는 사용하지 않음)
uint32_t *pgdir = NULL;

// -------------------------------------------------------------------
// 간단한 커널 전용 페이지 할당기
//  - ext_mem_alloc 의존을 제거하여 부팅 초기 트리플 폴트 방지
//  - 커널 이미지 바로 뒤의 물리 메모리에서 일정 크기만큼만 사용
// -------------------------------------------------------------------

extern char __kernel_phys_end[];

#define FALLBACK_POOL_SIZE (64 * 1024 * 1024) // 여유 확보 (DMA/큰 버퍼 대비)

static uintptr_t fb_base   = 0;
static uintptr_t fb_limit  = 0;
static uintptr_t fb_cursor = 0;
static uint32_t fb_total_pages = 0;
static uint32_t memmap_total_pages = 0;
static int memmap_total_ready = 0;
static uint32_t boot_total_pages = 0;

static inline uintptr_t align_up(uintptr_t v, uintptr_t a) {
    return (v + (a - 1)) & ~(a - 1);
}

static void ensure_fallback_pool(void) {
    if (fb_base) {
        return;
    }

    fb_base   = align_up((uintptr_t)__kernel_phys_end, PAGE_SIZE);
    fb_limit  = fb_base + FALLBACK_POOL_SIZE;
    fb_cursor = fb_base;
    fb_total_pages = (uint32_t)((fb_limit - fb_base) / PAGE_SIZE);
}

static void compute_memmap_total(void)
{
    if (boot_total_pages) {
        memmap_total_pages = boot_total_pages;
        memmap_total_ready = 1;
        return;
    }

    if (memmap_total_ready)
        return;

    size_t n = 0;
    struct memmap_entry *mm = get_raw_memmap(&n);

    // raw memmap만 사용 (부트 단계에서 확보된 범위)

    uint64_t total = 0;
    for (size_t i = 0; i < n; ++i)
    {
        if (mm[i].type == MEMMAP_USABLE ||
            mm[i].type == MEMMAP_BOOTLOADER_RECLAIMABLE ||
            mm[i].type == MEMMAP_KERNEL_AND_MODULES ||
            mm[i].type == MEMMAP_FRAMEBUFFER ||
            mm[i].type == MEMMAP_EFI_RECLAIMABLE)
        {
            total += mm[i].length;
        }
    }

    if (total > 0) {
        memmap_total_pages = (uint32_t)(total / PAGE_SIZE);
        memmap_total_ready = 1;
    }
}

void pmm_set_boot_total_pages(uint32_t pages)
{
    boot_total_pages = pages;
}

uint32_t pmm_boot_total_pages(void)
{
    return boot_total_pages;
}

// 커널 PMM 초기화는 현재 단순 풀만 준비한다.
void pmm_init(void *heap_top) {
    (void)heap_top;
    ensure_fallback_pool();
}

// Fallback pool 기반 4KiB 페이지 할당 (HHDM VA 반환)
void *pmm_alloc(void) {
    ensure_fallback_pool();

    if (fb_cursor + PAGE_SIZE > fb_limit) {
        return NULL;
    }

    uintptr_t phys = fb_cursor;
    fb_cursor += PAGE_SIZE;

    // HHDM VA로 변환 후 0으로 초기화
    void *va = (void *)(phys + vmm_hhdm_offset());
    memset(va, 0, PAGE_SIZE);
    return va;
}

// 물리 주소가 필요할 때는 HHDM 오프셋을 제거해서 돌려준다.
uint32_t pmm_alloc_phys(void) {
    void *va = pmm_alloc();
    if (!va) {
        return 0;
    }

    uintptr_t addr = (uintptr_t)va;
    uintptr_t hhdm = vmm_hhdm_offset();

    if (addr >= hhdm) {
        return (uint32_t)(addr - hhdm);
    }

    // HHDM 매핑이 없을 경우를 대비한 안전 장치
    return (uint32_t)addr;
}

// low 4MiB 전용 호출도 동일하게 fallback 풀을 사용한다.
void *pmm_alloc_low_4m(void) {
    return pmm_alloc();
}

// 해제/예약 관련 API는 현재 커널에서 실질적으로 사용하지 않으므로 no-op로 둔다.
void pmm_free_page(void *phys_addr) {
    (void)phys_addr;
}

void pmm_reserve_range(uintptr_t phys_begin, uintptr_t phys_end) {
    (void)phys_begin;
    (void)phys_end;
}

void pmm_release_range(uintptr_t phys_begin, uintptr_t phys_end) {
    (void)phys_begin;
    (void)phys_end;
}

// Try to allocate N contiguous physical pages from fallback pool
void *pmm_alloc_contig(uint32_t pages)
{
    ensure_fallback_pool();
    uintptr_t need = (uintptr_t)pages * PAGE_SIZE;
    if (fb_cursor + need > fb_limit)
        return NULL;
    uintptr_t phys = fb_cursor;
    fb_cursor += need;
    void *va = (void *)(phys + vmm_hhdm_offset());
    memset(va, 0, need);
    return va;
}

// 통계는 아직 의미 있는 값을 추적하지 않으므로 0을 반환한다.
uint32_t pmm_free_count(void) {
    ensure_fallback_pool();
    compute_memmap_total();
    uint32_t fb_used = (fb_cursor > fb_base) ? (uint32_t)((fb_cursor - fb_base) / PAGE_SIZE) : 0;
    if (memmap_total_pages == 0)
        return (fb_limit > fb_cursor) ? (uint32_t)((fb_limit - fb_cursor) / PAGE_SIZE) : 0;
    if (memmap_total_pages > fb_used)
        return memmap_total_pages - fb_used;
    return 0;
}

uint32_t pmm_total_count(void) {
    ensure_fallback_pool();
    compute_memmap_total();
    if (memmap_total_pages)
        return memmap_total_pages;
    return fb_total_pages;
}
