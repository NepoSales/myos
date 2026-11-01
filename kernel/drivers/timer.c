/* timer.c - PIT channel 0 programmed as the system clock (IRQ 0)
 *
 * Each tick advances the uptime counter and drives the scheduler, which is
 * what makes multitasking preemptive.
 */

#include "drivers/timer.h"
#include "arch/idt.h"
#include "arch/io.h"
#include "proc/task.h"
#include "lib/printf.h"

#define PIT_FREQ    1193182
#define PIT_CH0     0x40
#define PIT_CMD     0x43

static volatile uint32_t ticks = 0;

static void timer_irq(regs_t* r) {
    (void)r;
    ticks++;
    task_tick(ticks);
}

void timer_init(void) {
    uint32_t divisor = PIT_FREQ / TIMER_HZ;
    outb(PIT_CMD, 0x36);                  /* channel 0, lo/hi byte, mode 3 (square wave) */
    outb(PIT_CH0, divisor & 0xFF);
    outb(PIT_CH0, (divisor >> 8) & 0xFF);
    irq_register(0, timer_irq);
}

uint32_t timer_ticks(void) { return ticks; }
uint32_t timer_ms(void) { return ticks * (1000 / TIMER_HZ); }

void timer_format(uint32_t t, char* buf, uint32_t size) {
    uint32_t s = t / TIMER_HZ;
    ksnprintf(buf, size, "%02u:%02u:%02u", s / 3600, (s / 60) % 60, s % 60);
}
