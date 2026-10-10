/**
 * @file rg_hop.h
 * @brief Firmware-independent Frequency Hopper logic.
 *
 * The hopper listens on one frequency at a time. While a frequency is quiet it
 * moves on after the dwell time; when RSSI rises above that frequency's own
 * noise floor by the threshold (see rg_scan) it holds there for the activity
 * hold time, extended while the signal stays up or keeps decoding, so a
 * transmission can be decoded or recorded instead of being cut off by a
 * retune. A manual lock stops hopping entirely.
 *
 * The caller samples RSSI on the current frequency, feeds it in with
 * rg_hop_feed(), performs any retune the result asks for and confirms it with
 * rg_hop_tuned(). No furi/SDK includes: host-tested in test/test_hop.c.
 */
#pragma once

#include "rg_scan.h"

#define RG_HOP_MAX_CHANNELS 24
#define RG_HOP_MAX_EVENTS   16
#define RG_HOP_PROTO_LEN    16

typedef struct {
    uint32_t dwell_ms; // time on a quiet frequency before moving on
    uint32_t hold_ms; // time to stay after activity was last seen
    RgScanParams detect; // floor-relative detection settings
} RgHopConfig;

typedef struct {
    uint32_t active_ms; // total time spent holding on activity
    uint16_t decodes; // decoded parcels while tuned here
} RgHopStats;

/** One detected activity period. */
typedef struct {
    uint8_t channel;
    uint8_t decodes;
    bool recorded; // a RAW capture of it was saved
    bool open; // still in progress
    uint32_t start_ms;
    uint32_t duration_ms;
    float peak_dbm;
    char protocol[RG_HOP_PROTO_LEN]; // first decoded protocol, or ""
} RgHopEvent;

typedef struct {
    size_t count;
    size_t current;
    uint32_t tuned_ms; // when the current frequency was tuned
    uint32_t hold_until_ms; // 0 when not holding
    bool holding;
    bool locked;
    RgScanChannel channels[RG_HOP_MAX_CHANNELS];
    RgHopStats stats[RG_HOP_MAX_CHANNELS];
    RgHopEvent events[RG_HOP_MAX_EVENTS]; // ring buffer
    size_t event_head; // index of the next slot to write
    size_t event_count; // number of valid events (<= RG_HOP_MAX_EVENTS)
    uint32_t hold_started_ms;
} RgHop;

typedef struct {
    bool activity_started; // a new activity period began on the current channel
    bool activity_ended; // the hold ended (signal gone for hold_ms)
    bool retune; // caller should tune to next_channel and call rg_hop_tuned()
    size_t next_channel;
} RgHopStep;

/** Defaults: 200 ms dwell, 2 s hold, rg_scan detection defaults. */
void rg_hop_config_default(RgHopConfig* config);

/** Reset everything for @p count channels (clamped), starting on channel 0. */
void rg_hop_init(RgHop* hop, size_t count, uint32_t now_ms);

/** Feed one RSSI reading taken on the current channel. */
RgHopStep rg_hop_feed(RgHop* hop, const RgHopConfig* config, float rssi, uint32_t now_ms);

/** Confirm the radio is now on @p channel. */
void rg_hop_tuned(RgHop* hop, size_t channel, uint32_t now_ms);

/**
 * A parcel was decoded on the current channel: counts it and treats it as
 * activity (starting or extending a hold). Returns true if this started a
 * new activity period.
 */
bool rg_hop_note_decode(
    RgHop* hop,
    const RgHopConfig* config,
    const char* protocol,
    uint32_t now_ms);

/** Lock on / unlock from the current channel. Unlocking restarts its dwell. */
void rg_hop_set_locked(RgHop* hop, bool locked, uint32_t now_ms);

/** Channel after the current one (wraps). */
size_t rg_hop_next_channel(const RgHop* hop);

/** Mark the most recent event as recorded. */
void rg_hop_mark_recorded(RgHop* hop);

/** The @p i-th most recent event (0 = newest), or NULL. */
const RgHopEvent* rg_hop_event(const RgHop* hop, size_t i);

/** Milliseconds of hold left, or 0. */
uint32_t rg_hop_hold_remaining(const RgHop* hop, uint32_t now_ms);
