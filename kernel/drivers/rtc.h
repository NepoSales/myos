/* rtc.h - CMOS real-time clock (wall-clock time) */
#ifndef DRIVERS_RTC_H
#define DRIVERS_RTC_H

#include <stdint.h>

/* Read the CMOS clock once at boot. Afterwards the time is derived from
 * the boot time plus the PIT uptime, so the CMOS is never read again. */
void     rtc_init(void);

/* Current time as a Unix timestamp (seconds since 1970-01-01 00:00:00) */
uint32_t rtc_now(void);

/* Format a Unix timestamp as "YYYY-MM-DD hh:mm:ss" */
void     rtc_format(uint32_t timestamp, char* buf, uint32_t size);

#endif
