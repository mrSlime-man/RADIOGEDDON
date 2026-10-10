/**
 * @file rg_timeline.h
 * @brief Pure layout and navigation maths for the pulse timeline view.
 *
 * The timeline shows a RAW recording as a high/low waveform, one screen column
 * per `us_per_px` microseconds. Only a window of samples is held in RAM (see
 * views/radiogeddon_timeline_view.h); these helpers decide what each column
 * shows, where duration labels go, how panning and zooming move the view, and
 * when the window must be reloaded from the file. No SDK includes, so it is
 * unit-tested on the host (test/test_timeline.c).
 */
#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#define RG_TL_ZOOM_COUNT 10
/* Column flags from rg_timeline_raster(). */
#define RG_TL_HIGH       0x01u
#define RG_TL_LOW        0x02u

/** Microseconds per screen column at each zoom step, finest first. */
extern const uint32_t rg_timeline_zoom_us[RG_TL_ZOOM_COUNT];

/** A duration label for one pulse wide enough to print it under. */
typedef struct {
    int16_t x; /* centre column */
    bool high;
    uint32_t us;
} RgTlLabel;

/**
 * Mark each of @p width columns with RG_TL_HIGH / RG_TL_LOW for the levels
 * the samples hold inside it. Column c covers
 * [left_us + c * us_per_px, left_us + (c + 1) * us_per_px). @p first_us is
 * the recording time of samples[0]. Columns without samples stay 0.
 */
void rg_timeline_raster(
    const int32_t* samples,
    size_t count,
    uint64_t first_us,
    uint64_t left_us,
    uint32_t us_per_px,
    uint8_t* cols,
    size_t width);

/** Labels for pulses fully on screen and at least @p min_px columns wide. */
size_t rg_timeline_labels(
    const int32_t* samples,
    size_t count,
    uint64_t first_us,
    uint64_t left_us,
    uint32_t us_per_px,
    size_t width,
    uint32_t min_px,
    RgTlLabel* out,
    size_t max);

/** Keep the view inside the recording: 0 <= left <= total - span. */
uint64_t rg_timeline_clamp(uint64_t left_us, uint64_t span_us, uint64_t total_us);

/** Pan by a quarter screen; @p dir is -1 or +1. */
uint64_t
    rg_timeline_pan(uint64_t left_us, int dir, uint32_t us_per_px, size_t width, uint64_t total_us);

/**
 * Change zoom by @p dir steps (negative = finer) keeping the screen centre in
 * place. Updates @p zoom and @p left_us; returns false at either end.
 */
bool rg_timeline_zoom(int* zoom, int dir, uint64_t* left_us, size_t width, uint64_t total_us);

/** Finest zoom step whose screen spans at least @p detail_us. */
int rg_timeline_zoom_for(uint64_t detail_us, size_t width);

/**
 * Whether the visible span [left, left + span) is inside the loaded window
 * [win_first, win_end). A window that starts at the recording start or ends
 * at its end covers everything before or after it.
 */
bool rg_timeline_covered(
    uint64_t win_first_us,
    uint64_t win_end_us,
    bool at_end,
    uint64_t left_us,
    uint64_t span_us);

/**
 * Frame navigation. A frame is "current" when it starts at or before the
 * marker one eighth into the screen. @p dir +1 finds the next frame, -1 the
 * previous; returns its index or @p count when there is none, and writes the
 * left edge that puts it on the marker.
 */
size_t rg_timeline_frame_step(
    const uint64_t* starts,
    size_t count,
    uint64_t left_us,
    uint64_t span_us,
    int dir,
    uint64_t* new_left);

/** Index of the current frame (see above), or @p count before the first. */
size_t
    rg_timeline_frame_at(const uint64_t* starts, size_t count, uint64_t left_us, uint64_t span_us);
