/* Host stand-in for the firmware date helpers (UTC, via the C library). */
#pragma once

#include <stdint.h>

typedef struct {
    uint8_t hour;
    uint8_t minute;
    uint8_t second;
    uint8_t day;
    uint8_t month;
    uint16_t year;
    uint8_t weekday;
} DateTime;

void datetime_timestamp_to_datetime(uint32_t timestamp, DateTime* datetime);
