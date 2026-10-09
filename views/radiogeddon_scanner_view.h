/**
 * @file radiogeddon_scanner_view.h
 * @brief Narrowband RSSI scanner view: one row per frequency.
 *
 * Each row shows an activity dot, the frequency, a bar for the latest RSSI
 * with a peak-hold tick and a threshold mark, the latest RSSI in dBm and the
 * number of activity periods seen. The header shows the sweep state and the
 * estimated noise floor. The owning scene copies results in with
 * radiogeddon_scanner_view_update().
 */
#pragma once

#include <gui/view.h>
#include "../helpers/radiogeddon_scanner.h"

typedef struct RadioGeddonScannerView RadioGeddonScannerView;

typedef enum {
    RadioGeddonScannerEventSelect, // OK: open the receiver on the highlighted frequency
    RadioGeddonScannerEventTogglePause, // Left: release a hold, else pause / resume
    RadioGeddonScannerEventResetPeaks, // Long Left: clear peaks and counters
    RadioGeddonScannerEventSave, // Right: save results to the SD card
} RadioGeddonScannerEvent;

typedef void (*RadioGeddonScannerCallback)(RadioGeddonScannerEvent event, void* context);

RadioGeddonScannerView* radiogeddon_scanner_view_alloc(void);
void radiogeddon_scanner_view_free(RadioGeddonScannerView* instance);
View* radiogeddon_scanner_view_get_view(RadioGeddonScannerView* instance);

void radiogeddon_scanner_view_set_callback(
    RadioGeddonScannerView* instance,
    RadioGeddonScannerCallback callback,
    void* context);

/** Refresh the displayed results from the scanner. */
void radiogeddon_scanner_view_update(RadioGeddonScannerView* instance, RadioGeddonScanner* scanner);

/** Move the highlight to @p index (e.g. the frequency the sweep is holding on). */
void radiogeddon_scanner_view_set_selected(RadioGeddonScannerView* instance, size_t index);

/** Index currently highlighted by the user. */
size_t radiogeddon_scanner_view_get_selected(RadioGeddonScannerView* instance);
