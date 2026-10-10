/* Host stand-in for the firmware HAL header that lib/subghz includes
 * (test_fwdecode). The decoders take nothing from the HAL itself; the firmware
 * header also brings in <math.h>, which some of them use without including,
 * and (through its Sub-GHz part) level_duration.h. */
#pragma once

#include <math.h>

#include "furi.h"
#include "furi_hal_rtc.h"
#include <lib/toolbox/level_duration.h>
