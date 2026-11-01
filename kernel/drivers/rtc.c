/* rtc.c - CMOS real-time clock driver
 *
 * The CMOS chip keeps date and time while the machine is off. Its
 * registers are read through ports 0x70 (index) and 0x71 (data) and are
 * usually stored in BCD. The clock has no time zone: QEMU sets it to UTC
 * by default.
 */

#include "drivers/rtc.h"
#include "drivers/timer.h"
#include "lib/printf.h"
#include "arch/io.h"

#define CMOS_INDEX  0x70
#define CMOS_DATA   0x71

static uint32_t boot_time = 0;      /* Unix time at boot */
static uint32_t boot_ticks = 0;

static uint8_t cmos_read(uint8_t reg) {
    outb(CMOS_INDEX, reg);
    return inb(CMOS_DATA);
}

static int update_in_progress(void) {
    return cmos_read(0x0A) & 0x80;
}

static uint8_t bcd_to_bin(uint8_t v) {
    return (uint8_t)((v & 0x0F) + (v >> 4) * 10);
}

/* Days since 1970-01-01 for a civil date (Howard Hinnant's algorithm) */
static uint32_t days_from_civil(int y, int m, int d) {
    y -= m <= 2;
    int era = y / 400;
    int yoe = y - era * 400;
    int doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    int doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return (uint32_t)(era * 146097 + doe - 719468);
}

void rtc_init(void) {
    uint8_t sec, min, hour, day, mon, year;

    /* read twice until two consecutive reads agree, so we never catch the
     * clock in the middle of an update */
    do {
        while (update_in_progress()) ;
        sec = cmos_read(0x00); min = cmos_read(0x02); hour = cmos_read(0x04);
        day = cmos_read(0x07); mon = cmos_read(0x08); year = cmos_read(0x09);
        while (update_in_progress()) ;
    } while (sec != cmos_read(0x00) || min != cmos_read(0x02));

    uint8_t status_b = cmos_read(0x0B);
    int pm = hour & 0x80;
    hour &= 0x7F;
    if (!(status_b & 0x04)) {           /* BCD mode */
        sec = bcd_to_bin(sec); min = bcd_to_bin(min); hour = bcd_to_bin(hour);
        day = bcd_to_bin(day); mon = bcd_to_bin(mon); year = bcd_to_bin(year);
    }
    if (!(status_b & 0x02) && pm)       /* 12-hour mode */
        hour = (uint8_t)((hour % 12) + 12);

    boot_time = days_from_civil(2000 + year, mon, day) * 86400u
              + hour * 3600u + min * 60u + sec;
    boot_ticks = timer_ticks();
}

uint32_t rtc_now(void) {
    return boot_time + (timer_ticks() - boot_ticks) / TIMER_HZ;
}

void rtc_format(uint32_t t, char* buf, uint32_t size) {
    uint32_t days = t / 86400, secs = t % 86400;

    /* inverse of days_from_civil */
    int z = (int)days + 719468;
    int era = z / 146097;
    int doe = z - era * 146097;
    int yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    int doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    int mp = (5 * doy + 2) / 153;
    int d = doy - (153 * mp + 2) / 5 + 1;
    int m = mp + (mp < 10 ? 3 : -9);
    int y = yoe + era * 400 + (m <= 2);

    ksnprintf(buf, size, "%04d-%02d-%02d %02u:%02u:%02u",
              y, m, d, secs / 3600, (secs / 60) % 60, secs % 60);
}
