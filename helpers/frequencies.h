#pragma once

#include <stdint.h>
#include <stddef.h>

/**
 * Common Sub-GHz frequencies (Hz) used for scanning and manual selection.
 *
 * These are the frequencies the Flipper's CC1101 can tune. Whether a given
 * frequency may legally be *transmitted* on is a separate, region-dependent
 * question that is always checked at TX time via the firmware region API -
 * see helpers/radio.c. This table only drives tuning/scanning in RX.
 */

typedef struct {
    uint32_t frequency; /**< frequency in Hz */
    const char* label; /**< short human label, e.g. "433.92" */
} RgFrequency;

extern const RgFrequency rg_frequencies[];
extern const size_t rg_frequencies_count;

/** Index of the default frequency (433.92 MHz) in rg_frequencies. */
extern const size_t rg_frequencies_default_index;

/**
 * Subset of frequencies used by the Frequency Hopper. Kept small and focused
 * on the most active bands so each dwell is long enough to actually decode.
 */
extern const uint32_t rg_hopper_frequencies[];
extern const size_t rg_hopper_frequencies_count;
