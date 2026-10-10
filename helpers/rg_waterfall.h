/**
 * @file rg_waterfall.h
 * @brief Full edition: RSSI sweep history ("waterfall") of a range scan.
 *
 * The CC1101 measures one narrowband frequency at a time. A range scan visits
 * its points in turn; this module keeps, for each complete sweep, the reading
 * of every display column, in a fixed-size circular buffer, and renders that
 * history as a monochrome picture: frequency across, time down (newest row on
 * top), intensity as an ordered-dither density.
 *
 * It is a history of sequential RSSI sweeps, never a wideband or IQ capture:
 * a burst on a point the sweep is not visiting is not seen, and nothing is
 * drawn for a column no point of that sweep was measured in (such a cell is
 * marked "missing", drawn with its own pattern, never filled in).
 *
 * Columns: a scan of P points has min(P, RG_WF_MAX_COLUMNS) columns; with
 * more points than columns a column holds the strongest of its points'
 * readings in that sweep. The x axis follows the scan's point order, so the
 * gaps the radio cannot tune (rg_range) take no width.
 *
 * Memory: one caller-provided buffer of @c rg_waterfall_bytes() bytes, split
 * into rows of (columns cells + a 4-byte timestamp). A cell is one byte
 * (dBm + RG_WF_CELL_BIAS, 0 = missing). Nothing is allocated here.
 *
 * Pure C with no SDK includes; host-tested (test/test_waterfall.c).
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define RG_WF_MAX_COLUMNS  128u
#define RG_WF_MAX_ROWS     255u
/* A cell stores dBm + bias: -159..+95 dBm fit, 0 means "no measurement". */
#define RG_WF_CELL_BIAS    160
#define RG_WF_NO_DATA      INT16_MIN
/* No noise floor known for a column. */
#define RG_WF_NO_FLOOR     INT8_MIN
/* Dither levels: 0 (blank) .. RG_WF_LEVEL_MAX; RG_WF_LEVEL_STRONG is solid. */
#define RG_WF_LEVEL_MAX    12u
#define RG_WF_LEVEL_STRONG 16u
/* Bytes a row takes besides its cells (end-of-sweep timestamp). */
#define RG_WF_ROW_EXTRA    4u
/* Reference level for absolute (uncompensated) intensity. */
#define RG_WF_ABS_REF_DBM  (-105)

typedef struct {
    uint8_t* cells; // rows x columns, row-major
    uint32_t* stamp; // rows: tick (ms) at the end of each sweep
    uint32_t points; // scan points
    uint16_t columns; // min(points, RG_WF_MAX_COLUMNS)
    uint16_t rows; // capacity
    uint16_t head; // next row to write
    uint16_t filled; // rows holding a sweep (<= rows)
    uint32_t sweeps; // sweeps committed since clear (may exceed rows)
    uint32_t missing; // committed cells with no measurement
    uint8_t cur[RG_WF_MAX_COLUMNS]; // sweep being measured
    bool cur_any; // the current sweep has at least one reading
} RgWaterfall;

/** Bytes a history of @p rows rows for @p points points needs. */
size_t rg_waterfall_bytes(uint32_t points, uint16_t rows);

/** Rows a buffer of @p bytes holds for @p points points (capped at RG_WF_MAX_ROWS). */
uint16_t rg_waterfall_rows_for(uint32_t points, size_t bytes);

/**
 * Lay the history out over @p buf (@p bytes long, any alignment). False when
 * @p points is 0 or the buffer holds less than one row.
 */
bool rg_waterfall_init(RgWaterfall* wf, void* buf, size_t bytes, uint32_t points);

/** Forget every row and the sweep in progress. */
void rg_waterfall_clear(RgWaterfall* wf);

/** Display column of scan point @p index. */
uint16_t rg_waterfall_column_of(const RgWaterfall* wf, uint32_t index);

/** First and last scan point a column holds (inclusive). */
void rg_waterfall_points_of(
    const RgWaterfall* wf,
    uint16_t column,
    uint32_t* first,
    uint32_t* last);

/** Record a reading of point @p index in the sweep in progress (keeps the strongest). */
void rg_waterfall_add(RgWaterfall* wf, uint32_t index, float dbm);

/** Drop the sweep in progress (e.g. after a recalibration). */
void rg_waterfall_discard(RgWaterfall* wf);

/**
 * The sweep is complete: store it as the newest row, stamped @p now_ms.
 * A sweep with no reading at all is dropped (false).
 */
bool rg_waterfall_commit(RgWaterfall* wf, uint32_t now_ms);

/** Reading of @p column @p age rows back (0 = newest), or RG_WF_NO_DATA. */
int16_t rg_waterfall_cell(const RgWaterfall* wf, uint16_t age, uint16_t column);

/** Timestamp of the row @p age back (0 if there is none). */
uint32_t rg_waterfall_stamp(const RgWaterfall* wf, uint16_t age);

/**
 * Strongest stored reading: its column, age and dBm. False if no row holds a
 * reading.
 */
bool rg_waterfall_peak(const RgWaterfall* wf, uint16_t* column, uint16_t* age, int16_t* dbm);

/** How the history is turned into dither levels. */
typedef struct {
    uint8_t span_db; // dB from the reference to the densest non-strong level
    uint8_t strong_db; // at or above reference + this: solid (strong signal)
    bool noise_comp; // reference = the column's noise floor (else RG_WF_ABS_REF_DBM)
    const int8_t* floors; // per column floor, RG_WF_NO_FLOOR if unknown (may be NULL)
    int8_t floor_all; // used where a column's floor is unknown (RG_WF_NO_FLOOR: absolute)
} RgWaterfallStyle;

/**
 * Dither level of a reading: 0 at or below the reference, rising linearly to
 * RG_WF_LEVEL_MAX at reference + span, RG_WF_LEVEL_STRONG at or above
 * reference + strong_db. @p floor is the column's reference when noise
 * compensation is on (RG_WF_NO_FLOOR falls back to the absolute reference).
 */
uint8_t rg_waterfall_level(int16_t dbm, int8_t floor, const RgWaterfallStyle* style);

/** Whether the pixel (x, y) is set for a dither level (4x4 ordered dither). */
bool rg_waterfall_dither(uint8_t level, uint16_t x, uint16_t y);

/** Whether the pixel (x, y) is set in a missing cell (sparse vertical dots). */
bool rg_waterfall_missing_dot(uint16_t x, uint16_t y);

/** Display column under pixel @p x of a @p width pixel wide picture. */
uint16_t rg_waterfall_column_at(const RgWaterfall* wf, uint16_t x, uint16_t width);

/** First pixel and pixel count of @p column in a @p width pixel picture. */
void rg_waterfall_column_span(
    const RgWaterfall* wf,
    uint16_t column,
    uint16_t width,
    uint16_t* x,
    uint16_t* w);

/**
 * Render @p height rows, starting @p scroll rows back from the newest, into
 * an XBM bitmap (rows of (width + 7) / 8 bytes, LSB = leftmost pixel) of
 * @p width x @p height pixels. Rows with no stored sweep stay blank. Returns
 * the number of picture rows that hold a sweep.
 */
uint16_t rg_waterfall_render(
    const RgWaterfall* wf,
    const RgWaterfallStyle* style,
    uint16_t scroll,
    uint8_t* xbm,
    uint16_t width,
    uint16_t height);

/** Largest useful scroll for a @p height row picture (0 when it all fits). */
uint16_t rg_waterfall_max_scroll(const RgWaterfall* wf, uint16_t height);
