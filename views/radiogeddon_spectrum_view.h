/**
 * @file radiogeddon_spectrum_view.h
 * @brief Full edition: range-scan screen (RSSI per point across a range).
 *
 * Top line: sweep state and the noise floor / threshold. Middle: one column
 * per screen pixel, the latest reading as a bar, the peak hold as a dot, the
 * threshold over the median floor as a dotted line, active points marked on
 * top. Bottom line: the cursor's frequency, latest / peak dBm and counter.
 * Exists only while the Range Scanner screen is open (allocated on entry).
 *
 * Keys: Left/Right move the cursor (held: fast), Down jumps to the strongest
 * peak, OK opens Receive on the cursor, long OK opens Receive and starts a RAW
 * recording, Up pauses / resumes (releases a hold), long Up recalibrates the
 * noise floor, long Down clears peaks and counters, long Right saves CSV.
 */
#pragma once

#include "../radiogeddon_edition.h"

#if RG_FEATURE_RANGE_SCAN

#include <gui/view.h>
#include "../helpers/radiogeddon_rangescan.h"

typedef struct RadioGeddonSpectrumView RadioGeddonSpectrumView;

typedef enum {
    RadioGeddonSpectrumEventReceive, // OK
    RadioGeddonSpectrumEventRecord, // long OK
    RadioGeddonSpectrumEventTogglePause, // Up
    RadioGeddonSpectrumEventRecalibrate, // long Up
    RadioGeddonSpectrumEventResetPeaks, // long Down
    RadioGeddonSpectrumEventSave, // long Right
} RadioGeddonSpectrumEvent;

typedef void (*RadioGeddonSpectrumCallback)(RadioGeddonSpectrumEvent event, void* context);

RadioGeddonSpectrumView* radiogeddon_spectrum_view_alloc(void);
void radiogeddon_spectrum_view_free(RadioGeddonSpectrumView* instance);
View* radiogeddon_spectrum_view_get_view(RadioGeddonSpectrumView* instance);

void radiogeddon_spectrum_view_set_callback(
    RadioGeddonSpectrumView* instance,
    RadioGeddonSpectrumCallback callback,
    void* context);

/** Refresh from the engine (GUI thread, on the tick). */
void radiogeddon_spectrum_view_update(
    RadioGeddonSpectrumView* instance,
    RadioGeddonRangeScan* scan);

/** Header reads "EXT" while the external CC1101 module is in use. */
void radiogeddon_spectrum_view_set_external(RadioGeddonSpectrumView* instance, bool external);

/** Put the cursor on point @p index (e.g. the point the sweep holds on). */
void radiogeddon_spectrum_view_set_cursor(RadioGeddonSpectrumView* instance, uint32_t index);

/** Point under the cursor. */
uint32_t radiogeddon_spectrum_view_get_cursor(RadioGeddonSpectrumView* instance);

#endif
