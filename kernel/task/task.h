#pragma once
#include <stdint.h>
#include <stddef.h>

typedef enum {
    TASK_READY = 0,
    TASK_RUNNING,
    TASK_BLOCKED,
    TASK_ZOMBIE
} task_state_t;


typedef struct cpu_ctx {

    uint32_t esp;     
} cpu_ctx_t;

typedef struct task {
    uint32_t        tid;
    task_state_t    state;
    cpu_ctx_t       ctx;

    uint32_t       *pgdir;

    uint8_t        *kstack_base;
    size_t          kstack_size;

    int             time_slice;   
    struct task    *next;         

    const char     *name;
    int             is_user;      
} task_t;

void     tasking_init(void);
task_t*  kthread_create(void (*entry)(void *), void *arg, const char *name);
void     schedule_from_timer(void);   
void     yield(void);              
task_t*  current_task(void);


task_t*  proc_create_user(uint32_t entry_user, uint32_t user_stack_top,
                          uint32_t *pgdir, const char *name);

void     start_scheduler(void);

