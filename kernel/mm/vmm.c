#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <string.h>
#include <mm/vmm.h>
#include <mm/pmm.h>
#include "../pmm.h"   // kernel-side PMM shim (ext_mem_alloc wrapper)
#include <sys/cpu.h>
#include <limine.h>
#include "serial.h"
#include <fb.h>
#include "../lib/misc.h"

#define PT_SIZE ((uint64_t)0x1000)

typedef uint64_t pt_entry_t;
extern pagemap_t kernel_pagemap;
// Maps level indexes to the page size for that level.
static const uint64_t page_sizes[] = {
    0x1000,          // L1 -> 4KiB
    0x200000,        // L2 -> 2MiB
    0x40000000,      // L3 -> 1GiB
    0x8000000000,    // L4 -> 512GiB (5-level only)
    0x1000000000000  // L5 -> 256TiB (5-level only)
};

pagemap_t kernel_pagemap = {0};

#if defined(__x86_64__)

/* Forward decl so early uses do not trigger implicit int issues. */
pagemap_t current_pagemap(void);

static inline uint64_t read_cr3(void) {
    uint64_t v;
    __asm__ volatile("mov %%cr3, %0" : "=r"(v));
    return v;
}

static inline void write_cr3(uint64_t v) {
    __asm__ volatile("mov %0, %%cr3" :: "r"(v) : "memory");
}

static inline uint64_t read_cr4(void) {
    uint64_t v;
    __asm__ volatile("mov %%cr4, %0" : "=r"(v));
    return v;
}

/* Linker-provided bounds for physical/virtual kernel placement */
extern char __text_lma[];
extern char __kernel_high_start[];
extern char __kernel_high_end[];
extern char __kernel_phys_start[];
extern char __kernel_phys_end[];



void vmm_alloc_range(uintptr_t va_start, size_t size, uint64_t flags)
{
    size_t pages = (size + 0xFFF) / 0x1000;

    for (size_t i = 0; i < pages; i++) {
        uintptr_t va = va_start + i * 0x1000;
        uintptr_t pa = (uintptr_t)pmm_alloc();   // 새 물리 페이지 할당

        if (!pa)
            panic(false, "pmm_alloc failed in vmm_alloc_range");


        vmm_map_page(va, pa, flags);


    }
}


// Translate assuming: low phys is identity; higher phys reachable via kernel high-half offset.
static inline void *phys_to_virt(uint64_t phys) {
    uint64_t hhdm = vmm_hhdm_offset();

    // Kernel image range is mapped at __kernel_high_start.
    if (phys >= (uint64_t)(uintptr_t)__kernel_phys_start
     && phys <  (uint64_t)(uintptr_t)__kernel_phys_end) {
        uintptr_t delta = phys - (uint64_t)(uintptr_t)__kernel_phys_start;
        return (void *)((uintptr_t)__kernel_high_start + delta);
    }

    // Use the higher-half direct map (HHDM) for all other physical addresses.
    return (void *)(uintptr_t)(phys + hhdm);
}

static inline uint64_t virt_to_phys(const void *virt) {
    uintptr_t va = (uintptr_t)virt;
    uintptr_t hhdm = vmm_hhdm_offset();

    // Kernel image higher-half mapping.
    if (va >= (uintptr_t)__kernel_high_start && va < (uintptr_t)__kernel_high_end) {
        return va - (uintptr_t)__kernel_high_start + (uintptr_t)__kernel_phys_start;
    }

    // HHDM mapping.
    if (va >= hhdm) {
        return va - hhdm;
    }

    // Identity-mapped low addresses.
    return va;
}

#define PT_FLAG_VALID   ((uint64_t)1 << 0)
#define PT_FLAG_WRITE   ((uint64_t)1 << 1)
#define PT_FLAG_USER    ((uint64_t)1 << 2)
#define PT_FLAG_PWT     ((uint64_t)1 << 3)
#define PT_FLAG_PCD     ((uint64_t)1 << 4)
#define PT_FLAG_LARGE   ((uint64_t)1 << 7)
#define PT_FLAG_GLOBAL  ((uint64_t)1 << 8)
#define PT_FLAG_NX      ((uint64_t)1 << 63)
#define PT_PADDR_MASK   ((uint64_t)0x0000FFFFFFFFF000)

#define PT_TABLE_FLAGS  (PT_FLAG_VALID | PT_FLAG_WRITE | PT_FLAG_USER)
#define PT_IS_TABLE(x) (((x) & (PT_FLAG_VALID | PT_FLAG_LARGE)) == PT_FLAG_VALID)
#define PT_IS_LARGE(x) (((x) & (PT_FLAG_VALID | PT_FLAG_LARGE)) == (PT_FLAG_VALID | PT_FLAG_LARGE))

#define pte_new(addr, flags)    ((pt_entry_t)(addr) | (flags))
#define pte_addr(pte)           ((pte) & PT_PADDR_MASK)

static inline void flush_tlb_single(void *addr) {
    __asm__ volatile("invlpg (%0)" :: "r"(addr) : "memory");
}

static void *alloc_table(void) {
    void *raw = pmm_alloc_low_4m();
    if (!raw) {
        raw = ext_mem_alloc(PT_SIZE);
    }

    uint64_t phys = virt_to_phys(raw);
    void *virt = phys_to_virt(phys);
    memset(virt, 0, PT_SIZE);
    return virt;
}

static bool cpu_has_1gib_pages(void) {
    static bool checked = false;
    static bool supported = false;

    if (!checked) {
        uint32_t eax, ebx, ecx, edx;
        supported = cpuid(0x80000001, 0, &eax, &ebx, &ecx, &edx) && ((edx & (1u << 26)) != 0);
        checked = true;
    }

    return supported;
}

// HHDM offset supplied by Limine (falls back to canonical higher-half base)
static uint64_t g_hhdm_offset = 0;

__attribute__((used, section(".limine_requests")))
static volatile struct limine_hhdm_request hhdm_request = {
    .id = LIMINE_HHDM_REQUEST_ID,
    .revision = 0,
};

uint64_t vmm_hhdm_offset(void) {
    if (g_hhdm_offset) {
        return g_hhdm_offset;
    }

    if (hhdm_request.response && hhdm_request.response->offset) {
        g_hhdm_offset = hhdm_request.response->offset;
    } else {
        g_hhdm_offset = paging_mode_higher_half(vmm_max_paging_mode());
    }
    return g_hhdm_offset;
}

int vmm_max_paging_mode(void) {
    uint32_t eax, ebx, ecx, edx;

    if (cpuid(7, 0, &eax, &ebx, &ecx, &edx) && (ecx & (1u << 16)))
        return PAGING_MODE_X86_64_5LVL;
    return PAGING_MODE_X86_64_4LVL;
}

pagemap_t new_pagemap(int paging_mode) {
    pagemap_t pagemap;
    pagemap.levels    = (paging_mode == PAGING_MODE_X86_64_5LVL) ? 5 : 4;
    pagemap.top_level = alloc_table();
    pagemap.top_level_phys = virt_to_phys(pagemap.top_level);
    return pagemap;
}

static pt_entry_t *get_next_level(pagemap_t pagemap, pt_entry_t *current_level,
                                  uint64_t virt, enum page_size desired_sz,
                                  size_t level_idx, size_t entry) {
    pt_entry_t *ret;

    if (PT_IS_TABLE(current_level[entry])) {
        ret = (pt_entry_t *)phys_to_virt(pte_addr(current_level[entry]));
    } else {
        if (PT_IS_LARGE(current_level[entry])) {
            // Already mapped with a larger page; keep it to avoid deep recursion/splitting.
            // Signal caller to stop descending.
            return (pt_entry_t *)-1;
        } else {
            ret = alloc_table();
            current_level[entry] = pte_new(virt_to_phys(ret), PT_TABLE_FLAGS);
        }
    }

    static int dbg_next = 0;
    if (!dbg_next) {
        dbg_next = 1;
        serial_printf("[vmm] level=%zu entry=%zu cur=%p val=%llx -> %p\n",
                      level_idx, entry, current_level,
                      (unsigned long long)current_level[entry], ret);
    }

    return ret;
}

void map_page(pagemap_t pagemap, uint64_t virt_addr, uint64_t phys_addr, uint64_t flags, enum page_size pg_size) {
    if (!pagemap.top_level) {
        pagemap = kernel_pagemap;
    }
    if (!pagemap.top_level) {
        pagemap = current_pagemap();
        kernel_pagemap = pagemap;
    }

    static int dbg_printed = 0;
    if (!dbg_printed) {
        dbg_printed = 1;
        serial_printf("[vmm] map_page top=%p levels=%d cr3=%p virt=%p phys=%p flags=%llx sz=%d\n",
                      pagemap.top_level, pagemap.levels, (void *)(uintptr_t)read_cr3(),
                      (void *)(uintptr_t)virt_addr, (void *)(uintptr_t)phys_addr,
                      (unsigned long long)flags, (int)pg_size);
    }
    pagemap.top_level = phys_to_virt(virt_to_phys(pagemap.top_level));

    size_t pml5_entry = (virt_addr >> 48) & 0x1ff;
    size_t pml4_entry = (virt_addr >> 39) & 0x1ff;
    size_t pml3_entry = (virt_addr >> 30) & 0x1ff;
    size_t pml2_entry = (virt_addr >> 21) & 0x1ff;
    size_t pml1_entry = (virt_addr >> 12) & 0x1ff;

    pt_entry_t *pml5, *pml4, *pml3, *pml2, *pml1;

    flags |= PT_FLAG_VALID; // Always present

    switch (pagemap.levels) {
        case 5:
            pml5 = pagemap.top_level;
            goto level5;
        case 4:
            pml4 = pagemap.top_level;
            goto level4;
        default:
            __builtin_unreachable();
    }

level5:
    pml4 = get_next_level(pagemap, pml5, virt_addr, pg_size, 4, pml5_entry);
level4:
    pml3 = get_next_level(pagemap, pml4, virt_addr, pg_size, 3, pml4_entry);
    if (pml3 == (pt_entry_t *)-1) return;

    if (pg_size == Size1GiB) {
        if (cpu_has_1gib_pages()) {
            pml3[pml3_entry] = (pt_entry_t)(phys_addr | flags | PT_FLAG_LARGE);
        } else {
            for (uint64_t i = 0; i < 0x40000000; i += 0x200000) {
                map_page(pagemap, virt_addr + i, phys_addr + i, flags, Size2MiB);
            }
        }

        return;
    }

    pml2 = get_next_level(pagemap, pml3, virt_addr, pg_size, 2, pml3_entry);
    if (pml2 == (pt_entry_t *)-1) return;

    if (pg_size == Size2MiB) {
        pml2[pml2_entry] = (pt_entry_t)(phys_addr | flags | PT_FLAG_LARGE);
        return;
    }

    pml1 = get_next_level(pagemap, pml2, virt_addr, pg_size, 1, pml2_entry);
    if (pml1 == (pt_entry_t *)-1) return;

    if (flags & ((uint64_t)1 << 12)) {
        flags &= ~((uint64_t)1 << 12);
        flags |= ((uint64_t)1 << 7);
    }

    pml1[pml1_entry] = (pt_entry_t)(phys_addr | flags);

    static int dbg_done = 0;
    if (!dbg_done) {
        dbg_done = 1;
        serial_printf("[vmm] map_page done virt=%p phys=%p pml4=%p pml3=%p pml2=%p pml1=%p\n",
                      (void *)(uintptr_t)virt_addr, (void *)(uintptr_t)phys_addr,
                      pml4, pml3, pml2, pml1);
    }
}

void map_pages(pagemap_t pagemap, uint64_t virt, uint64_t phys, uint64_t flags, uint64_t count) {
    if (!pagemap.top_level) {
        pagemap = kernel_pagemap;
    }
    if (!pagemap.top_level) {
        pagemap = current_pagemap();
        kernel_pagemap = pagemap;
    }
    pagemap.top_level = phys_to_virt(virt_to_phys(pagemap.top_level));

    if ((virt | phys | count) & 0xFFF)
        serial_printf("vmm: map_pages misaligned request virt=%p phys=%p count=%p\n",
                      (void *)virt, (void *)phys, (void *)count);

    for (uint64_t i = 0; i < count; ) {
        if (((phys + i) & (0x40000000 - 1)) == 0 && ((virt + i) & (0x40000000 - 1)) == 0 && count - i >= 0x40000000) {
            map_page(pagemap, virt + i, phys + i, flags, Size1GiB);
            i += 0x40000000;
            continue;
        }
        if (((phys + i) & (0x200000 - 1)) == 0 && ((virt + i) & (0x200000 - 1)) == 0 && count - i >= 0x200000) {
            map_page(pagemap, virt + i, phys + i, flags, Size2MiB);
            i += 0x200000;
            continue;
        }
        map_page(pagemap, virt + i, phys + i, flags, Size4KiB);
        i += 0x1000;
    }
}

pagemap_t current_pagemap(void) {
    pagemap_t pm;
    uint64_t cr4 = read_cr4();
    pm.levels = (cr4 & (1ull << 12)) ? 5 : 4;
    pm.top_level = phys_to_virt(read_cr3() & PT_PADDR_MASK);
    return pm;
}

static pt_entry_t *walk_to_pte_create(uintptr_t virt) {
    pagemap_t pm = current_pagemap();

    size_t i5 = (virt >> 48) & 0x1ff;
    size_t i4 = (virt >> 39) & 0x1ff;
    size_t i3 = (virt >> 30) & 0x1ff;
    size_t i2 = (virt >> 21) & 0x1ff;
    size_t i1 = (virt >> 12) & 0x1ff;

    pt_entry_t *pml5 = pm.top_level;
    pt_entry_t *pml4 = pm.top_level;
    if (pm.levels == 5) {
        pml4 = get_next_level(pm, pml5, virt, Size4KiB, 4, i5);
    }

    pt_entry_t *pml3 = get_next_level(pm, pml4, virt, Size4KiB, 3, i4);
    pt_entry_t *pml2 = get_next_level(pm, pml3, virt, Size4KiB, 2, i3);
    pt_entry_t *pml1 = get_next_level(pm, pml2, virt, Size4KiB, 1, i2);

    return &pml1[i1];
}

static int locate_entry(uintptr_t virt, pt_entry_t **out_entry, enum page_size *level_out) {
    pagemap_t pm = current_pagemap();

    size_t i5 = (virt >> 48) & 0x1ff;
    size_t i4 = (virt >> 39) & 0x1ff;
    size_t i3 = (virt >> 30) & 0x1ff;
    size_t i2 = (virt >> 21) & 0x1ff;
    size_t i1 = (virt >> 12) & 0x1ff;

    pt_entry_t *pml4;
    if (pm.levels == 5) {
        pt_entry_t e5 = ((pt_entry_t *)pm.top_level)[i5];
        if (!PT_IS_TABLE(e5))
            return -1;
        pml4 = (pt_entry_t *)phys_to_virt(pte_addr(e5));
    } else {
        pml4 = pm.top_level;
    }

    pt_entry_t e4 = pml4[i4];
    if (!PT_IS_TABLE(e4))
        return -1;
    pt_entry_t *pml3 = (pt_entry_t *)phys_to_virt(pte_addr(e4));

    pt_entry_t e3 = pml3[i3];
    if (PT_IS_LARGE(e3)) {
        *out_entry = &pml3[i3];
        if (level_out) *level_out = Size1GiB;
        return 0;
    }
    if (!PT_IS_TABLE(e3))
        return -1;
    pt_entry_t *pml2 = (pt_entry_t *)phys_to_virt(pte_addr(e3));

    pt_entry_t e2 = pml2[i2];
    if (PT_IS_LARGE(e2)) {
        *out_entry = &pml2[i2];
        if (level_out) *level_out = Size2MiB;
        return 0;
    }
    if (!PT_IS_TABLE(e2))
        return -1;
    pt_entry_t *pml1 = (pt_entry_t *)phys_to_virt(pte_addr(e2));

    *out_entry = &pml1[i1];
    if (level_out) *level_out = Size4KiB;
    return 0;
}

static uint32_t entry_to_legacy_flags(pt_entry_t entry) {
    uint32_t flags = 0;
    if (entry & PT_FLAG_VALID) flags |= (uint32_t)VMM_P;
    if (entry & PT_FLAG_WRITE) flags |= (uint32_t)VMM_RW;
    if (entry & PT_FLAG_USER)  flags |= (uint32_t)VMM_US;
    if (entry & PT_FLAG_PWT)   flags |= (uint32_t)VMM_PWT;
    if (entry & PT_FLAG_PCD)   flags |= (uint32_t)VMM_PCD;
    return flags;
}

int vmm_map(uintptr_t virt, uintptr_t phys, uint32_t flags) {
    if ((virt & 0xFFF) || (phys & 0xFFF))
        return -1;

    pt_entry_t *pte;
    enum page_size lvl;
    if (locate_entry(virt, &pte, &lvl) == 0 && (*pte & PT_FLAG_VALID))
        return -3;

    uint64_t map_flags = 0;
    if (flags & VMM_RW)  map_flags |= VMM_FLAG_WRITE;
    if (flags & VMM_PWT) map_flags |= VMM_PWT;
    if (flags & VMM_PCD) map_flags |= VMM_PCD;

    map_page(current_pagemap(), virt, phys, map_flags, Size4KiB);
    flush_tlb_single((void *)virt);
    return 0;
}

int vmm_unmap(uintptr_t virt) {
    if (virt & 0xFFF)
        return -1;

    pt_entry_t *pte;
    enum page_size lvl;
    if (locate_entry(virt, &pte, &lvl) != 0)
        return -2;

    if (!(*pte & PT_FLAG_VALID))
        return -3;

    *pte = 0;
    flush_tlb_single((void *)virt);
    return 0;
}

int vmm_query(uintptr_t virt, uintptr_t *phys_out, uint32_t *flags_out) {
    pt_entry_t *pte;
    enum page_size lvl;
    if (locate_entry(virt, &pte, &lvl) != 0)
        return -1;

    pt_entry_t entry = *pte;
    if (!(entry & PT_FLAG_VALID))
        return -2;

    uint64_t base = pte_addr(entry);
    uint64_t offset = virt & (page_sizes[lvl] - 1);

    if (phys_out)  *phys_out  = (uintptr_t)(base + offset);
    if (flags_out) *flags_out = entry_to_legacy_flags(entry);
    return 0;
}

void *vmm_alloc_page(uintptr_t virt, uint32_t flags) {
    void *frame = pmm_alloc();
    if (!frame)
        return NULL;

    if (vmm_map(virt, (uintptr_t)frame & ~0xFFFu, flags | (uint32_t)VMM_P) != 0) {
        pmm_free(frame, 0x1000);
        return NULL;
    }

    return (void *)virt;
}

int vmm_map_page(uintptr_t virt, uintptr_t phys, uint32_t flags) {
    return vmm_map(virt, phys, flags | (uint32_t)VMM_P);
}

void vmm_reload_cr3(void) {
    write_cr3(read_cr3());
}

void vmm_invlpg(void *addr) {
    flush_tlb_single(addr);
}

void vmm_page_fault_handler(uint32_t errcode, uintptr_t cr2) {
    serial_printf("[pf] cr2=%p err=%08x\n", (void *)cr2, errcode);
    // Avoid touching the framebuffer here; page faults during FB operations
    // must not recursively fault again.
    __asm__ __volatile__("cli; hlt");
}

/* Translate high-half direct map (kernel virtual) to physical.
   For lower-half identity-mapped addresses, this is a passthrough. */
uintptr_t phys_addr_of(uintptr_t va) {
    uintptr_t vstart = (uintptr_t)__kernel_high_start;
    uintptr_t pstart = (uintptr_t)__text_lma;

    if (va >= vstart) {
        return (va - vstart) + pstart;
    }
    return va;
}

/* Legacy API stub: switch page directory (CR3) */
void vmm_switch_pagedir(uint32_t *new_pgdir_phys) {
    if (!new_pgdir_phys)
        return;
    write_cr3((uint64_t)(uintptr_t)new_pgdir_phys);
}

void vmm_init(void) {
    uintptr_t cr3_phys = read_cr3() & PT_PADDR_MASK;

    // Cache HHDM offset early
    uint64_t hhdm = vmm_hhdm_offset();

    // 현재 하드웨어 CR3/CR4 기반으로 pagemap 채움
    kernel_pagemap = current_pagemap();
    kernel_pagemap.top_level_phys = cr3_phys;

    vmm_reload_cr3();

    serial_printf("[vmm] long mode paging ready cr3=%p top_phys=%p top=%p levels=%d off=%p\n",
                  (void *)cr3_phys,
                  (void *)kernel_pagemap.top_level_phys,
                  kernel_pagemap.top_level,
                  kernel_pagemap.levels,
                  (void *)hhdm);
}


#else
#error "vmm.c only implements x86_64 long mode in this configuration"
#endif
