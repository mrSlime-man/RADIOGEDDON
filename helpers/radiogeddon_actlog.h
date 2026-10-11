/**
 * @file radiogeddon_actlog.h
 * @brief Full edition (a module): the RF Activity Logger's files.
 *
 * The Scanner engine does the listening (one frequency at a time, its
 * schedule Standard or adaptive) and reports every visit to the log
 * (observe, on the scanner's thread with its lock held: only counters move).
 * On the app's tick the scene collects the interval's rows under that lock
 * (collect: formatting into a small buffer) and writes them without it
 * (write), so the radio thread never waits for the card.
 *
 * Files: apps_data/radiogeddon/activity/<base>.csv, then <base>_2.csv ...
 * when a part reaches its size (rg_actlog). A write error or the last part
 * filling up stops the log (the observation can still be read on screen and
 * its summary saved); nothing is overwritten. The summary is written next
 * to the log as <base>_summary.txt when the log finishes.
 */
#pragma once

#include "../radiogeddon_edition.h"

#if RG_FEATURE_ACTIVITY_LOG

#include <furi.h>
#include <storage/storage.h>
#include "rg_actlog.h"

#define RADIOGEDDON_ACTLOG_FOLDER EXT_PATH("apps_data/radiogeddon/activity")

typedef struct {
    uint16_t interval_s; // seconds per row set
    uint16_t minutes; // observation length (0: until stopped)
    uint32_t max_bytes; // per part
    char scanner[64]; // the scanner's settings, for the header
} RadioGeddonActlogConfig;

typedef enum {
    RadioGeddonActlogRunning,
    RadioGeddonActlogDone, // the observation time is over
    RadioGeddonActlogFull, // the size limit stopped the file
    RadioGeddonActlogFailed, // the card refused a write
} RadioGeddonActlogState;

typedef struct {
    uint8_t state; // RadioGeddonActlogState
    uint32_t elapsed_s;
    uint32_t limit_s; // 0: until stopped
    uint32_t events;
    uint32_t rows;
    uint32_t bytes; // written in all parts
    uint8_t part;
    uint8_t top[2]; // most active channels (0xFF: none)
    uint32_t top_hz[2];
    uint32_t top_events[2];
    int16_t top_peak[2];
    char file[40]; // the current part's name
} RadioGeddonActlogStatus;

typedef struct RadioGeddonActlog RadioGeddonActlog;

size_t radiogeddon_actlog_memory(size_t count);

RadioGeddonActlog* radiogeddon_actlog_start(
    Storage* storage,
    const uint32_t* frequencies,
    size_t count,
    const RadioGeddonActlogConfig* config,
    uint32_t now);

void radiogeddon_actlog_observe(
    void* context,
    size_t index,
    bool active,
    bool started,
    int16_t rssi_dbm,
    int16_t floor_dbm,
    uint16_t dwell_ms,
    uint32_t now_ms);

/** Rows due by @p now into @p buf (0: none due). Scanner lock held. */
size_t radiogeddon_actlog_collect(RadioGeddonActlog* log, uint32_t now, char* buf, size_t size);

/** Append @p len bytes (rotating parts as needed). False: the log stopped
 * (a write error, or the last part full). */
bool radiogeddon_actlog_write(RadioGeddonActlog* log, const char* buf, size_t len);

/** What the screen shows. Scanner lock held. */
void radiogeddon_actlog_status(RadioGeddonActlog* log, uint32_t now, RadioGeddonActlogStatus* out);

/**
 * Write the last rows and the footer (@p reason), the summary file, close
 * and free the log; the summary text goes to @p summary. Scanner stopped.
 */
size_t radiogeddon_actlog_finish(
    RadioGeddonActlog* log,
    const char* reason,
    uint32_t now,
    char* summary,
    size_t size);

#endif
