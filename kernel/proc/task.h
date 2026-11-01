/* task.h - kernel threads and the round-robin scheduler */
#ifndef PROC_TASK_H
#define PROC_TASK_H

#include <stdint.h>
#include "arch/idt.h"

#define MAX_TASKS       32
#define TASK_NAME_MAX   16
#define TASK_ARGS_MAX   64

typedef enum {
    TASK_UNUSED = 0,    /* free slot in the task table */
    TASK_READY,         /* runnable, waiting for the CPU */
    TASK_RUNNING,       /* currently on the CPU */
    TASK_SLEEPING,      /* waiting for a timer deadline */
    TASK_BLOCKED,       /* waiting for an event (e.g. a key press) */
    TASK_ZOMBIE,        /* finished, resources not yet reclaimed */
} task_state_t;

typedef void (*task_entry_t)(const char* args);

struct outbuf;

typedef struct task {
    int           pid;
    char          name[TASK_NAME_MAX];
    char          args[TASK_ARGS_MAX];
    task_state_t  state;
    int           system;       /* system tasks cannot be killed */

    uint32_t      esp;          /* saved stack pointer (points to a regs_t) */
    void*         stack;        /* base of the stack (NULL for the idle task) */
    task_entry_t  entry;

    uint32_t      wake_tick;    /* when a sleeping task becomes ready */
    uint32_t      start_tick;
    uint32_t      cpu_ticks;    /* timer ticks spent running */

    struct outbuf* out;         /* redirected stdout, or NULL for the console */
} task_t;

/* Read-only snapshot of a task, for tools like `ps` */
typedef struct {
    int          pid;
    char         name[TASK_NAME_MAX];
    char         args[TASK_ARGS_MAX];
    task_state_t state;
    int          system;
    uint32_t     start_tick;
    uint32_t     cpu_ticks;
} task_info_t;

/* Turn the boot context into the idle task (pid 0) and enable scheduling. */
void        tasking_init(void);

/* Create a new kernel thread. Returns its pid, or -1 on failure. */
int         task_spawn(const char* name, task_entry_t entry, const char* args, int system);

void        task_exit(void) __attribute__((noreturn));
void        task_yield(void);
void        task_sleep(uint32_t ms);
int         task_kill(int pid);         /* 0 ok, -1 no such task, -2 not allowed */

/* Event waiting. task_block() must be called with interrupts disabled. */
void        task_block(void);
void        task_unblock(task_t* t);

task_t*     task_current(void);
struct outbuf* task_stdout(void);
void        task_set_stdout(struct outbuf* out);

int         task_list(task_info_t* out, int max);
int         task_count(void);
int         task_find(const char* name, int nth);   /* pid of the nth task with that name */
const char* task_state_name(task_state_t s);

/* Called from interrupt context only */
void        task_tick(uint32_t now);
void        task_request_resched(void);
uint32_t    task_schedule(uint32_t esp);
void        task_fault(regs_t* r, const char* what);

#endif
