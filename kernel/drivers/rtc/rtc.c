#include <lib/printf.h>
#include <rexos/io.h>
#include <rexos/rtc.h>

#define CMOS_ADDR 0x70
#define CMOS_DATA 0x71

static int get_update_in_progress_flag() {
    outb(CMOS_ADDR, 0x0A);
    return (inb(CMOS_DATA) & 0x80);
}

static uint8_t get_rtc_register(int reg) {
    outb(CMOS_ADDR, reg);
    return inb(CMOS_DATA);
}

void rtc_get_time(rtc_time_t *time) {
    /* Várjunk, amíg nincs frissítés folyamatban */
    while (get_update_in_progress_flag());

    time->second = get_rtc_register(0x00);
    time->minute = get_rtc_register(0x02);
    time->hour   = get_rtc_register(0x04);
    time->day    = get_rtc_register(0x07);
    time->month  = get_rtc_register(0x08);
    time->year   = get_rtc_register(0x09);

    uint8_t registerB = get_rtc_register(0x0B);

    /* BCD átalakítás (ha szükséges) */
    if (!(registerB & 0x04)) {
        time->second = (time->second & 0x0F) + ((time->second / 16) * 10);
        time->minute = (time->minute & 0x0F) + ((time->minute / 16) * 10);
        time->hour   = ((time->hour & 0x0F) + (((time->hour & 0x70) / 16) * 10)) | (time->hour & 0x80);
        time->day    = (time->day & 0x0F) + ((time->day / 16) * 10);
        time->month  = (time->month & 0x0F) + ((time->month / 16) * 10);
        time->year   = (time->year & 0x0F) + ((time->year / 16) * 10);
    }

    /* 12h -> 24h átalakítás */
    if (!(registerB & 0x02) && (time->hour & 0x80)) {
        time->hour = ((time->hour & 0x7F) + 12) % 24;
    }

    /* Évszázad korrekció (egyszerűsítve) */
    time->year += 2000;
}
