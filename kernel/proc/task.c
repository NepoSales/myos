/* task.c - kernel threads with a preemptive round-robin scheduler
 *
 * Every task owns a 16 KiB kernel stack. A task that is not running has
 * its full CPU state saved on top of that stack in the same layout the
 * interrupt stubs produce (regs_t), and `esp` points to it.
 *
 * Context switches only happen on the way out of an interrupt:
 *   - the timer IRQ ends a task's time slice (preemption), or
 *   - a task calls task_yield(), which raises interrupt 0x30.
 * task_schedule() saves the current stack pointer, picks the next READY
 * task in round-robin order and returns its stack pointer. The assembly
 * stub then pops that task's registers and irets into it.
 *
 * A new task gets a hand-made regs_t frame whose eip is task_trampoline(),
 * so the first switch to it "returns" into its entry function.
 */

#include "proc/task.h"
#include "mm/pmm.h"
#include "lib/string.h"
#include "lib/printf.h"
#include "drivers/timer.h"
#include "drivers/vga.h"
#include "arch/gdt.h"
#include "arch/io.h"
#include "panic.h"

#define STACK_PAGES     4                       /* 16 KiB per task */
#define STACK_SIZE      (STACK_PAGES * PAGE_SIZE)
#define STACK_CANARY    0xDEADC0DE
#define QUANTUM_TICKS   5                       /* 50 ms time slice */

static task_t tasks[MAX_TASKS];
static task_t* current = NULL;
static task_t* const idle = &tasks[0];
static int next_pid = 0;
static int quantum = QUANTUM_TICKS;
static volatile int need_resched = 0;
static int tasking_on = 0;

/* ---------- helpers ---------- */

static void task_trampoline(void) {
    current->entry(current->args);
    task_exit();
}

static void reap(task_t* t) {
    if (t->stack) pmm_free_pages(t->stack, STACK_PAGES);
    memset(t, 0, sizeof(*t));               /* state becomes TASK_UNUSED */
}

static int is_alive(task_t* t) {
    return t->state != TASK_UNUSED && t->state != TASK_ZOMBIE;
}

/* Print a kernel notice directly on the screen (never redirected). */
static void notice(const char* fmt, ...) {
    char buf[128];
    va_list ap;
    va_start(ap, fmt);
    kvsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    uint8_t old = console_get_color();
    console_set_color(VGA_COLOR(VGA_LIGHT_RED, VGA_BLACK));
    console_write(buf);
    console_set_color(old);
}

/* ---------- public API ---------- */

void tasking_init(void) {
    memset(tasks, 0, sizeof(tasks));
    idle->pid = next_pid++;
    strlcpy(idle->name, "idle", TASK_NAME_MAX);
    idle->state = TASK_RUNNING;
    idle->system = 1;
    idle->stack = NULL;                     /* keeps running on the boot stack */
    current = idle;
    tasking_on = 1;
}

int task_spawn(const char* name, task_entry_t entry, const char* args, int system) {
    uint32_t f = irq_save();

    task_t* t = NULL;
    for (int i = 1; i < MAX_TASKS; i++) {
        if (tasks[i].state == TASK_UNUSED) { t = &tasks[i]; break; }
    }
    void* stack = t ? pmm_alloc_pages(STACK_PAGES) : NULL;
    if (!stack) {
        irq_restore(f);
        return -1;
    }

    memset(t, 0, sizeof(*t));
    t->pid = next_pid++;
    strlcpy(t->name, name, TASK_NAME_MAX);
    strlcpy(t->args, args ? args : "", TASK_ARGS_MAX);
    t->system = system;
    t->entry = entry;
    t->stack = stack;
    t->start_tick = timer_ticks();
    *(uint32_t*)stack = STACK_CANARY;       /* detects stack overflows */

    /* Build the initial frame that isr_common will "return" into. */
    regs_t* r = (regs_t*)((char*)stack + STACK_SIZE - sizeof(regs_t));
    memset(r, 0, sizeof(*r));
    r->ds = r->es = r->fs = r->gs = GDT_KERNEL_DATA;
    r->cs = GDT_KERNEL_CODE;
    r->eip = (uint32_t)task_trampoline;
    r->eflags = 0x202;                      /* IF=1: start with interrupts enabled */
    t->esp = (uint32_t)r;
    t->state = TASK_READY;

    int pid = t->pid;
    irq_restore(f);
    klog("task: spawned pid %d (%s)\n", pid, name);
    return pid;
}

void task_exit(void) {
    __asm__ volatile ("cli");
    klog("task: pid %d (%s) exited\n", current->pid, current->name);
    current->state = TASK_ZOMBIE;           /* reclaimed by a later task_schedule() */
    task_yield();
    for (;;) cpu_halt();                    /* never reached */
}

void task_yield(void) {
    __asm__ volatile ("int $0x30" : : : "memory");
}

void task_sleep(uint32_t ms) {
    uint32_t ticks = (ms * TIMER_HZ + 999) / 1000;
    if (ticks == 0) ticks = 1;
    uint32_t f = irq_save();
    current->wake_tick = timer_ticks() + ticks;
    current->state = TASK_SLEEPING;
    task_yield();
    irq_restore(f);
}

void task_block(void) {
    current->state = TASK_BLOCKED;
    task_yield();
}

void task_unblock(task_t* t) {
    if (t->state == TASK_BLOCKED) {
        t->state = TASK_READY;
        need_resched = 1;                   /* respond to input right away */
    }
}

int task_kill(int pid) {
    uint32_t f = irq_save();
    task_t* t = NULL;
    for (int i = 0; i < MAX_TASKS; i++) {
        if (is_alive(&tasks[i]) && tasks[i].pid == pid) { t = &tasks[i]; break; }
    }
    if (!t) { irq_restore(f); return -1; }
    if (t->system) { irq_restore(f); return -2; }

    klog("task: pid %d (%s) killed\n", t->pid, t->name);
    if (t == current) task_exit();
    t->state = TASK_ZOMBIE;
    irq_restore(f);
    return 0;
}

task_t* task_current(void) { return current; }

struct outbuf* task_stdout(void) {
    return current ? current->out : NULL;
}

void task_set_stdout(struct outbuf* out) {
    current->out = out;
}

int task_list(task_info_t* out, int max) {
    int n = 0;
    uint32_t f = irq_save();
    for (int i = 0; i < MAX_TASKS && n < max; i++) {
        task_t* t = &tasks[i];
        if (!is_alive(t)) continue;
        out[n].pid = t->pid;
        strlcpy(out[n].name, t->name, TASK_NAME_MAX);
        strlcpy(out[n].args, t->args, TASK_ARGS_MAX);
        out[n].state = t->state;
        out[n].system = t->system;
        out[n].start_tick = t->start_tick;
        out[n].cpu_ticks = t->cpu_ticks;
        n++;
    }
    irq_restore(f);
    return n;
}

int task_count(void) {
    int n = 0;
    for (int i = 0; i < MAX_TASKS; i++)
        if (is_alive(&tasks[i])) n++;
    return n;
}

int task_find(const char* name, int nth) {
    for (int i = 0; i < MAX_TASKS; i++) {
        if (is_alive(&tasks[i]) && strcmp(tasks[i].name, name) == 0 && nth-- == 0)
            return tasks[i].pid;
    }
    return -1;
}

const char* task_state_name(task_state_t s) {
    switch (s) {
    case TASK_READY:    return "ready";
    case TASK_RUNNING:  return "running";
    case TASK_SLEEPING: return "sleeping";
    case TASK_BLOCKED:  return "blocked";
    case TASK_ZOMBIE:   return "zombie";
    default:            return "unused";
    }
}

/* ---------- interrupt-context entry points ---------- */

void task_tick(uint32_t now) {
    if (!tasking_on) return;
    current->cpu_ticks++;

    for (int i = 0; i < MAX_TASKS; i++) {
        task_t* t = &tasks[i];
        if (t->state == TASK_SLEEPING && (int32_t)(now - t->wake_tick) >= 0)
            t->state = TASK_READY;
    }

    /* the idle task gives up the CPU as soon as anyone else can run */
    if (current == idle || --quantum <= 0)
        need_resched = 1;
}

void task_request_resched(void) {
    need_resched = 1;
}

uint32_t task_schedule(uint32_t esp) {
    if (!tasking_on || !need_resched) return esp;
    need_resched = 0;

    current->esp = esp;
    if (current->state == TASK_RUNNING) current->state = TASK_READY;

    if (current->stack && *(uint32_t*)current->stack != STACK_CANARY) {
        notice("\n[kernel] task %d (%s) killed: stack overflow\n", current->pid, current->name);
        if (current->system) kpanic("stack overflow in a system task", NULL);
        current->state = TASK_ZOMBIE;
    }

    /* round robin: the first READY task after the current one wins */
    int start = (int)(current - tasks);
    task_t* next = idle;
    for (int i = 1; i <= MAX_TASKS; i++) {
        task_t* t = &tasks[(start + i) % MAX_TASKS];
        if (t != idle && t->state == TASK_READY) { next = t; break; }
    }

    /* free finished tasks; never the current one, we are still on its stack */
    for (int i = 1; i < MAX_TASKS; i++) {
        if (tasks[i].state == TASK_ZOMBIE && &tasks[i] != current)
            reap(&tasks[i]);
    }

    next->state = TASK_RUNNING;
    quantum = QUANTUM_TICKS;
    current = next;
    return next->esp;
}

/* A CPU exception: kill the faulting task if possible, panic otherwise. */
void task_fault(regs_t* r, const char* what) {
    if (!tasking_on || current->system) kpanic(what, r);

    notice("\n[kernel] task %d (%s) killed: %s at eip=%p\n",
           current->pid, current->name, what, (void*)r->eip);
    klog("task: pid %d (%s) fault: %s\n", current->pid, current->name, what);
    current->state = TASK_ZOMBIE;
    need_resched = 1;
}
