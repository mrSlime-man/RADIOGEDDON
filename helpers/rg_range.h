/**
 * @file rg_range.h
 * @brief Receive bands and range-scan planning (start / end / step).
 *
 * The CC1101 tunes three separate bands, and each firmware decides which
 * parts of them its radio driver accepts (Official: about 300-348, 387-464
 * and 779-928 MHz; RogueMaster, Momentum and Unleashed: 281-361, 378-481 and
 * 749-962 MHz). rg_range_probe_bands() measures the bands the radio in use
 * really accepts by asking its driver, and rg_range_plan() lays a start / end
 * / step grid over them, skipping every grid point that falls in a gap, so a
 * scan never asks the radio to tune a frequency it rejects (the firmware
 * asserts on those).
 *
 * The scanner measures one point at a time (narrowband RSSI); a sweep over
 * many points takes points x (dwell + retune) and can miss a short burst on a
 * point it is not visiting. rg_range_sweep_ms() gives that estimate.
 *
 * Pure C with no SDK includes; host-tested (test/test_range.c).
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define RG_RANGE_MAX_BANDS 4u

/* Most points one range scan holds (each needs one RgScanChannel). */
#define RG_RANGE_MAX_POINTS 256u

/* Step limits: 1 kHz (the radio's useful resolution for this) to 10 MHz. */
#define RG_RANGE_MIN_STEP_HZ 1000UL
#define RG_RANGE_MAX_STEP_HZ 10000000UL

/* Time a probe spends besides the dwell: retune and calibration plus the
 * 3 ms the scanner waits for the AGC (radiogeddon_subghz_probe_rssi_dwell). */
#define RG_RANGE_PROBE_OVERHEAD_MS 4u

typedef struct {
    uint32_t lo_hz;
    uint32_t hi_hz; // inclusive
} RgBand;

typedef struct {
    RgBand band[RG_RANGE_MAX_BANDS];
    size_t count; // sorted by lo_hz, not overlapping
} RgBandSet;

/** The widest bands any supported firmware's CC1101 driver accepts. */
extern const RgBand rg_range_cc1101_bands[3];

/** Answers whether the radio in use can tune @p hz (the driver's own check). */
typedef bool (*RgRangeValidFn)(uint32_t hz, void* context);

/**
 * For each candidate band, find the part @p valid accepts, to the hertz:
 * a point inside the band that is accepted is found first (the middle, then
 * 15 more evenly spaced points), then both edges by binary search. Assumes
 * the accepted part of one candidate is one interval, which holds for every
 * supported firmware. Writes the bands found (sorted) to @p out.
 */
void rg_range_probe_bands(
    const RgBand* candidates,
    size_t count,
    RgRangeValidFn valid,
    void* context,
    RgBandSet* out);

/** True if @p hz lies in one of the bands. */
bool rg_range_in_bands(const RgBandSet* bands, uint32_t hz);

typedef enum {
    RgRangeOk,
    RgRangeErrorOrder, // start above end
    RgRangeErrorStep, // step outside RG_RANGE_MIN_STEP_HZ..RG_RANGE_MAX_STEP_HZ
    RgRangeErrorNoPoints, // no grid point lies in a band
    RgRangeErrorTooMany, // more points than the limit given to rg_range_plan
} RgRangeResult;

/** Short English text for a result ("Too many points"). */
const char* rg_range_result_text(RgRangeResult result);

typedef struct {
    uint32_t first_k; // grid index of the segment's first point
    uint32_t count; // points in the segment
} RgRangeSegment;

typedef struct {
    uint32_t start_hz;
    uint32_t end_hz;
    uint32_t step_hz;
    RgRangeSegment segment[RG_RANGE_MAX_BANDS];
    size_t segments;
    uint32_t points; // tunable points (sum of the segments)
    uint32_t grid_points; // all grid points start..end, gaps included
} RgRange;

/**
 * Plan a scan of start, start + step, ... up to @p end_hz, keeping the points
 * inside @p bands. The counts are exact even when refused for having more
 * than @p max_points points, so the screen can say how many there would be.
 */
RgRangeResult rg_range_plan(
    RgRange* range,
    uint32_t start_hz,
    uint32_t end_hz,
    uint32_t step_hz,
    const RgBandSet* bands,
    uint32_t max_points);

/** Frequency of tunable point @p index (0 .. points - 1); 0 if out of range. */
uint32_t rg_range_frequency(const RgRange* range, uint32_t index);

/** Index of the tunable point nearest @p hz (0 if there are none). */
uint32_t rg_range_nearest_index(const RgRange* range, uint32_t hz);

/** Estimated sweep time in ms for @p points at @p dwell_ms each (saturates). */
uint32_t rg_range_sweep_ms(uint32_t points, uint32_t dwell_ms);

/**
 * Move @p hz by @p delta_hz within the bands. A result in a gap goes on to
 * the edge of the next band in that direction (the lower band's top edge when
 * going down, the upper band's bottom edge when going up); past the last band
 * it stays on that band's edge. Edges are rounded inwards to whole kHz.
 * A start outside every band first snaps to the nearest band.
 */
uint32_t rg_range_step(const RgBandSet* bands, uint32_t hz, int32_t delta_hz);
