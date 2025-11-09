// kernel/mm/vmm.c — x86 32-bit VMM (4KiB pages)
#include "vmm.h"
#include <pmm.h>
#include <stdint.h>
#include <stddef.h>
#include <fb.h>
/* 외부 심볼: 부트/링커에서 제공 */
extern uint32_t page_directory[];     /* 물리 주소에 놓여 있음(저주소 아이덴티티 매핑 가정) */

/* PMM (이미 제공됨) */
extern void*    pmm_alloc(void);
extern void     pmm_free(void*);
extern uint32_t pmm_free_count(void);

/* (선택) 시리얼 로그가 있다면 사용 */
#ifndef COM1
#define COM1 0x3F8
#endif
extern void serial_write(uint16_t port, const char* s);

/* 유틸 */
#define PAGE_SIZE   4096u
#define PDE_COUNT   1024u
#define PTE_COUNT   1024u
#define PAGE_MASK   (~(uintptr_t)(PAGE_SIZE-1))

#define PAGE_PRESENT  (1 << 0)
#define PAGE_RW       (1 << 1)
#define PAGE_PWT      (1 << 3)
#define PAGE_PCD      (1 << 4)

static inline uint32_t* phys_to_virt_low(uintptr_t phys)
{
    /* 저주소(최소 4MiB) 아이덴티티 매핑 가정: 물리=가상 */
    return (uint32_t*)phys;
}

static inline uintptr_t virt_to_phys_page_aligned(uintptr_t v)
{
    return (v & PAGE_MASK);
}

static inline uint32_t pde_flags(uint32_t pde) { return pde & 0xFFFu; }
static inline uint32_t pte_flags(uint32_t pte) { return pte & 0xFFFu; }
static inline uintptr_t pde_addr(uint32_t pde) { return (uintptr_t)(pde & ~0xFFFu); }
static inline uintptr_t pte_addr(uint32_t pte) { return (uintptr_t)(pte & ~0xFFFu); }

static inline void write_cr3(uintptr_t phys) { __asm__ __volatile__("mov %0, %%cr3"::"r"(phys):"memory"); }
static inline uintptr_t read_cr3(void){ uintptr_t v; __asm__ __volatile__("mov %%cr3,%0":"=r"(v)); return v; }
static inline uintptr_t read_cr2(void){ uintptr_t v; __asm__ __volatile__("mov %%cr2,%0":"=r"(v)); return v; }
static inline void invlpg(void* addr) { __asm__ __volatile__("invlpg (%0)"::"r"(addr):"memory"); }

static void* memset32(void* dst, uint32_t value, size_t bytes)
{
    uint32_t* p = (uint32_t*)dst;
    size_t n = bytes >> 2;
    for (size_t i=0;i<n;++i) p[i] = value;
    return dst;
}

static inline uint32_t* get_page_table(uintptr_t virt, int create, uint32_t pde_extra_flags)
{
    uint32_t pd_index = (virt >> 22) & 0x3FFu;
    uint32_t pde = page_directory[pd_index];

    if (pde & VMM_P) {
        uintptr_t pt_phys = pde_addr(pde);
        return phys_to_virt_low(pt_phys);
    }

    if (!create) return NULL;

    /* 새 PT 할당 (물리 프레임 4KiB) */
    void* pt_phys_v = pmm_alloc_low_4m(); 
    if (!pt_phys_v) return NULL;
    uintptr_t pt_phys = (uintptr_t)pt_phys_v & PAGE_MASK;

    /* 물리 PT 페이지(저주소 아이덴티티) 0으로 초기화 */
    memset32(phys_to_virt_low(pt_phys), 0, PAGE_SIZE);

    /* PDE 기록: present|rw + extra */
    uint32_t flags = (VMM_P | VMM_RW) | (pde_extra_flags & 0xFFFu);
    page_directory[pd_index] = (uint32_t)(pt_phys | flags);

    /* CR3 재적재(혹은 해당 PT 영역 INVLPG) — 여기선 간단히 CR3 reload */
    vmm_reload_cr3();

    return phys_to_virt_low(pt_phys);
}

int vmm_map(uintptr_t virt, uintptr_t phys, uint32_t flags)
{
    if ((virt & (PAGE_SIZE-1)) || (phys & (PAGE_SIZE-1))) return -1;

    uint32_t pde_flags_extra = 0;
    /* 사용자/캐시 정책 등 상위 비트는 PDE에도 일부 반영하는 것이 안전 */
    if (flags & VMM_US)  pde_flags_extra |= VMM_US;
    if (flags & VMM_RW)  pde_flags_extra |= VMM_RW;
    if (flags & VMM_PWT) pde_flags_extra |= VMM_PWT;
    if (flags & VMM_PCD) pde_flags_extra |= VMM_PCD;

    uint32_t* pt = get_page_table(virt, /*create=*/1, pde_flags_extra);
    if (!pt) return -2;

    uint32_t pti = (virt >> 12) & 0x3FFu;
    uint32_t pte = pt[pti];
    if (pte & VMM_P) {
        /* 이미 매핑 존재 */
        return -3;
    }

    pt[pti] = (uint32_t)(phys | (flags | VMM_P));

    /* 해당 주소만 TLB invalidate */
    vmm_invlpg((void*)virt);
    return 0;
}

int vmm_unmap(uintptr_t virt)
{
    if (virt & (PAGE_SIZE-1)) return -1;

    uint32_t* pt = get_page_table(virt, /*create=*/0, 0);
    if (!pt) return -2;
    uint32_t pti = (virt >> 12) & 0x3FFu;

    if (!(pt[pti] & VMM_P)) return -3;

    pt[pti] = 0;
    vmm_invlpg((void*)virt);
    return 0;
}

int vmm_query(uintptr_t virt, uintptr_t* phys_out, uint32_t* flags_out)
{
    uint32_t* pt = get_page_table(virt, /*create=*/0, 0);
    if (!pt) return -1;
    uint32_t pti = (virt >> 12) & 0x3FFu;
    uint32_t pte = pt[pti];
    if (!(pte & VMM_P)) return -2;

    if (phys_out)  *phys_out  = pte_addr(pte);
    if (flags_out) *flags_out = pte_flags(pte);
    return 0;
}

void* vmm_alloc_page(uintptr_t virt, uint32_t flags)
{
    void* frame = pmm_alloc();
    if (!frame) return NULL;
    if (vmm_map(virt, ((uintptr_t)frame & PAGE_MASK), flags) != 0) {
        /* 맵 실패시 프레임 반환 */
        pmm_free(frame);
        return NULL;
    }
    return (void*)virt;
}

void vmm_reload_cr3(void)
{
    /* page_directory는 물리 주소에 놓여있는 것으로 가정 */
    write_cr3((uintptr_t)page_directory);
}

void vmm_invlpg(void* addr)
{
    invlpg(addr);
}

/* ───────────────── 페이지 폴트 핸들러 ─────────────────
   - 당신의 ISR/IDT 디스패처에서 벡터 14 발생 시
     (errcode, cr2)를 넘겨 이 함수를 호출만 해주세요.
*/
static void print_hex(uint32_t v, char* out8)
{
    static const char H[]="0123456789ABCDEF";
    for (int i=0;i<8;i++){ out8[7-i]=H[v&0xF]; v>>=4; }
    out8[8]=0;
}

void vmm_page_fault_handler(uint32_t errcode, uintptr_t cr2)
{
    /* 간단 로그: serial 만 사용 (있다면) */
    char buf[96];
    char a[9], e[9];
    print_hex((uint32_t)cr2, a);
    print_hex(errcode, e);
    buf[0]=0;

    /* 조촐한 문자열 합치기 */
    const char* p1="[pf] cr2=0x";
    const char* p2=" err=";
    const char* p3="\r\n";
    char* w=buf;
    const char* s=p1; while(*s)*w++=*s++;
    s=a; while(*s)*w++=*s++;
    s=p2; while(*s)*w++=*s++;
    s=e; while(*s)*w++=*s++;
    s=p3; while(*s)*w++=*s++; *w=0;

    serial_write(COM1, buf);

    draw_text(0, 0, ":|\nSomething went wrong with your PC.", 0x00FF00, 0x000000);

    __asm__ __volatile__("cli; hlt");
}
int page_present(uint32_t va)
{
    uint32_t pdi = (va >> 22) & 0x3FF;
    uint32_t pti = (va >> 12) & 0x3FF;
    extern uint32_t page_directory[];


    if (!(page_directory[pdi] & 1))
        return 0;

    uint32_t *page_table = (uint32_t *)(page_directory[pdi] & ~0xFFFu);
    if (!(page_table[pti] & 1))
        return 0;

    return 1;
}
void vmm_init(void)
{
    vmm_reload_cr3();
    serial_write(COM1, "[vmm] init done\r\n");
    
}
int vmm_map_page(uint32_t va, uint32_t pa, uint32_t flags)
{
    va &= ~0xFFFu;
    pa &= ~0xFFFu;

    // 이미 매핑된 페이지면 skip
    if (page_present(va))
        return -3;

    // 페이지 엔트리 플래그 계산
    uint32_t entry_flags = flags | PAGE_PRESENT;

    // 실제 매핑 시도
    int rc = vmm_map(va, pa, entry_flags);

    // 디버그 출력
    serial_printf("[vmm_map_page] va=%08x pa=%08x -> rc=%d\n",
                  (unsigned)va, (unsigned)pa, rc);

    // TLB invalidate
    __asm__ volatile("invlpg (%0)" :: "r"(va) : "memory");

    return rc;
}



void setup_paging(void) {
    // TODO: implement paging later
}

uint32_t virt_to_phys(uint32_t *pgdir, uint32_t vaddr)
{
    uint32_t pd_idx = (vaddr >> 22) & 0x3FF;
    uint32_t pt_idx = (vaddr >> 12) & 0x3FF;

    uint32_t *pt = (uint32_t*)(pgdir[pd_idx] & ~0xFFF);
    if (!pt)
        return 0;

    uint32_t phys = (pt[pt_idx] & ~0xFFF) | (vaddr & 0xFFF);
    return phys;
}

void vmm_switch_pagedir(uint32_t *new_pgdir_phys)
{
    if (!new_pgdir_phys)
        return;

    // 새 페이지 디렉터리로 전환 (CR3에 물리 주소 로드)
    __asm__ __volatile__("mov %0, %%cr3" :: "r"(new_pgdir_phys) : "memory");
}
