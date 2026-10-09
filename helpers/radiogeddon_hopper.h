/**
 * @file radiogeddon_hopper.h
 * @brief Frequency Hopper 2.0 engine.
 *
 * Runs on its own thread while a receive session is active: samples RSSI on
 * the current frequency every 10 ms, feeds rg_hop (per-frequency noise floor,
 * floor-relative detection, activity hold, lock) and retunes the running RX
 * session when the logic says so. Optionally records RAW while holding on
 * activity and saves each capture to the signals folder when the hold ends.
 *
 * The caller owns the RX session: start RX (tuned to
 * radiogeddon_hopper_current_frequency()) before radiogeddon_hopper_start(),
 * and stop it after radiogeddon_hopper_stop(). While the thread runs, nothing
 * else may touch the radio. Results (statistics, activity history) outlive the
 * thread so they survive leaving the hopper screen and coming back.
 */
#pragma once

#include <furi.h>
#include "radiogeddon_subghz.h"
#include "rg_hop.h"

typedef struct RadioGeddonHopper RadioGeddonHopper;

typedef enum {
    RadioGeddonHopperEventRetuned,
    RadioGeddonHopperEventActivity, // a new activity period began
    RadioGeddonHopperEventRecordSaved, // an automatic RAW capture was saved
    RadioGeddonHopperEventRecordFailed, // an automatic capture could not be saved
} RadioGeddonHopperEvent;

/** Called from the hopper thread. */
typedef void (*RadioGeddonHopperCallback)(RadioGeddonHopperEvent event, void* context);

typedef struct {
    size_t count;
    size_t index;
    uint32_t frequency;
    float rssi;
    float threshold_dbm; // current frequency's floor + threshold, or RG_SCAN_RSSI_NONE
    bool locked;
    bool holding;
    uint32_t hold_left_ms;
    bool recording;
    RadioGeddonRecordStats record; // valid while recording
} RadioGeddonHopperStatus;

RadioGeddonHopper* radiogeddon_hopper_alloc(RadioGeddonSubGhz* subghz);
void radiogeddon_hopper_free(RadioGeddonHopper* instance);

/**
 * Set the hop list and timing. Clears statistics and history if the list
 * changed. Must be called while stopped.
 */
void radiogeddon_hopper_configure(
    RadioGeddonHopper* instance,
    const uint32_t* frequencies,
    size_t count,
    uint16_t dwell_ms,
    uint16_t hold_ms,
    uint8_t threshold_db,
    bool auto_record);

void radiogeddon_hopper_set_callback(
    RadioGeddonHopper* instance,
    RadioGeddonHopperCallback callback,
    void* context);

/** Frequency the hopper expects RX to be tuned to when it starts. */
uint32_t radiogeddon_hopper_current_frequency(RadioGeddonHopper* instance);

void radiogeddon_hopper_start(RadioGeddonHopper* instance);
/** Stop the thread; an automatic recording in progress is saved first. */
void radiogeddon_hopper_stop(RadioGeddonHopper* instance);

/** Lock on / unlock from the current frequency (handled by the thread). */
void radiogeddon_hopper_toggle_lock(RadioGeddonHopper* instance);
/** Step to the next frequency now (handled by the thread). */
void radiogeddon_hopper_next(RadioGeddonHopper* instance);

/** Report a decoded parcel (safe to call from the radio worker thread). */
void radiogeddon_hopper_note_decode(RadioGeddonHopper* instance, const char* protocol);

void radiogeddon_hopper_status(RadioGeddonHopper* instance, RadioGeddonHopperStatus* out);

/** Copy the full statistics/history state (large: allocate @p out on the heap). */
void radiogeddon_hopper_copy_state(RadioGeddonHopper* instance, RgHop* out, uint32_t* frequencies);

/** Write a human-readable statistics and activity report to @p out. */
void radiogeddon_hopper_report(RadioGeddonHopper* instance, FuriString* out);
