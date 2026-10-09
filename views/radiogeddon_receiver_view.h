/**
 * @file radiogeddon_receiver_view.h
 * @brief Live receive view: RSSI meter, decoded-signal list and RAW recorder.
 */
#pragma once

#include <gui/view.h>

typedef struct RadioGeddonReceiverView RadioGeddonReceiverView;

typedef enum {
    RadioGeddonReceiverEventSave, // OK: persist the highlighted decoded signal
    RadioGeddonReceiverEventToggleRecord, // Left: start/stop RAW capture
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

void radiogeddon_receiver_view_set_recording(
    RadioGeddonReceiverView* instance,
    bool recording,
    size_t samples,
    bool overflow);

/** Replace the decoded-signal list (count) and newest label. */
void radiogeddon_receiver_view_set_history(
    RadioGeddonReceiverView* instance,
    size_t count,
    const char* latest_label);

/** Currently highlighted decoded-signal index. */
size_t radiogeddon_receiver_view_get_selected(RadioGeddonReceiverView* instance);
