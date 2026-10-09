/**
 * @file radiogeddon_scanner_view.h
 * @brief Live frequency-sweep view with per-frequency RSSI bars.
 *
 * The owning scene drives the sweep (probing one frequency per tick) and pushes
 * results in with radiogeddon_scanner_view_set_rssi(). The view renders a
 * scrollable list and reports selection/activation back through its callback.
 */
#pragma once

#include <gui/view.h>

typedef struct RadioGeddonScannerView RadioGeddonScannerView;

typedef enum {
    RadioGeddonScannerEventSelect, // OK pressed on the highlighted frequency
} RadioGeddonScannerEvent;

typedef void (*RadioGeddonScannerCallback)(RadioGeddonScannerEvent event, void* context);

RadioGeddonScannerView* radiogeddon_scanner_view_alloc(void);
void radiogeddon_scanner_view_free(RadioGeddonScannerView* instance);
View* radiogeddon_scanner_view_get_view(RadioGeddonScannerView* instance);

void radiogeddon_scanner_view_set_callback(
    RadioGeddonScannerView* instance,
    RadioGeddonScannerCallback callback,
    void* context);

/** Provide the frequency table the view will display (Hz values, not copied). */
void radiogeddon_scanner_view_set_frequencies(
    RadioGeddonScannerView* instance,
    const uint32_t* frequencies,
    size_t count);

/** Update the RSSI (dBm) sampled for a given frequency index. */
void radiogeddon_scanner_view_set_rssi(
    RadioGeddonScannerView* instance,
    size_t index,
    float rssi);

/** Index currently highlighted by the user. */
size_t radiogeddon_scanner_view_get_selected(RadioGeddonScannerView* instance);
