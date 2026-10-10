/**
 * @file radiogeddon_receiver_view.h
 * @brief Live receive view: RSSI meter, decoded-signal list and RAW recorder.
 */
#pragma once

#include <gui/view.h>
#include "../helpers/radiogeddon_recorder.h"

typedef struct RadioGeddonReceiverView RadioGeddonReceiverView;

typedef enum {
    RadioGeddonReceiverEventSave, // OK: persist the highlighted decoded signal
    RadioGeddonReceiverEventToggleRecord, // Left: start/stop RAW capture (hopper: lock)
    RadioGeddonReceiverEventRight, // Right (hopper: next frequency)
    RadioGeddonReceiverEventMore, // Long OK (hopper: statistics)
} RadioGeddonReceiverEvent;

typedef void (*RadioGeddonReceiverCallback)(RadioGeddonReceiverEvent event, void* context);

RadioGeddonReceiverView* radiogeddon_receiver_view_alloc(void);
void radiogeddon_receiver_view_free(RadioGeddonReceiverView* instance);
View* radiogeddon_receiver_view_get_view(RadioGeddonReceiverView* instance);

void radiogeddon_receiver_view_set_callback(
    RadioGeddonReceiverView* instance,
    RadioGeddonReceiverCallback callback,
    void* context);

void radiogeddon_receiver_view_set_config(
    RadioGeddonReceiverView* instance,
    uint32_t frequency,
    const char* preset_label);

/** Switch the status hint between fixed-RX (record) and hopping modes. */
void radiogeddon_receiver_view_set_hopping(RadioGeddonReceiverView* instance, bool hopping);

void radiogeddon_receiver_view_set_rssi(RadioGeddonReceiverView* instance, float rssi);

/** Show a detection-threshold mark on the RSSI bar (pass -127 or lower to hide). */
void radiogeddon_receiver_view_set_threshold(RadioGeddonReceiverView* instance, float dbm);

/** Status text shown instead of the default hint ("" for default). */
void radiogeddon_receiver_view_set_status(RadioGeddonReceiverView* instance, const char* text);

/**
 * Show the REC line (elapsed time, samples, and samples lost or, when it is
 * at least half full, the buffer fill) while @p recording; @p stats may be
 * NULL when not recording.
 */
void radiogeddon_receiver_view_set_recording(
    RadioGeddonReceiverView* instance,
    bool recording,
    const RadioGeddonRecordStats* stats);

/** Replace the decoded-signal list (count) and newest label. */
void radiogeddon_receiver_view_set_history(
    RadioGeddonReceiverView* instance,
    size_t count,
    const char* latest_label);

/** Currently highlighted decoded-signal index. */
size_t radiogeddon_receiver_view_get_selected(RadioGeddonReceiverView* instance);
