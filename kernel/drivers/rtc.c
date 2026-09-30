#include <kern/io.h>
#include <kern/rtc.h>
#include <stdbool.h>

static uint8_t read_register(uint8_t number) {
    outb(0x70, number);
    return inb(0x71);
}

static uint8_t from_bcd(uint8_t value) {
    return (uint8_t)((value & 0x0f) + ((value >> 4) * 10));
}

static int64_t days_from_civil(int64_t year, unsigned month, unsigned day) {
    year -= month <= 2;
    int64_t era = (year >= 0 ? year : year - 399) / 400;
    unsigned year_of_era = (unsigned)(year - era * 400);
    int month_adjusted = (int)month + (month > 2 ? -3 : 9);
    unsigned day_of_year = (unsigned)((153 * month_adjusted + 2) / 5) +
                           day - 1;
    unsigned day_of_era = year_of_era * 365 + year_of_era / 4 -
                          year_of_era / 100 + day_of_year;
    return era * 146097 + (int64_t)day_of_era - 719468;
}

int rtc_read_unix_seconds(int64_t *seconds) {
    if (!seconds) return -1;
    for (unsigned attempt = 0; attempt < 1000; ++attempt) {
        if (read_register(0x0a) & 0x80) continue;
        uint8_t second = read_register(0x00);
        uint8_t minute = read_register(0x02);
        uint8_t hour = read_register(0x04);
        uint8_t day = read_register(0x07);
        uint8_t month = read_register(0x08);
        uint8_t year = read_register(0x09);
        uint8_t century = read_register(0x32);
        uint8_t status_b = read_register(0x0b);
        if (second != read_register(0x00)) continue;
        bool binary = (status_b & 0x04) != 0;
        bool twenty_four_hour = (status_b & 0x02) != 0;
        bool afternoon = (hour & 0x80) != 0;
        hour &= 0x7f;
        if (!binary) {
            second = from_bcd(second);
            minute = from_bcd(minute);
            hour = from_bcd(hour);
            day = from_bcd(day);
            month = from_bcd(month);
            year = from_bcd(year);
            century = from_bcd(century);
        }
        if (!twenty_four_hour) {
            hour %= 12;
            if (afternoon) hour += 12;
        }
        if (century < 19 || century > 99) century = 20;
        int64_t full_year = (int64_t)century * 100 + year;
        if (second > 59 || minute > 59 || hour > 23 || day < 1 || day > 31 ||
            month < 1 || month > 12) return -1;
        *seconds = days_from_civil(full_year, month, day) * 86400 +
                   hour * 3600 + minute * 60 + second;
        return 0;
    }
    return -1;
}
