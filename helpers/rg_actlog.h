/**
 * @file rg_actlog.h
 * @brief RF Activity Logger (Full edition): what a long passive observation
 * of a frequency list saw, and how it is written to the SD card.
 *
 * The CC1101 measures one frequency at a time, so every figure here is per
 * visit: a frequency is observed only while the scanner listens to it, and
 * everything else is time without a measurement. The logger keeps both, so
 * a quiet interval (measured, nothing above the threshold) is never confused
 * with an interval nobody listened (no visits): a row says which.
 *
 * Rows are aggregated over the logging interval (one per frequency
 * observed or not), so a long log grows by a bounded amount per interval
 * whatever happens on the air, and nothing is written between intervals.
 * The file has a size limit and up to RG_ACTLOG_MAX_PARTS parts; when the
 * last is full the log stops (and says so) rather than overwrite anything.
 * Lines are complete when written; a log cut by a reboot or a full card
 * lacks its "# End" footer, which the summary reports as interrupted.
 *
 * Pure C with no SDK headers; host-tested in test/test_actlog.c.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define RG_ACTLOG_MAX_CHANNELS 24
#define RG_ACTLOG_MAX_PARTS    8u
#define RG_ACTLOG_LINE_MAX     96

typedef struct {
    /* the whole observation */
    uint32_t observed_ms; // time spent listening to it
    uint32_t visits;
    uint32_t active_visits;
    uint32_t events; // activity starts
    uint32_t active_ms; // listening time during which it was active (approximate)
    int16_t peak_dbm; // strongest reading (-127: none)
    int16_t floor_dbm; // latest noise-floor estimate (-127: none)
    uint32_t first_event_ms; // time of the first / last activity start (0: none)
    uint32_t last_event_ms;
    /* the interval being logged */
    uint32_t i_observed_ms;
    uint16_t i_visits;
    uint16_t i_active_visits;
    uint16_t i_events;
    int16_t i_peak_dbm;
} RgActlogChannel;

typedef struct {
    uint8_t count;
    uint32_t frequencies[RG_ACTLOG_MAX_CHANNELS];
    RgActlogChannel ch[RG_ACTLOG_MAX_CHANNELS];
    uint32_t start_ms; // when observation began
    uint32_t interval_start_ms;
    uint32_t elapsed_ms; // observation time so far
    uint32_t intervals; // intervals logged
    /* file state */
    uint32_t max_bytes; // per part
    uint32_t written; // bytes in the current part
    uint8_t part; // 0-based part number
    bool full; // the size limit stopped the log
} RgActlog;

/** Start an observation of @p count frequencies at @p now_ms. */
void rg_actlog_init(
    RgActlog* log,
    const uint32_t* frequencies,
    size_t count,
    uint32_t max_bytes,
    uint32_t now_ms);

/** One visit to channel @p index that ended at @p now_ms. */
void rg_actlog_observe(
    RgActlog* log,
    size_t index,
    bool active,
    bool started,
    int16_t rssi_dbm,
    int16_t floor_dbm,
    uint16_t dwell_ms,
    uint32_t now_ms);

/** Advance the observation clock (the screen's tick). */
void rg_actlog_tick(RgActlog* log, uint32_t now_ms);

/** True once @p interval_ms have passed since the interval began. */
bool rg_actlog_interval_due(const RgActlog* log, uint32_t interval_ms, uint32_t now_ms);

/**
 * The file's header lines (written at the start of each part): what was
 * observed and how, and the column names. @p config describes the scanner
 * (preset, dwell, threshold, mode). Returns the length (0: does not fit).
 */
size_t rg_actlog_header(
    const RgActlog* log,
    const char* config,
    const char* started,
    char* out,
    size_t size);

/**
 * The row for channel @p index over the interval ending at @p now_ms:
 * "t_s,freq_hz,visits,observed_ms,active_visits,events,peak_dbm,floor_dbm"
 * with empty fields where nothing was measured. Returns the length.
 */
size_t rg_actlog_row(const RgActlog* log, size_t index, uint32_t now_ms, char* out, size_t size);

/** Clear the interval counters after its rows were written. */
void rg_actlog_end_interval(RgActlog* log, uint32_t now_ms);

/**
 * Account for @p bytes written. Returns true when the next write must go to
 * a new part (the current one reached max_bytes); sets log->full instead
 * when no part is left.
 */
bool rg_actlog_account(RgActlog* log, uint32_t bytes);

/** Name of part @p part of log @p base: "BASE.csv", then "BASE_2.csv", ... */
size_t rg_actlog_part_name(const char* base, uint8_t part, char* out, size_t size);

/** Footer line: observation length and why it ended. */
size_t rg_actlog_footer(const RgActlog* log, const char* reason, char* out, size_t size);

/** Channel indices ordered by activity (events, then active share). */
size_t rg_actlog_ranking(const RgActlog* log, uint8_t* order, size_t max);

/**
 * Share of the observation (percent) spent listening to channel @p index:
 * how much of its time was actually measured.
 */
uint8_t rg_actlog_coverage(const RgActlog* log, size_t index);

/**
 * A plain-text summary (most active frequencies, events, peaks, coverage
 * and its limits). Returns the length.
 */
size_t rg_actlog_summary(const RgActlog* log, char* out, size_t size);
