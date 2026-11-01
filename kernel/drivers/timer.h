/* timer.h - Programmable Interval Timer (8253/8254) */
#ifndef DRIVERS_TIMER_H
#define DRIVERS_TIMER_H

#include <stdint.h>

#define TIMER_HZ 100        /* one tick every 10 ms */

void     timer_init(void);
uint32_t timer_ticks(void);
uint32_t timer_ms(void);    /* milliseconds since boot */

/* Format a tick count as "hh:mm:ss" */
void     timer_format(uint32_t ticks, char* buf, uint32_t size);

#endif
