/**
 * @file radiogeddon_rangescan.h
 * @brief Full edition: narrowband RSSI sweep over a start / end / step range.
 *
 * Like the Scanner (radiogeddon_scanner.h) but over a planned range
 * (rg_range): up to RG_RANGE_MAX_POINTS points spread over every band the
 * radio accepts, gaps skipped. A worker thread visits the points in turn,
 * listens for the dwell time and feeds the strongest reading into rg_scan
 * (noise floor, detection with hysteresis, peak hold, counter). One point is
 * measured at a time, so a short burst on a point the sweep is not visiting
 * is missed; the sweep time is shown so this is never a surprise.
 *
 * The object holds the results; the thread exists only between start() and
 * stop(), and nothing else may use the radio meanwhile. Memory: one
 * RgScanChannel per point, allocated with the object
 * (radiogeddon_rangescan_memory()), freed with it.
 */
#pragma once

#include "../radiogeddon_edition.h"

#if RG_FEATURE_RANGE_SCAN

#include <furi.h>
#include <storage/storage.h>
#include "radiogeddon_subghz.h"
#include "rg_range.h"
#include "rg_scan.h"
#include "rg_spectrum.h"
#if RG_FEATURE_WATERFALL
#include "rg_waterfall.h"
#endif

typedef struct RadioGeddonRangeScan RadioGeddonRangeScan;

/** Called from the scan thread when a point starts a new activity period. */
typedef void (*RadioGeddonRangeScanHitCallback)(uint32_t index, void* context);

typedef struct {
    RgRange range;
    uint32_t count; // points (range.points)
    RgSpectrumPoint point[RG_RANGE_MAX_POINTS];
    int8_t floor; // median noise floor, or RG_SPECTRUM_NONE
    int32_t hold_index; // point the sweep is holding on, or -1
    uint32_t position; // point being measured
    bool paused;
    bool calibrating; // some point has not finished its noise-floor warm-up
    uint32_t sweeps; // complete sweeps since start or recalibration
    uint32_t sweep_ms; // duration of the last complete sweep (0 until known)
    uint16_t dwell_ms;
    uint8_t threshold_db;
} RadioGeddonRangeSnapshot;

/** Heap an instance for @p points takes (check it fits before allocating). */
size_t radiogeddon_rangescan_memory(uint32_t points);

/**
 * Allocate for @p range (which must have planned RgRangeOk with at most
 * RG_RANGE_MAX_POINTS points); the range is copied.
 */
RadioGeddonRangeScan* radiogeddon_rangescan_alloc(
    RadioGeddonSubGhz* subghz,
    const RgRange* range,
    uint16_t dwell_ms,
    uint8_t threshold_db,
    bool hold_on_hit);
void radiogeddon_rangescan_free(RadioGeddonRangeScan* instance);

void radiogeddon_rangescan_set_callback(
    RadioGeddonRangeScan* instance,
    RadioGeddonRangeScanHitCallback callback,
    void* context);

/** Start the sweep thread (it opens the radio session). */
void radiogeddon_rangescan_start(RadioGeddonRangeScan* instance);
/** Stop the thread and release the radio. Results are kept. */
void radiogeddon_rangescan_stop(RadioGeddonRangeScan* instance);

/** Release a hold if holding; otherwise pause or resume. */
void radiogeddon_rangescan_toggle_pause(RadioGeddonRangeScan* instance);
/** Clear peak holds and counters (noise floors kept). */
void radiogeddon_rangescan_reset_peaks(RadioGeddonRangeScan* instance);
/** Forget every point's noise floor and measure it again (calibration). */
void radiogeddon_rangescan_recalibrate(RadioGeddonRangeScan* instance);

/** Compact copy of the results (any thread; @p out is large: heap or model). */
void radiogeddon_rangescan_snapshot(RadioGeddonRangeScan* instance, RadioGeddonRangeSnapshot* out);

/** Frequency of point @p index (0 if out of range). */
uint32_t radiogeddon_rangescan_frequency(RadioGeddonRangeScan* instance, uint32_t index);

/**
 * Write the results as CSV (a comment header describing the scan, then one
 * row per measured point). Returns false on a card error (the file is removed).
 */
bool radiogeddon_rangescan_save_csv(
    RadioGeddonRangeScan* instance,
    Storage* storage,
    const char* path,
    const char* preset_label);

#if RG_FEATURE_WATERFALL

/* The Waterfall screen's picture: 128 x 42 pixels, one row per sweep. */
#define RADIOGEDDON_WF_VIEW_W 128u
#define RADIOGEDDON_WF_VIEW_H 42u

/**
 * Keep the history of complete sweeps (rg_waterfall) in @p buf, @p bytes
 * long, owned by the caller and valid until the engine is freed. Call before
 * start(). False if the buffer holds less than one row.
 */
bool radiogeddon_rangescan_waterfall_attach(
    RadioGeddonRangeScan* instance,
    void* buf,
    size_t bytes);

/** What the Waterfall screen asks for. */
typedef struct {
    uint16_t scroll; // rows back from the newest sweep (clamped)
    uint16_t cursor; // display column (clamped)
    uint8_t span_db; // sensitivity: dB from the reference to the densest dither
    bool noise_comp; // reference = each column's noise floor
} RadioGeddonWaterfallRequest;

/** Everything the Waterfall screen draws, made under the engine's lock. */
typedef struct {
    uint8_t xbm[RADIOGEDDON_WF_VIEW_H * (RADIOGEDDON_WF_VIEW_W / 8u)];
    uint8_t seg_start[RADIOGEDDON_WF_VIEW_W / 8u]; // pixels where a band segment starts
    uint16_t drawn; // picture rows holding a sweep
    uint16_t filled; // sweeps stored
    uint16_t rows; // history capacity
    uint16_t scroll; // the request's, clamped
    uint16_t max_scroll;
    uint16_t columns;
    uint16_t cursor; // the request's, clamped
    uint16_t cursor_x; // first pixel of the cursor column
    uint16_t cursor_w; // its width in pixels
    uint32_t cursor_hz; // frequency OK tunes to (strongest point of the column)
    int16_t cursor_dbm; // the column in the top visible row, RG_WF_NO_DATA if none
    int16_t cursor_peak; // the column's strongest stored reading, RG_WF_NO_DATA if none
    bool have_peak;
    uint16_t peak_column;
    uint16_t peak_age;
    int16_t peak_dbm;
    uint32_t peak_hz;
    int8_t floor; // median noise floor, RG_WF_NO_FLOOR while calibrating
    uint8_t threshold_db;
    bool paused;
    bool calibrating;
    uint32_t sweeps; // sweeps since start or recalibration
    uint32_t stored; // sweeps added to the history (one more per new row)
    uint32_t sweep_ms; // last measured sweep (0 until known)
    uint32_t estimate_ms; // estimated sweep time from points and dwell
    uint32_t top_age_ms; // how long ago the top visible row ended
    uint32_t points;
} RadioGeddonWaterfallFrame;

/** Build the screen's frame (GUI thread, on the tick). */
void radiogeddon_rangescan_waterfall_frame(
    RadioGeddonRangeScan* instance,
    const RadioGeddonWaterfallRequest* request,
    RadioGeddonWaterfallFrame* out);

/** How the screen asks for a frame: the engine above, passed as a pointer so
 * the screen (a loadable module) needs no symbol of the app. */
typedef void (*RadioGeddonWaterfallFrameFn)(
    void* engine,
    const RadioGeddonWaterfallRequest* request,
    RadioGeddonWaterfallFrame* out);

/** radiogeddon_rangescan_waterfall_frame as a RadioGeddonWaterfallFrameFn. */
void radiogeddon_rangescan_waterfall_frame_fn(
    void* engine,
    const RadioGeddonWaterfallRequest* request,
    RadioGeddonWaterfallFrame* out);

/**
 * Write the stored sweeps as CSV: comment header, then one row per sweep
 * (newest first), its age in ms, then each column's reading in dBm (empty
 * where nothing was measured). False on a card error (the file is removed).
 */
bool radiogeddon_rangescan_waterfall_save_csv(
    RadioGeddonRangeScan* instance,
    Storage* storage,
    const char* path,
    const char* preset_label);

#endif /* RG_FEATURE_WATERFALL */

#endif
