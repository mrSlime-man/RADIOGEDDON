/**
 * @file rg_adapt.h
 * @brief Adaptive Scanner (Full edition): which frequency to measure next,
 * and for how long, from the activity seen so far.
 *
 * The CC1101 hears one frequency at a time, so a scanner's only freedom is
 * the order and length of its visits. Standard mode keeps the classic round
 * robin with a fixed dwell. The adaptive modes give frequencies that have
 * shown activity more visits (stride scheduling: visits at a rate
 * proportional to a bounded weight, evenly spaced) and longer dwells, while
 * every frequency keeps a hard guarantee:
 *
 *  - Fairness: no configured frequency waits more than max_revisit picks
 *    (a bound in visits, so in time it is at most max_revisit * max_dwell);
 *    the most overdue one is taken first.
 *  - No monopoly: a frequency is never picked more than RG_ADAPT_MAX_RUN
 *    times in a row while another exists, and a frequency active on most
 *    visits (a carrier, local noise) has its activity weight capped, so it
 *    cannot take the scan for itself.
 *  - Bounded dwell: dwells stay within [min_dwell, max_dwell].
 *
 * Activity is the scanner's own detection (rg_scan: above the noise floor
 * by the threshold), reported per visit; its weight decays with every pick
 * so old activity fades. Learning can be reset at any time. Everything is
 * integer arithmetic on caller-supplied times, deterministic, and has no
 * SDK includes; host-tested in test/test_adapt.c (fairness, changing
 * signals, Standard against Adaptive on identical simulated scenarios).
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define RG_ADAPT_MAX_CHANNELS 24
/* Consecutive picks of one frequency, at most, while others exist. */
#define RG_ADAPT_MAX_RUN      3u
/* Activity weight a frequency may gain (a fresh activity start adds 64). */
#define RG_ADAPT_SCORE_MAX    512u
/* Visits after which a frequency active on more than 80% of them counts as
 * noisy (its weight is capped at RG_ADAPT_NOISY_CAP). */
#define RG_ADAPT_NOISY_VISITS 20u
#define RG_ADAPT_NOISY_CAP    48u
/* Stride scheduling's unit: a visit moves a frequency on by this / weight. */
#define RG_ADAPT_STRIDE       65536u

/** Scan mode. Stored by number in settings and profiles: append only. */
typedef enum {
    RgAdaptStandard, // round robin, fixed dwell (the classic Scanner)
    RgAdaptBalanced, // activity weighs as much as waiting time
    RgAdaptFocused, // activity weighs three times as much, longer dwells
    RgAdaptCustom, // Balanced plus the user's per-frequency priorities
    RgAdaptModeCount,
} RgAdaptMode;

typedef struct {
    uint32_t visits; // measurements taken
    uint32_t active_visits; // measurements that saw activity
    uint32_t hits; // activity starts
    uint32_t last_pick; // pick number of the last visit
    uint32_t last_visit_ms; // time of the last visit (0: never)
    uint32_t last_active_ms; // time activity was last seen (0: never)
    uint32_t max_gap_ms; // longest time between two visits
    uint32_t dwell_ms_total; // time spent measuring it
    uint16_t score; // decaying activity weight
    int16_t peak_dbm; // strongest reading (dBm, -127 none)
    uint8_t priority; // 0..3, Custom mode only
    uint8_t visited; // at least one visit
} RgAdaptChannel;

typedef struct {
    uint8_t mode; // RgAdaptMode
    uint8_t count;
    uint16_t base_dwell_ms;
    uint16_t min_dwell_ms;
    uint16_t max_dwell_ms;
    uint16_t max_revisit; // fairness bound in picks
    uint8_t run; // consecutive picks of last
    uint8_t last; // last picked channel
    uint32_t picks;
    RgAdaptChannel ch[RG_ADAPT_MAX_CHANNELS];
    uint32_t pass[RG_ADAPT_MAX_CHANNELS]; // stride scheduling: next turn
} RgAdapt;

/**
 * Set up @p count frequencies (at most RG_ADAPT_MAX_CHANNELS) for @p mode
 * with the scanner's @p dwell_ms; learning starts empty.
 */
void rg_adapt_init(RgAdapt* a, RgAdaptMode mode, size_t count, uint16_t dwell_ms);

/** Forget all learning (counters, weights, gaps); priorities are kept. */
void rg_adapt_reset(RgAdapt* a);

/** Priority 0..3 of channel @p index (Custom mode). */
void rg_adapt_set_priority(RgAdapt* a, size_t index, uint8_t priority);

/** The channel to measure next. */
size_t rg_adapt_next(RgAdapt* a);

/** How long to measure channel @p index. */
uint16_t rg_adapt_dwell(const RgAdapt* a, size_t index);

/**
 * Report the visit to @p index that just ended at @p now_ms: whether the
 * channel was active, whether this visit started an activity period, its
 * reading in dBm and how long it was measured.
 */
void rg_adapt_observe(
    RgAdapt* a,
    size_t index,
    bool active,
    bool started,
    int16_t rssi_dbm,
    uint16_t dwell_ms,
    uint32_t now_ms);

/**
 * Share of picks (percent) channel @p index got, and the longest gap any
 * channel waited (ms) — the honest coverage figures for the screen.
 */
uint8_t rg_adapt_share(const RgAdapt* a, size_t index);
uint32_t rg_adapt_worst_gap(const RgAdapt* a, uint32_t now_ms);

/** A frequency the noisy cap applies to now. */
bool rg_adapt_noisy(const RgAdaptChannel* ch);

const char* rg_adapt_mode_name(RgAdaptMode mode);
