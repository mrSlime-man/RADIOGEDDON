/**
 * @file radiogeddon_waterfall_view.h
 * @brief Full edition: Waterfall screen (RSSI sweep history of a range scan).
 *
 * Header: "RSSI sweep" and the state (LIVE, PAUSE, CAL while the noise floors
 * warm up, or -N when scrolled N sweeps back), then the sensitivity span and
 * N (noise-floor compensation) or A (absolute). A marker row shows the
 * strongest stored reading (a tick) and where band segments start after a
 * gap the radio cannot tune (a dot). The picture: one row per complete sweep,
 * newest on top, frequency across in the scan's point order. Footer: the
 * cursor's frequency, its reading in the top visible row and its peak over
 * the history, and the measured (or, before the first sweep, estimated)
 * sweep time, or how old the top row is when scrolled.
 *
 * Keys: Left/Right move the cursor one column (held: faster), Up/Down
 * scroll four sweeps newer/older (the view then stays on those sweeps while
 * new ones arrive), OK pauses / resumes the sweep. Holding OK opens a menu:
 * Receive here, Cursor to peak, Newest sweeps, Sensitivity, Floor comp and
 * Save history CSV; Back closes it. Exists only while the Waterfall is open.
 */
#pragma once

#include "../radiogeddon_edition.h"

#if RG_FEATURE_WATERFALL

#include <gui/view.h>
#include "../helpers/radiogeddon_rangescan.h"

typedef struct RadioGeddonWaterfallView RadioGeddonWaterfallView;

typedef enum {
    RadioGeddonWaterfallEventTogglePause, // OK
    RadioGeddonWaterfallEventReceive, // menu: Receive here
    RadioGeddonWaterfallEventSave, // menu: Save history CSV
    RadioGeddonWaterfallEventSettings, // menu: sensitivity or compensation changed
} RadioGeddonWaterfallEvent;

typedef void (*RadioGeddonWaterfallCallback)(RadioGeddonWaterfallEvent event, void* context);

/* Sensitivity choices (dB from the reference to the densest dither). */
#define RADIOGEDDON_WF_SPANS 5u
extern const uint8_t radiogeddon_waterfall_spans[RADIOGEDDON_WF_SPANS];

RadioGeddonWaterfallView* radiogeddon_waterfall_view_alloc(void);
void radiogeddon_waterfall_view_free(RadioGeddonWaterfallView* instance);
View* radiogeddon_waterfall_view_get_view(RadioGeddonWaterfallView* instance);

void radiogeddon_waterfall_view_set_callback(
    RadioGeddonWaterfallView* instance,
    RadioGeddonWaterfallCallback callback,
    void* context);

/** Refresh from the engine (GUI thread, on the tick). */
void radiogeddon_waterfall_view_update(
    RadioGeddonWaterfallView* instance,
    RadioGeddonRangeScan* scan);

/** Display settings: sensitivity index into radiogeddon_waterfall_spans, compensation. */
void radiogeddon_waterfall_view_set_style(
    RadioGeddonWaterfallView* instance,
    uint8_t span_index,
    bool noise_comp);
void radiogeddon_waterfall_view_get_style(
    RadioGeddonWaterfallView* instance,
    uint8_t* span_index,
    bool* noise_comp);

/** Header reads "EXT" while the external CC1101 module is in use. */
void radiogeddon_waterfall_view_set_external(RadioGeddonWaterfallView* instance, bool external);

/** Frequency under the cursor (0 before the first frame). */
uint32_t radiogeddon_waterfall_view_get_cursor_hz(RadioGeddonWaterfallView* instance);

/** Cursor column, kept across a trip to Receive. */
uint16_t radiogeddon_waterfall_view_get_cursor(RadioGeddonWaterfallView* instance);
void radiogeddon_waterfall_view_set_cursor(RadioGeddonWaterfallView* instance, uint16_t column);

#endif
