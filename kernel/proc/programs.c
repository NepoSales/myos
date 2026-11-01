/* programs.c - demo programs that run as independent kernel threads
 *
 * Each one exercises a different part of the system:
 *   primes   CPU-bound work: shows preemption (the shell stays responsive)
 *   hog      an infinite busy loop: shows that only time slices matter
 *   logger   periodic sleep + file writes: tasks sharing the file system
 *   spinner  animation on the status bar: many tasks running at once
 *   crash    divides by zero: the kernel kills the task, not the system
 */

#include "proc/programs.h"
#include "fs/fs.h"
#include "mm/heap.h"
#include "mm/pmm.h"
#include "drivers/timer.h"
#include "drivers/rtc.h"
#include "drivers/vga.h"
#include "lib/printf.h"
#include "lib/string.h"

static int is_prime(int n) {
    if (n < 2) return 0;
    if (n % 2 == 0) return n == 2;
    for (int d = 3; d * d <= n; d += 2)
        if (n % d == 0) return 0;
    return 1;
}

static void prog_primes(const char* args) {
    int limit = 1000000;
    if (args[0] && (!parse_int(args, &limit) || limit < 2)) {
        kprintf("usage: primes [limit >= 2]\n");
        return;
    }

    uint32_t start = timer_ms();
    int found = 0;
    for (int n = 2; n <= limit; n++)
        found += is_prime(n);
    uint32_t elapsed = timer_ms() - start;

    char line[96];
    ksnprintf(line, sizeof(line), "%d primes up to %d (computed in %u ms by pid %d)\n",
              found, limit, elapsed, task_current()->pid);
    fs_write("/tmp/primes.txt", line, strlen(line), 0);
    kprintf("\n[primes] %d primes up to %d in %u ms, saved to /tmp/primes.txt\n",
            found, limit, elapsed);
}

static void prog_hog(const char* args) {
    (void)args;
    for (volatile uint32_t i = 0;; i++)
        ;
}

static void prog_logger(const char* args) {
    int interval = 5;
    if (args[0] && (!parse_int(args, &interval) || interval < 1)) {
        kprintf("usage: logger [seconds >= 1]\n");
        return;
    }

    const char* path = "/var/log/system.log";
    for (;;) {
        char when[24], line[96];
        heap_stats_t hs;
        heap_get_stats(&hs);
        rtc_format(rtc_now(), when, sizeof(when));
        ksnprintf(line, sizeof(line), "[%s] tasks=%d heap_used=%uK free_ram=%uK\n",
                  when, task_count(), hs.used / 1024,
                  (pmm_total_pages() - pmm_used_pages()) * 4);
        fs_write(path, line, strlen(line), 1);
        task_sleep((uint32_t)interval * 1000);
    }
}

static void prog_spinner(const char* args) {
    (void)args;
    static const char frames[] = "|/-\\";
    static const uint8_t colors[SPINNER_SLOTS] = {
        VGA_YELLOW, VGA_LIGHT_GREEN, VGA_LIGHT_CYAN, VGA_LIGHT_MAGENTA,
        VGA_LIGHT_RED, VGA_WHITE, VGA_LIGHT_BLUE, VGA_LIGHT_GREY,
    };
    int slot = task_current()->pid % SPINNER_SLOTS;
    char cell[2] = { 0, 0 };
    for (int i = 0;; i++) {
        cell[0] = frames[i % 4];
        status_write(SPINNER_COL + slot, cell, VGA_COLOR(colors[slot], VGA_BLUE));
        task_sleep(100);
    }
}

static void prog_crash(const char* args) {
    (void)args;
    kprintf("crash: dividing by zero...\n");
    volatile int zero = 0;
    volatile int result = 42 / zero;
    kprintf("crash: this line is never printed (%d)\n", result);
}

static const program_t programs[] = {
    { "primes",  "primes [limit]",   "count primes up to limit (default 1000000)", prog_primes  },
    { "hog",     "hog",              "busy-loop forever (kill it with 'kill')",    prog_hog     },
    { "logger",  "logger [seconds]", "append system stats to /var/log/system.log", prog_logger  },
    { "spinner", "spinner",          "animate a spinner on the status bar",        prog_spinner },
    { "crash",   "crash",            "trigger a CPU exception (divide by zero)",   prog_crash   },
};

#define NUM_PROGRAMS (int)(sizeof(programs) / sizeof(programs[0]))

const program_t* program_find(const char* name) {
    for (int i = 0; i < NUM_PROGRAMS; i++)
        if (strcmp(programs[i].name, name) == 0) return &programs[i];
    return NULL;
}

const program_t* program_get(int index) {
    return index >= 0 && index < NUM_PROGRAMS ? &programs[index] : NULL;
}
