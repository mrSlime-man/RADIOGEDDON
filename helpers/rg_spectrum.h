/**
 * @file rg_spectrum.h
 * @brief Compact per-point results of a range scan, and how they are drawn.
 *
 * A range scan keeps one rg_scan channel per point (noise floor, detection,
 * peak hold, counter). The screen gets a compact copy (RgSpectrumPoint, 5
 * bytes a point, whole dBm), which this module turns into display columns,
 * the overall noise floor (median over every warmed-up point, not just the
 * first ones) and the strongest point.
 *
 * Every reading is one narrowband RSSI measurement at one frequency at one
 * moment; the picture is a sequential sweep, not a wideband spectrum.
 *
 * Pure C with no SDK includes; host-tested (test/test_spectrum.c).
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "rg_scan.h"

enum {
    RgSpectrumMeasured = 1 << 0, // at least one reading
    RgSpectrumWarm = 1 << 1, // noise floor trusted (RG_SCAN_WARMUP_SAMPLES readings)
    RgSpectrumActive = 1 << 2, // above threshold now
};

/* "No reading" in the compact form (dBm). */
#define RG_SPECTRUM_NONE INT8_MIN

typedef struct {
    int8_t last; // dBm, RG_SPECTRUM_NONE if never measured
    int8_t peak;
    int8_t floor;
    uint8_t hits; // activity periods, saturating at 255
    uint8_t flags; // RgSpectrum* bits
} RgSpectrumPoint;

/** Compact copy of one channel (dBm rounded to the nearest whole number). */
void rg_spectrum_from_channel(RgSpectrumPoint* out, const RgScanChannel* ch);

/** Median noise floor over every warm point, or RG_SPECTRUM_NONE if none. */
int8_t rg_spectrum_median_floor(const RgSpectrumPoint* points, size_t count);

/** Index of the point with the highest peak hold; @p count if none measured. */
size_t rg_spectrum_strongest(const RgSpectrumPoint* points, size_t count);

/**
 * Bin @p count points into @p width columns: each column shows the strongest
 * last reading and peak of the points it covers (RG_SPECTRUM_NONE if none were
 * measured) and the OR of their flags. With fewer points than columns a point
 * spans several columns.
 */
void rg_spectrum_columns(
    const RgSpectrumPoint* points,
    size_t count,
    size_t width,
    int8_t* col_last,
    int8_t* col_peak,
    uint8_t* col_flags);

/** Column the point @p index is drawn in (the middle one if it spans several). */
size_t rg_spectrum_column_of(size_t index, size_t count, size_t width);
