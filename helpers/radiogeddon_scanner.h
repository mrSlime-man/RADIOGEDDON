/**
 * @file radiogeddon_scanner.h
 * @brief Background narrowband RSSI sweep.
 *
 * A worker thread retunes the radio to each frequency in the scan list in
 * turn, listens for the configured dwell time and feeds the strongest RSSI
 * reading into rg_scan (noise floor, activity detection, peak hold, counters).
 * Only one frequency is measured at a time; this is not a wideband spectrum
 * analyzer.
 *
 * The scanner object holds the results and can outlive a sweep, so results
 * survive a trip to the receiver and back. The thread exists only between
 * start() and stop(); the radio must not be used by anything else meanwhile.
 */
#pragma once

#include <furi.h>
#include <storage/storage.h>
#include "radiogeddon_subghz.h"
#include "rg_scan.h"
#include "rg_adapt.h"
#include "../radiogeddon_edition.h"

#define RADIOGEDDON_SCANNER_MAX_CHANNELS 24

typedef struct RadioGeddonScanner RadioGeddonScanner;

/** Called from the scanner thread when a channel starts a new activity period. */
typedef void (*RadioGeddonScannerHitCallback)(size_t index, void* context);

typedef struct {
    size_t count;
    uint32_t frequencies[RADIOGEDDON_SCANNER_MAX_CHANNELS];
    RgScanChannel channels[RADIOGEDDON_SCANNER_MAX_CHANNELS];
    float global_floor; // median floor, or RG_SCAN_RSSI_NONE
    int32_t hold_index; // channel the sweep is holding on, or -1
    int32_t measuring; // channel being measured, or -1 (paused, not started)
    bool paused;
    uint32_t sweep_ms; // duration of the last complete sweep (0 until known)
    uint8_t threshold_db;
    // Adaptive Scanner (Full edition): the mode, each channel's share of the
    // visits (percent) and the longest wait any channel had between visits.
    uint8_t mode; // RgAdaptMode (Standard without the feature)
    uint8_t share[RADIOGEDDON_SCANNER_MAX_CHANNELS];
    uint32_t worst_gap_ms;
} RadioGeddonScannerSnapshot;

/**
 * Told about every visit as it ends (scanner thread, with the scanner's
 * lock held: keep it short, never wait): channel @p index, whether it was
 * active and whether this visit started an activity period, the reading and
 * the channel's noise floor (dBm), how long it was measured and when.
 */
typedef void (*RadioGeddonScannerObserver)(
    void* context,
    size_t index,
    bool active,
    bool started,
    int16_t rssi_dbm,
    int16_t floor_dbm,
    uint16_t dwell_ms,
    uint32_t now_ms);

RadioGeddonScanner* radiogeddon_scanner_alloc(RadioGeddonSubGhz* subghz);
void radiogeddon_scanner_free(RadioGeddonScanner* instance);

/**
 * Set the scan list and detection settings. Clears all results if the list
 * changed. Must be called while stopped.
 */
void radiogeddon_scanner_configure(
    RadioGeddonScanner* instance,
    const uint32_t* frequencies,
    size_t count,
    uint16_t dwell_ms,
    uint8_t threshold_db,
    bool hold_on_hit);

void radiogeddon_scanner_set_callback(
    RadioGeddonScanner* instance,
    RadioGeddonScannerHitCallback callback,
    void* context);

#if RG_FEATURE_ADAPTIVE_SCAN
/**
 * Scheduling mode (RgAdaptMode): Standard is the classic round robin; the
 * adaptive modes learn from activity (helpers/rg_adapt.h). Must be called
 * while stopped; a changed mode starts learning afresh.
 */
void radiogeddon_scanner_set_mode(RadioGeddonScanner* instance, uint8_t mode);
/** Forget what the adaptive schedule learned (and the peaks, as Reset). */
void radiogeddon_scanner_reset_learning(RadioGeddonScanner* instance);
/** Custom mode: priority 0..3 of channel @p index. */
void radiogeddon_scanner_set_priority(RadioGeddonScanner* instance, size_t index, uint8_t prio);
#endif

/** Visit observer (NULL: none). Must be called while stopped. */
void radiogeddon_scanner_set_observer(
    RadioGeddonScanner* instance,
    RadioGeddonScannerObserver observer,
    void* context);

/** Hold the scanner's lock (to read what an observer wrote); keep it short. */
void radiogeddon_scanner_lock(RadioGeddonScanner* instance);
void radiogeddon_scanner_unlock(RadioGeddonScanner* instance);

/**
 * Heap a sweep takes to start from here: the scanner object (if @p instance
 * is NULL), the adaptive schedule for @p mode (RgAdaptMode; Standard none),
 * the thread's stack and records. Compare with the largest free block
 * before starting: firmware malloc() never returns NULL, it stops the device.
 */
size_t radiogeddon_scanner_start_cost(RadioGeddonScanner* instance, uint8_t mode);

/** Start the sweep thread (radio session is opened by the thread). */
void radiogeddon_scanner_start(RadioGeddonScanner* instance);
/** Stop the sweep thread and release the radio. Results are kept. */
void radiogeddon_scanner_stop(RadioGeddonScanner* instance);

/** Release a hold if holding; otherwise pause or resume sweeping. */
void radiogeddon_scanner_toggle_pause(RadioGeddonScanner* instance);
/** Release a hold-on-hit without pausing. */
void radiogeddon_scanner_release_hold(RadioGeddonScanner* instance);
/** Clear peak holds and activity counters (noise floors are kept). */
void radiogeddon_scanner_reset_peaks(RadioGeddonScanner* instance);

/** Copy the current results. Safe to call from the GUI thread while running. */
void radiogeddon_scanner_snapshot(RadioGeddonScanner* instance, RadioGeddonScannerSnapshot* out);

/**
 * Write the current results as CSV to @p path (a comment header describing
 * the scan, then one row per frequency). Returns false on I/O failure.
 */
bool radiogeddon_scanner_save_csv(
    RadioGeddonScanner* instance,
    Storage* storage,
    const char* path,
    const char* preset_label);

/** True while the sweep is paused by the user. */
bool radiogeddon_scanner_is_paused(RadioGeddonScanner* instance);

/** Channel the sweep is holding on after a hit, or -1. */
int32_t radiogeddon_scanner_hold_index(RadioGeddonScanner* instance);

/** Frequency of channel @p index, or 0 if out of range. */
uint32_t radiogeddon_scanner_frequency(RadioGeddonScanner* instance, size_t index);

/** Number of channels in the current scan list. */
size_t radiogeddon_scanner_count(RadioGeddonScanner* instance);
