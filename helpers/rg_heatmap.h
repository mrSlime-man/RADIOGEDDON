/**
 * @file rg_heatmap.h
 * @brief Full edition: RF activity heatmap from RF Activity Logger files.
 *
 * Frequency down, time across, one cell per (frequency, column). The CC1101
 * listens to one frequency at a time, so a cell is one of:
 *
 *   - not observed: no visit to that frequency in that time (the log's row
 *     says "visits 0", or there is no row at all): drawn blank, never as
 *     silence;
 *   - observed, quiet: listened to, nothing above the threshold;
 *   - active: activity seen, at one of three levels (by bursts relative to
 *     the busiest cell, or by how far the peak rose above the noise floor).
 *
 * Each cell also keeps its coverage (listening time over the cell's time
 * span): a quiet cell with 3% coverage says little.
 *
 * The log is read once, a line at a time (rg_heatmap_feed), whatever its
 * length and number of parts; the grid has RG_HEATMAP_COLS columns, and
 * when the log outgrows them neighbouring columns are merged (each column's
 * span doubles), so memory is fixed by the number of frequencies:
 * rg_heatmap_cells_bytes(). The frequencies are those of the log's first
 * interval (the logger writes a row for every frequency every interval):
 * rg_heatmap_probe finds them before the grid is allocated.
 *
 * No SDK headers; tested on the host with rg_actlog's own output
 * (test/test_heatmap.c, fuzz/fuzz_heatmap.c).
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define RG_HEATMAP_COLS     64u
#define RG_HEATMAP_MAX_CH   24u
#define RG_HEATMAP_LINE_MAX 128u

#define RG_HEAT_ROW     0x01u /* the log covers this time (a row exists) */
#define RG_HEAT_VISITED 0x02u /* listened to (visits > 0) */
#define RG_HEAT_ACTIVE  0x04u /* activity above the threshold seen */

typedef struct {
    uint16_t observed_ds; /* listening time, 0.1 s units (saturating) */
    uint8_t events; /* activity starts (saturating at 255) */
    uint8_t flags; /* RG_HEAT_* */
    int8_t peak; /* strongest reading, dBm (-128: none) */
    uint8_t margin; /* most the peak rose above the floor, dB (0xFF: unknown) */
} RgHeatCell;

typedef enum {
    RgHeatNotObserved, /* no visit (or no row): unknown, not silence */
    RgHeatQuiet, /* observed, nothing above the threshold */
    RgHeatLow,
    RgHeatMid,
    RgHeatHigh,
} RgHeatLevel;

typedef enum {
    RgHeatMetricEvents, /* level by bursts, relative to the busiest cell */
    RgHeatMetricStrength, /* level by peak above the floor: <10, <20, >=20 dB */
} RgHeatMetric;

typedef struct {
    uint8_t channels;
    uint32_t freq[RG_HEATMAP_MAX_CH]; /* in the log's order */
    uint8_t order[RG_HEATMAP_MAX_CH]; /* channels by frequency, ascending */
    uint32_t span_s; /* seconds per column */
    uint32_t end_s; /* latest row time (end of the log's last interval) */
    uint16_t cols_used;
    uint8_t max_events; /* busiest cell */
    RgHeatCell* cells; /* channels x RG_HEATMAP_COLS, caller-owned */

    /* line assembly */
    char line[RG_HEATMAP_LINE_MAX];
    uint16_t line_len;
    bool overlong;
    /* probe */
    bool probing;
    bool probed;
    uint32_t probe_t;
    /* counts */
    uint32_t rows; /* rows binned */
    uint32_t skipped; /* malformed rows, or frequencies not in the first interval */
    bool ended; /* a "# End" footer was read (the log was closed normally) */
} RgHeatmap;

/** Start finding the log's frequencies (feed the first part from its start). */
void rg_heatmap_probe_begin(RgHeatmap* h);

/** True once the frequencies are known (stop feeding then). */
bool rg_heatmap_probe_done(const RgHeatmap* h);

/** Bytes of grid for the frequencies found. 0 if none. */
size_t rg_heatmap_cells_bytes(const RgHeatmap* h);

/** Attach the grid and start binning (feed every part from its start). */
void rg_heatmap_begin(RgHeatmap* h, RgHeatCell* cells);

/** Feed the next bytes of the log (any chunking). */
void rg_heatmap_feed(RgHeatmap* h, const char* data, size_t len);

/** After the last byte (a last line without a newline is used). */
void rg_heatmap_end(RgHeatmap* h);

/** Cell of display row @p row (by frequency) and column @p col. */
const RgHeatCell* rg_heatmap_cell(const RgHeatmap* h, size_t row, size_t col);

/** Frequency of display row @p row. */
uint32_t rg_heatmap_row_freq(const RgHeatmap* h, size_t row);

RgHeatLevel rg_heatmap_level(const RgHeatmap* h, const RgHeatCell* c, RgHeatMetric metric);

/** Coverage of a cell: listening time over its span, percent (0-100). */
uint8_t rg_heatmap_coverage(const RgHeatmap* h, const RgHeatCell* c);

/** Time window of column @p col from the log's start, e.g. "1:20-1:40". */
size_t rg_heatmap_col_time(const RgHeatmap* h, size_t col, char* out, size_t size);

/**
 * One line about cell (@p row, @p col) for the screen's footer, e.g.
 * "433.92 3 bursts cov 21%" (99+ past 99), "315.00 quiet, cov 4%",
 * "390.00 not observed" or "390.00 no data" (after the log's end).
 */
size_t rg_heatmap_cell_text(const RgHeatmap* h, size_t row, size_t col, char* out, size_t size);
