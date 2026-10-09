#include "radiogeddon_scene.h"

typedef enum {
    ReceiverCustomDecoded = 200,
    ReceiverCustomSave,
    ReceiverCustomToggleRecord,
} ReceiverCustomEvent;

// --- Radio decode callback (runs on the Sub-GHz worker thread) ------------
// Only records into the (mutex-protected) history and signals the UI thread.
// All GUI/view updates happen on the UI thread in the custom-event handler.
static void radiogeddon_scene_receiver_decode_cb(
    const char* protocol_name,
    uint8_t hash,
    FuriString* text,
    FuriString* serialized,
    void* context) {
    RadioGeddonApp* app = context;

    furi_mutex_acquire(app->history_mutex, FuriWaitForever);
    bool added = radiogeddon_history_add(app->history, protocol_name, hash, text, serialized);
    furi_mutex_release(app->history_mutex);

    if(added) {
        view_dispatcher_send_custom_event(app->view_dispatcher, ReceiverCustomDecoded);
    }
}

// Refresh the decoded-signal view from history (UI thread only).
static void radiogeddon_scene_receiver_refresh_history(RadioGeddonApp* app) {
    furi_mutex_acquire(app->history_mutex, FuriWaitForever);
    size_t count = radiogeddon_history_count(app->history);
    const char* latest = count ? radiogeddon_history_get_name(app->history, count - 1) : "";
    radiogeddon_receiver_view_set_history(app->receiver_view, count, latest);
    furi_mutex_release(app->history_mutex);
}

// Keep the displayed protocol name in step with the highlighted entry, so the
// name shown next to "[i]" is the one OK will actually save.
static void radiogeddon_scene_receiver_show_selected(RadioGeddonApp* app) {
    size_t sel = radiogeddon_receiver_view_get_selected(app->receiver_view);
    furi_mutex_acquire(app->history_mutex, FuriWaitForever);
    size_t count = radiogeddon_history_count(app->history);
    const char* name = (count && sel < count) ? radiogeddon_history_get_name(app->history, sel) :
                                                "";
    radiogeddon_receiver_view_set_history(app->receiver_view, count, name);
    furi_mutex_release(app->history_mutex);
}

// --- View input callback (UI thread) --------------------------------------
static void radiogeddon_scene_receiver_view_cb(RadioGeddonReceiverEvent event, void* context) {
    RadioGeddonApp* app = context;
    if(event == RadioGeddonReceiverEventSave) {
        view_dispatcher_send_custom_event(app->view_dispatcher, ReceiverCustomSave);
    } else if(event == RadioGeddonReceiverEventToggleRecord) {
        view_dispatcher_send_custom_event(app->view_dispatcher, ReceiverCustomToggleRecord);
    }
}

void radiogeddon_scene_receiver_on_enter(void* context) {
    RadioGeddonApp* app = context;

    // Clear the session list only on a fresh entry, not when returning from the
    // save-name screen (so saving one decode does not discard the others).
    if(!app->receiver_preserve_history) {
        furi_mutex_acquire(app->history_mutex, FuriWaitForever);
        radiogeddon_history_reset(app->history);
        furi_mutex_release(app->history_mutex);
    }
    app->receiver_preserve_history = false;

    radiogeddon_receiver_view_set_callback(
        app->receiver_view, radiogeddon_scene_receiver_view_cb, app);
    radiogeddon_receiver_view_set_config(
        app->receiver_view, app->frequency, radiogeddon_presets[app->preset_index].label);
    radiogeddon_receiver_view_set_hopping(app->receiver_view, false);
    radiogeddon_receiver_view_set_threshold(app->receiver_view, -127.0f);
    radiogeddon_scene_receiver_refresh_history(app);
    radiogeddon_receiver_view_set_recording(app->receiver_view, false, 0, false);

    radiogeddon_subghz_set_frequency(app->subghz, app->frequency);
    radiogeddon_subghz_set_preset(app->subghz, app->preset_index);

    if(radiogeddon_subghz_is_device_present(app->subghz)) {
        radiogeddon_subghz_rx_start(app->subghz, radiogeddon_scene_receiver_decode_cb, app);
    } else {
        // No radio: show a clear message instead of pretending to receive.
        popup_reset(app->popup);
        popup_set_header(app->popup, "No radio", 64, 20, AlignCenter, AlignCenter);
        popup_set_text(
            app->popup,
            "Sub-GHz device not\nfound or not responding.",
            64,
            38,
            AlignCenter,
            AlignCenter);
        view_dispatcher_switch_to_view(app->view_dispatcher, RadioGeddonViewPopup);
        return;
    }

    view_dispatcher_switch_to_view(app->view_dispatcher, RadioGeddonViewReceiver);
}

bool radiogeddon_scene_receiver_on_event(void* context, SceneManagerEvent event) {
    RadioGeddonApp* app = context;
    bool consumed = false;

    if(event.type == SceneManagerEventTypeTick) {
        if(radiogeddon_subghz_is_rx_running(app->subghz)) {
            radiogeddon_receiver_view_set_rssi(
                app->receiver_view, radiogeddon_subghz_get_rssi(app->subghz));
            if(radiogeddon_subghz_is_recording(app->subghz)) {
                radiogeddon_receiver_view_set_recording(
                    app->receiver_view,
                    true,
                    radiogeddon_subghz_record_sample_count(app->subghz),
                    radiogeddon_subghz_record_overflowed(app->subghz));
            }
            radiogeddon_scene_receiver_show_selected(app);
        }
        consumed = true;
    } else if(event.type == SceneManagerEventTypeCustom) {
        switch(event.event) {
        case ReceiverCustomDecoded:
            radiogeddon_scene_receiver_refresh_history(app);
            notification_message(app->notifications, &sequence_blink_green_10);
            consumed = true;
            break;
        case ReceiverCustomSave: {
            size_t sel = radiogeddon_receiver_view_get_selected(app->receiver_view);
            furi_mutex_acquire(app->history_mutex, FuriWaitForever);
            bool ok = radiogeddon_history_has_serialized(app->history, sel);
            if(ok) {
                furi_string_set(
                    app->temp_str, radiogeddon_history_get_serialized(app->history, sel));
            }
            furi_mutex_release(app->history_mutex);
            if(ok) {
                app->save_is_raw = false;
                app->receiver_preserve_history = true;
                scene_manager_next_scene(app->scene_manager, RadioGeddonSceneSaveName);
            } else {
                notification_message(app->notifications, &sequence_blink_red_100);
            }
            consumed = true;
            break;
        }
        case ReceiverCustomToggleRecord:
            if(radiogeddon_subghz_is_recording(app->subghz)) {
                radiogeddon_subghz_record_stop(app->subghz);
                notification_message(app->notifications, &sequence_reset_red);
                radiogeddon_receiver_view_set_recording(
                    app->receiver_view,
                    false,
                    radiogeddon_subghz_record_sample_count(app->subghz),
                    radiogeddon_subghz_record_overflowed(app->subghz));
                // Hand off to the naming scene to persist the capture.
                if(radiogeddon_subghz_record_sample_count(app->subghz) > 0) {
                    app->save_is_raw = true;
                    app->receiver_preserve_history = true;
                    scene_manager_next_scene(app->scene_manager, RadioGeddonSceneSaveName);
                }
            } else if(radiogeddon_subghz_record_start(app->subghz, NULL)) {
                notification_message(app->notifications, &sequence_set_only_red_255);
            } else {
                // Not enough free memory for a capture buffer: refuse cleanly.
                notification_message(app->notifications, &sequence_blink_red_100);
            }
            consumed = true;
            break;
        default:
            break;
        }
    }
    return consumed;
}

void radiogeddon_scene_receiver_on_exit(void* context) {
    RadioGeddonApp* app = context;
    notification_message(app->notifications, &sequence_reset_red);
    // Stop the radio. The RAW capture buffer is retained after stop, so the
    // save-name scene can still flush it to disk.
    radiogeddon_subghz_rx_stop(app->subghz);
}
