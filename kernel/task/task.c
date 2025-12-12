#include "task.h"
#include "tss.h"
#include "gdt.h"
#include "mm/vmm.h"
#include "serial.h"
#include <string.h>
#include <stdint.h>

/* externs from kernel */
extern uint32_t *pgdir;
extern void      ctx_switch(uint32_t *prev_esp, uint32_t *next_esp);
extern void      pic_send_eoi(int irq);

/* 설정 */
#define KSTACK_SIZE        (16 * 1024)
#define TIME_SLICE_TICKS   5

/* 전역 */
static task_t   *g_current     = NULL;
static task_t   *g_readyq      = NULL;   /* 원형 큐 헤드 */
static uint32_t  g_next_tid    = 1;
static int       sched_enabled = 0;
static task_t   *g_idle_task   = NULL;
static uint64_t  g_cpu_total_ticks = 0;
static uint64_t  g_cpu_idle_ticks  = 0;

/* 외부에서 현재 태스크 조회 */
task_t* current_task(void) { return g_current; }

/* 원형 큐 유틸 */
static void rq_push(task_t *t) {
    if (!g_readyq) {
        g_readyq = t->next = t;
    } else {
        t->next = g_readyq->next;
        g_readyq->next = t;
    }
}

/* 최초 idle 스레드 */
static void idle_thread(void *arg) {
    (void)arg;
    for (;;) {
        __asm__ __volatile__("sti; hlt");
    }
}

/* kthread 트램펄린 (ret 도착점) */
static void kthread_trampoline(void) {
    void (*entry)(void*);
    void *arg;

    /* prepare_kthread_stack에서 쌓아둔 entry, arg */
    __asm__ __volatile__(
        "pop %0\n"
        "pop %1\n"
        : "=r"(entry), "=r"(arg)
    );

    __asm__ __volatile__("sti");
    entry(arg);

    g_current->state = TASK_ZOMBIE;
    /* 단순 종료 처리: 다음 태스크로 */
    for (;;){
        __asm__ __volatile__("cli");
        /* 현재 태스크를 READY에서 제외하려면 별도 정리 로직 필요 */
        __asm__ __volatile__("sti");
        __asm__ __volatile__("hlt");
    }
}

/* kthread 스택: [ret=kthread_trampoline][edi][esi][ebx][ebp][entry][arg] */
static void prepare_kthread_stack(task_t *t, void (*entry)(void*), void *arg) {
    uint32_t *sp = (uint32_t *)(t->kstack_base + t->kstack_size);
    sp = (uint32_t *)((uintptr_t)sp & ~0xF);

    *(--sp) = (uint32_t)kthread_trampoline; /* ret */

    *(--sp) = 0; /* edi */
    *(--sp) = 0; /* esi */
    *(--sp) = 0; /* ebx */
    *(--sp) = 0; /* ebp */

    *(--sp) = (uint32_t)entry;
    *(--sp) = (uint32_t)arg;

    t->ctx.esp = (uint32_t)sp;
}

/* kmalloc는 커널 힙으로 가정 */
extern void *kmalloc(size_t sz);

static task_t* task_alloc(const char *name) {
    task_t *t = (task_t*)kmalloc(sizeof(task_t));
    memset(t, 0, sizeof(*t));
    t->tid        = g_next_tid++;
    t->state      = TASK_READY;
    t->time_slice = TIME_SLICE_TICKS;
    t->name       = name;
    return t;
}

task_t* kthread_create(void (*entry)(void *), void *arg, const char *name) {
    task_t *t = task_alloc(name ? name : "kthread");
    t->kstack_size = KSTACK_SIZE;
    t->kstack_base = (uint8_t*)kmalloc(KSTACK_SIZE);
    t->pgdir       = pgdir;   /* 커널 주소공간 공유 */
    t->is_user     = 0;

    /* (필요 시) 스택 페이지 매핑 보증: kmalloc가 이미 매핑해주면 생략 가능 */

    prepare_kthread_stack(t, entry, arg);
    rq_push(t);
    return t;
}

/* 가장 단순한 RR 스케줄러 */
static void schedule(void) {
    __asm__ __volatile__("cli");

    task_t *prev = g_current;

    task_t *cand = g_readyq ? g_readyq->next : NULL;
    if (!cand) { __asm__ __volatile__("sti"); return; }

    for (int i = 0; cand && i < 1024; ++i) {
        if (cand->state == TASK_READY) break;
        cand = cand->next;
        if (cand == g_readyq->next) break;
    }

    if (!cand || cand->state != TASK_READY) { __asm__ __volatile__("sti"); return; }

    if (prev == cand) {
        cand->time_slice = TIME_SLICE_TICKS;
        __asm__ __volatile__("sti");
        return;
    }

    if (cand->pgdir && prev && cand->pgdir != prev->pgdir) {
        vmm_switch_pagedir(cand->pgdir);
    }

    /* ring0 인터럽트 진입용 커널 스택 */
    tss_set_kernel_stack((uint32_t)(cand->kstack_base + cand->kstack_size));

    cand->state = TASK_RUNNING;
    if (prev && prev->state == TASK_RUNNING) prev->state = TASK_READY;

    task_t *old = g_current;
    g_current = cand;
    g_readyq  = cand;

    ctx_switch(&old->ctx.esp, &cand->ctx.esp);

    __asm__ __volatile__("sti");
}

/* PIT ISR에서 호출 */
void schedule_from_timer(void) {
    if (!sched_enabled || !g_current) return;

    g_cpu_total_ticks++;
    if (g_idle_task && g_current == g_idle_task)
        g_cpu_idle_ticks++;
    if (--g_current->time_slice <= 0) {
        g_current->time_slice = TIME_SLICE_TICKS;
        schedule();
    }
}

/* 자발적 양보 */
void yield(void) {
    if (!sched_enabled) return;
    schedule();
}

/* 부팅 스레드 래핑 + idle 생성 */
static task_t g_bootstrap;

void tasking_init(void) {
    memset(&g_bootstrap, 0, sizeof(g_bootstrap));
    g_bootstrap.tid         = g_next_tid++;
    g_bootstrap.state       = TASK_RUNNING;
    g_bootstrap.pgdir       = pgdir;
    g_bootstrap.name        = "bootstrap";
    g_bootstrap.kstack_size = KSTACK_SIZE;
    g_bootstrap.kstack_base = (uint8_t*)kmalloc(KSTACK_SIZE);
    tss_set_kernel_stack((uint32_t)(g_bootstrap.kstack_base + g_bootstrap.kstack_size));

    g_current       = &g_bootstrap;
    g_readyq        = &g_bootstrap;
    g_bootstrap.next= &g_bootstrap;

    /* idle */
    g_idle_task = kthread_create(idle_thread, 0, "idle");
}

/* 데모용 커널 스레드 */
static void test_worker(void *arg) {
    (void)arg;
    for (;;) {
        extern volatile uint64_t jiffies;
        serial_printf("[worker %u] j=%u\n", current_task()->tid, (unsigned)jiffies);
        for (volatile int i=0;i<1000000;i++) { }
        yield();
    }
}

void start_scheduler(void) {
    kthread_create(test_worker, 0, "worker#1");
    kthread_create(test_worker, 0, "worker#2");
    /* 비차단형 시작: PIT에서만 스케줄 */
    sched_enabled = 1;
}

/* --------- (선택) 유저모드 진입 골격 --------- */
extern void usermode_iret_trampoline(void);

static void prepare_user_first_switch(task_t *t, uint32_t entry_user, uint32_t user_stack_top) {
    uint32_t *sp = (uint32_t *)(t->kstack_base + t->kstack_size);
    sp = (uint32_t *)((uintptr_t)sp & ~0xF);

    /* ctx_switch 복원용 4레지스터 */
    *(--sp) = 0; /* edi */
    *(--sp) = 0; /* esi */
    *(--sp) = 0; /* ebx */
    *(--sp) = 0; /* ebp */

    /* ret → 트램펄린 (트램펄린 내부에서 iret 수행) */
    *(--sp) = (uint32_t)usermode_iret_trampoline;

    /* 트램펄린이 iret용으로 꺼내쓸 프레임 (아래 5개) */
    *(--sp) = 0x1B;           /* SS (USER_DS) */
    *(--sp) = user_stack_top; /* ESP */
    *(--sp) = 0x202;          /* EFLAGS (IF=1) */
    *(--sp) = 0x23;           /* CS (USER_CS) */
    *(--sp) = entry_user;     /* EIP */

    t->ctx.esp = (uint32_t)sp;
}

task_t* proc_create_user(uint32_t entry_user, uint32_t user_stack_top,
                         uint32_t *pgdir_user, const char *name) {
    task_t *t = task_alloc(name ? name : "proc");
    t->kstack_size = KSTACK_SIZE;
    t->kstack_base = (uint8_t*)kmalloc(KSTACK_SIZE);
    t->pgdir       = pgdir_user;
    t->is_user     = 1;

    prepare_user_first_switch(t, entry_user, user_stack_top);
    rq_push(t);
    return t;
}

/* CPU usage helpers for System Monitor */
uint32_t task_cpu_usage_percent(void)
{
    static uint64_t prev_total = 0;
    static uint64_t prev_idle  = 0;

    uint64_t total64 = g_cpu_total_ticks;
    uint64_t idle64  = g_cpu_idle_ticks;

    uint64_t delta_total = total64 - prev_total;
    uint64_t delta_idle  = idle64  - prev_idle;

    prev_total = total64;
    prev_idle  = idle64;

    if (delta_total == 0) {
        // No time elapsed since last sample; keep previous reading (assume 0%).
        return 0;
    }

    uint64_t delta_busy = (delta_total > delta_idle) ? (delta_total - delta_idle) : 0;
    uint32_t pct = (uint32_t)((delta_busy * 100u) / delta_total);
    if (pct > 100u)
        pct = 100u;
    return pct;
}

void task_cpu_reset(void)
{
    g_cpu_total_ticks = 0;
    g_cpu_idle_ticks = 0;
}

/* Task enumeration helpers for GUI (task manager) */
task_t* task_enum_head(void)
{
    if (!g_readyq)
        return NULL;
    return g_readyq->next;
}

task_t* task_enum_next(task_t *t)
{
    if (!t || !g_readyq)
        return NULL;
    task_t *next = t->next;
    if (!next)
        return NULL;
    /* Stop after one full cycle around the ready queue ring */
    if (next == g_readyq->next)
        return NULL;
    return next;
}
