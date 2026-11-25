#include <stddef.h>
#include "header/driver/cmos.h"
#include "header/cpu/portio.h"

#define CMOS_ADDRESS_PORT 0x70
#define CMOS_DATA_PORT    0x71

static uint8_t cmos_read(uint8_t reg) {
    out(CMOS_ADDRESS_PORT, (uint8_t)(reg | 0x80));
    return in(CMOS_DATA_PORT);
}

static bool cmos_update_in_progress(void) {
    return (cmos_read(0x0A) & 0x80) != 0;
}

static uint8_t bcd_to_binary(uint8_t value) {
    return (uint8_t)((value & 0x0F) + ((value >> 4) * 10));
}

bool cmos_read_time(struct TimeInfo *out) {
    if (out == NULL)
        return false;

    uint8_t second;
    uint8_t minute;
    uint8_t hour;

    do {
        while (cmos_update_in_progress()) {
        }
        second = cmos_read(0x00);
        minute = cmos_read(0x02);
        hour   = cmos_read(0x04);
    } while (cmos_update_in_progress() || second != cmos_read(0x00));

    uint8_t status_b = cmos_read(0x0B);
    bool pm = (hour & 0x80) != 0;
    hour &= 0x7F;

    if ((status_b & 0x04) == 0) {
        second = bcd_to_binary(second);
        minute = bcd_to_binary(minute);
        hour   = bcd_to_binary(hour);
    }

    if ((status_b & 0x02) == 0) {
        if (hour == 12) hour = 0;
        if (pm) hour = (uint8_t)(hour + 12);
    }

    if (hour > 23 || minute > 59 || second > 59)
        return false;

    out->hour = hour;
    out->minute = minute;
    out->second = second;
    return true;
}
