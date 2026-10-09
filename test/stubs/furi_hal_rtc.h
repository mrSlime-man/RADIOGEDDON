/* Host stand-in: the clock reads a time the test sets (stub_datetime). */
#pragma once

#include <datetime/datetime.h>

void furi_hal_rtc_get_datetime(DateTime* datetime);

extern DateTime stub_datetime;
