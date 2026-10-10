/**
 * @file rg_scan.h
 * @brief Firmware-independent logic for the narrowband RSSI scanner.
 *
 * The CC1101 measures one frequency at a time, so the scanner is a sequential
 * narrowband RSSI sweep, not a wideband spectrum analyzer. This module holds
 * the per-frequency bookkeeping that turns those sequential RSSI readings into
 * a noise-floor estimate, activity detection with hysteresis, a peak hold and
 * an activity counter. It has no furi/SDK includes so it can be unit-tested on
 * a host compiler (test/test_scan.c).
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/** Readings needed before a channel's noise floor is trusted for detection. */
#define RG_SCAN_WARMUP_SAMPLES 4u
/** RSSI value used for "no reading yet". */
#define RG_SCAN_RSSI_NONE      (-127.0f)

typedef struct {
    float threshold_db; // activity when RSSI >= floor + threshold_db
    float hysteresis_db; // activity ends when RSSI < floor + threshold_db - hysteresis_db
    float abs_min_dbm; // never report activity below this absolute level
} RgScanParams;

typedef struct {
    float last; // most recent reading, dBm
    float peak; // highest reading since the last peak reset, dBm
    float floor; // estimated noise floor, dBm
    uint32_t samples; // readings taken since reset
    uint32_t last_hit_ms; // timestamp of the most recent activity start
    uint16_t hits; // number of distinct activity starts
    bool active; // currently above threshold
} RgScanChannel;

/** Default detection parameters (+10 dB over the floor, 3 dB hysteresis). */
void rg_scan_params_default(RgScanParams* params);

/** Clear one channel completely (floor, peak, counters). */
void rg_scan_channel_reset(RgScanChannel* ch);

/** Clear only the peak hold and the activity counter, keeping the floor. */
void rg_scan_channel_reset_peak(RgScanChannel* ch);

/**
 * Feed one RSSI reading into a channel.
 *
 * The noise floor follows quiet readings: it drops quickly toward lower
 * readings and rises slowly toward higher ones, and it is frozen while the
 * channel is active so a long transmission cannot raise its own floor.
 *
 * @return true exactly when this reading starts a new activity period.
 */
bool rg_scan_channel_update(
    RgScanChannel* ch,
    float rssi,
    const RgScanParams* params,
    uint32_t now_ms);

/**
 * Overall noise floor: the median of the floors of channels that have
 * finished warming up. Returns RG_SCAN_RSSI_NONE if none have.
 */
float rg_scan_global_floor(const RgScanChannel* channels, size_t count);

/** True if @p frequency_hz lies in [@p lo_hz, @p hi_hz]. */
bool rg_scan_in_band(uint32_t frequency_hz, uint32_t lo_hz, uint32_t hi_hz);

/**
 * Bitmask with bit i set for every frequencies[i] inside [lo_hz, hi_hz]
 * (at most 32 entries are considered).
 */
uint32_t
    rg_scan_band_mask(const uint32_t* frequencies, size_t count, uint32_t lo_hz, uint32_t hi_hz);

/**
 * Format one CSV result row: "Freq_Hz,Last_dBm,Peak_dBm,Floor_dBm,Hits".
 * Returns the number of characters written (excluding NUL), or 0 if @p size is
 * too small.
 */
size_t rg_scan_format_row(char* out, size_t size, uint32_t frequency_hz, const RgScanChannel* ch);
