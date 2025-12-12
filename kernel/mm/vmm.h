#ifndef MM__VMM_H__
#define MM__VMM_H__

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#if defined (__x86_64__) || defined (__i386__)

// User-facing flags
#define VMM_FLAG_WRITE   ((uint64_t)1 << 1)
#define VMM_FLAG_NOEXEC  ((uint64_t)1 << 63)
#define VMM_FLAG_FB      (((uint64_t)1 << 3) | ((uint64_t)1 << 12))

// Legacy compatibility flags
#ifndef VMM_P
#define VMM_P    ((uint64_t)1 << 0)
#endif
#ifndef VMM_RW
#define VMM_RW   ((uint64_t)1 << 1)
#endif
#ifndef VMM_US
#define VMM_US   ((uint64_t)1 << 2)
#endif
#ifndef VMM_PWT
#define VMM_PWT  ((uint64_t)1 << 3)
#endif
#ifndef VMM_PCD
#define VMM_PCD  ((uint64_t)1 << 4)
#endif

// Backward aliases used across kernel code
#ifndef PAGE_PRESENT
#define PAGE_PRESENT  VMM_P
#endif
#ifndef PAGE_RW
#define PAGE_RW       VMM_RW
#endif
#ifndef PAGE_US
#define PAGE_US       VMM_US
#endif
#ifndef PAGE_PWT
#define PAGE_PWT      VMM_PWT
#endif
#ifndef PAGE_PCD
#define PAGE_PCD      VMM_PCD
#endif
#define VMM_A    0x020
#define VMM_D    0x040
#define VMM_PAT  0x080
#define VMM_G    0x100
#define VMM_MAX_LEVEL 3

#define PAGING_MODE_X86_64_4LVL 0
#define PAGING_MODE_X86_64_5LVL 1

#define PAGING_MODE_MIN PAGING_MODE_X86_64_4LVL
#define PAGING_MODE_MAX PAGING_MODE_X86_64_5LVL

#define paging_mode_va_bits(mode) ((mode) ? 57 : 48)

static inline uint64_t paging_mode_higher_half(int paging_mode) {
    return (paging_mode == PAGING_MODE_X86_64_5LVL)
        ? 0xff00000000000000ULL
        : 0xffff800000000000ULL;
}

typedef struct {
    int   levels;
    void *top_level;
    uint64_t top_level_phys;
} pagemap_t;

extern pagemap_t kernel_pagemap;

enum page_size {
    Size4KiB,
    Size2MiB,
    Size1GiB
};

pagemap_t new_pagemap(int lv);
void map_page(pagemap_t pagemap, uint64_t virt_addr, uint64_t phys_addr, uint64_t flags, enum page_size page_size);

#elif defined (__aarch64__)

#define VMM_FLAG_WRITE   ((uint64_t)1 << 0)
#define VMM_FLAG_NOEXEC  ((uint64_t)1 << 1)
#define VMM_FLAG_FB      ((uint64_t)1 << 2)

#define VMM_MAX_LEVEL 3

#define PAGING_MODE_AARCH64_4LVL 0
#define PAGING_MODE_AARCH64_5LVL 1

#define PAGING_MODE_MIN PAGING_MODE_AARCH64_4LVL
#define PAGING_MODE_MAX PAGING_MODE_AARCH64_5LVL

#define paging_mode_va_bits(mode) ((mode) ? 53 : 49)

static inline uint64_t paging_mode_higher_half(int paging_mode) {
    (void)paging_mode;
    return 0xffff000000000000ULL;
}

typedef struct {
    int   levels;
    void *top_level[2];
} pagemap_t;
extern pagemap_t kernel_pagemap;
enum page_size {
    Size4KiB,
    Size2MiB,
    Size1GiB
};

void vmm_assert_4k_pages(void);
pagemap_t new_pagemap(int lv);
void map_page(pagemap_t pagemap, uint64_t virt_addr, uint64_t phys_addr, uint64_t flags, enum page_size page_size);

#elif defined (__riscv)

#define VMM_FLAG_WRITE   ((uint64_t)1 << 0)
#define VMM_FLAG_NOEXEC  ((uint64_t)1 << 1)
#define VMM_FLAG_FB      ((uint64_t)1 << 2)

#define VMM_MAX_LEVEL 5

#define PAGING_MODE_RISCV_SV39 8
#define PAGING_MODE_RISCV_SV48 9
#define PAGING_MODE_RISCV_SV57 10

#define PAGING_MODE_MIN PAGING_MODE_RISCV_SV39
#define PAGING_MODE_MAX PAGING_MODE_RISCV_SV57

int paging_mode_va_bits(int paging_mode);

enum page_size {
    Size4KiB,
    Size2MiB,
    Size1GiB,
    Size512GiB,
    Size256TiB
};

typedef struct {
    enum page_size max_page_size;
    int            paging_mode;
    void          *top_level;
} pagemap_t;

uint64_t paging_mode_higher_half(int paging_mode);
int vmm_max_paging_mode(void);
pagemap_t new_pagemap(int paging_mode);
void map_page(pagemap_t pagemap, uint64_t virt_addr, uint64_t phys_addr, uint64_t flags, enum page_size page_size);

#elif defined (__loongarch64)

#define VMM_FLAG_WRITE   ((uint64_t)1 << 0)
#define VMM_FLAG_NOEXEC  ((uint64_t)1 << 1)
#define VMM_FLAG_FB      ((uint64_t)1 << 2)

#define PAGING_MODE_LOONGARCH64_4LVL 0

#define PAGING_MODE_MIN PAGING_MODE_LOONGARCH64_4LVL
#define PAGING_MODE_MAX PAGING_MODE_LOONGARCH64_4LVL

static inline uint64_t paging_mode_higher_half(int paging_mode) {
    (void)paging_mode;
    return 0xffff000000000000ULL;
}

enum page_size {
    Size4KiB,
    Size2MiB,
    Size1GiB
};

typedef struct {
    void *pgd[2];
} pagemap_t;

pagemap_t new_pagemap(int paging_mode);
void map_page(pagemap_t pagemap, uint64_t virt_addr, uint64_t phys_addr, uint64_t flags, enum page_size page_size);

#else
#error Unknown architecture
#endif

int vmm_max_paging_mode(void);
void map_pages(pagemap_t pagemap, uint64_t virt, uint64_t phys, uint64_t flags, uint64_t count);

/* Legacy API kept for existing code */
void vmm_init(void);
int  vmm_map(uintptr_t virt, uintptr_t phys, uint32_t flags);
int  vmm_unmap(uintptr_t virt);
int  vmm_query(uintptr_t virt, uintptr_t* phys_out, uint32_t* flags_out);

static inline uint32_t vmm_virt_to_phys(uintptr_t va)
{
    uintptr_t phys = 0;
    uint32_t fl = 0;
    if (vmm_query(va, &phys, &fl) == 0)
        return (uint32_t)phys;
    return 0;
}
void* vmm_alloc_page(uintptr_t virt, uint32_t flags);
int vmm_map_page(uintptr_t virt, uintptr_t phys, uint32_t flags);
void vmm_reload_cr3(void);
void vmm_invlpg(void* addr);
uint64_t vmm_hhdm_offset(void);
void vmm_page_fault_handler(uint32_t errcode, uintptr_t cr2);

#ifdef __cplusplus
}
#endif

#endif
