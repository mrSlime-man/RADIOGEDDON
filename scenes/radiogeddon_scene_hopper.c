#include "radiogeddon_scene.h"

#define TAG "RadioGeddonHopperScene"

typedef enum {
    HopperCustomDecoded = 500,
    HopperCustomSave,
    HopperCustomLock,
    HopperCustomNext,
    HopperCustomStats,
    HopperCustomRetuned,
    HopperCustomActivity,
    HopperCustomRecordSaved,
    HopperCustomRecordFailed,
} HopperCustomEvent;

static void radiogeddon_scene_hopper_decode_cb(
    const char* protocol_name,
    uint8_t hash,
    FuriString* text,
    FuriString* serialized,
    void* context) {
    RadioGeddonApp* app = context;
    // A decode is activity even if RSSI stayed low: keep holding here.
    if(app->hopper) radiogeddon_hopper_note_decode(app->hopper, protocol_name);
    furi_mutex_acquire(app->history_mutex, FuriWaitForever);
    bool added = radiogeddon_history_add(app->history, protocol_name, hash, text, serialized);
    furi_mutex_release(app->history_mutex);
    if(added) view_dispatcher_send_custom_event(app->view_dispatcher, HopperCustomDecoded);
}

// Hopper thread -> GUI thread.
static void radiogeddon_scene_hopper_engine_cb(RadioGeddonHopperEvent event, void* context) {
    RadioGeddonApp* app = context;
    uint32_t custom = HopperCustomRetuned;
    switch(event) {
    case RadioGeddonHopperEventRetuned:
        custom = HopperCustomRetuned;
        break;
    case RadioGeddonHopperEventActivity:
        custom = HopperCustomActivity;
        break;
    case RadioGeddonHopperEventRecordSaved:
        custom = HopperCustomRecordSaved;
        break;
    case RadioGeddonHopperEventRecordFailed:
        custom = HopperCustomRecordFailed;
        break;
    }
    view_dispatcher_send_custom_event(app->view_dispatcher, custom);
}

static void radiogeddon_scene_hopper_refresh_history(RadioGeddonApp* app) {
    furi_mutex_acquire(app->history_mutex, FuriWaitForever);
    size_t count = radiogeddon_history_count(app->history);
    const char* latest = count ? radiogeddon_history_get_name(app->history, count - 1) : "";
    radiogeddon_receiver_view_set_history(app->receiver_view, count, latest);
    furi_mutex_release(app->history_mutex);
}

// Keep the displayed name in step with the highlighted entry (see receiver).
static void radiogeddon_scene_hopper_show_selected(RadioGeddonApp* app) {
    size_t sel = radiogeddon_receiver_view_get_selected(app->receiver_view);
    furi_mutex_acquire(app->history_mutex, FuriWaitForever);
    size_t count = radiogeddon_history_count(app->history);
    const char* name = (count && sel < count) ? radiogeddon_history_get_name(app->history, sel) :
                                                "";
    radiogeddon_receiver_view_set_history(app->receiver_view, count, name);
    furi_mutex_release(app->history_mutex);
}

static void radiogeddon_scene_hopper_view_cb(RadioGeddonReceiverEvent event, void* context) {
    RadioGeddonApp* app = context;
    uint32_t custom = HopperCustomSave;
    switch(event) {
    case RadioGeddonReceiverEventSave:
        custom = HopperCustomSave;
        break;
    case RadioGeddonReceiverEventToggleRecord: // Left
        custom = HopperCustomLock;
        break;
    case RadioGeddonReceiverEventRight:
        custom = HopperCustomNext;
        break;
    case RadioGeddonReceiverEventMore:
        custom = HopperCustomStats;
        break;
    }
    view_dispatcher_send_custom_event(app->view_dispatcher, custom);
}

// Build the hop list from the saved mask, skipping frequencies the radio
// cannot tune.
static size_t radiogeddon_scene_hopper_build_list(RadioGeddonApp* app, uint32_t* out) {
    size_t n = 0;
    for(size_t i = 0; i < radiogeddon_frequencies_count && i < 32; i++) {
        if(!(app->settings.hop_mask & (1u << i))) continue;
        uint32_t f = radiogeddon_frequencies[i];
        if(!radiogeddon_subghz_is_frequency_allowed(app->subghz, f)) continue;
        if(n < RG_HOP_MAX_CHANNELS) out[n++] = f;
    }
    return n;
}

static void
    radiogeddon_scene_hopper_show_popup(RadioGeddonApp* app, const char* h, const char* t) {
    popup_reset(app->popup);
    popup_set_header(app->popup, h, 64, 20, AlignCenter, AlignCenter);
    popup_set_text(app->popup, t, 64, 38, AlignCenter, AlignCenter);
    view_dispatcher_switch_to_view(app->view_dispatcher, RadioGeddonViewPopup);
}

// Refresh header, RSSI, threshold mark and status line from the engine.
static void radiogeddon_scene_hopper_refresh_status(RadioGeddonApp* app) {
    RadioGeddonHopperStatus st;
    radiogeddon_hopper_status(app->hopper, &st);
    radiogeddon_receiver_view_set_config(
        app->receiver_view, st.frequency, radiogeddon_presets[app->preset_index].label);
    radiogeddon_receiver_view_set_rssi(app->receiver_view, st.rssi);
    radiogeddon_receiver_view_set_threshold(app->receiver_view, st.threshold_dbm);
    radiogeddon_receiver_view_set_recording(app->receiver_view, st.recording, &st.record);

    char status[26];
    if(st.locked && st.holding) {
        snprintf(status, sizeof(status), "LOCKED  activity");
    } else if(st.locked) {
        snprintf(status, sizeof(status), "LOCKED  L:unlock R:next");
    } else if(st.holding) {
        snprintf(
            status,
            sizeof(status),
            "HOLD %lu.%lus  L:lock",
            (unsigned long)(st.hold_left_ms / 1000),
            (unsigned long)((st.hold_left_ms % 1000) / 100));
    } else {
        snprintf(
            status,
            sizeof(status),
            "Hop %u/%u  L:lock R:next",
            (unsigned)(st.index + 1),
            (unsigned)st.count);
    }
    radiogeddon_receiver_view_set_status(app->receiver_view, status);
}

void radiogeddon_scene_hopper_on_enter(void* context) {
    RadioGeddonApp* app = context;
    FURI_LOG_I(TAG, "enter, free heap %u", (unsigned)memmgr_get_free_heap());

    if(!app->receiver_preserve_history) {
        furi_mutex_acquire(app->history_mutex, FuriWaitForever);
        radiogeddon_history_reset(app->history);
        furi_mutex_release(app->history_mutex);
    }
    app->receiver_preserve_history = false;
    app->scanner_running = false;

    radiogeddon_receiver_view_set_callback(
        app->receiver_view, radiogeddon_scene_hopper_view_cb, app);
    radiogeddon_receiver_view_set_hopping(app->receiver_view, true);
    radiogeddon_receiver_view_set_recording(app->receiver_view, false, NULL);
    radiogeddon_receiver_view_set_status(app->receiver_view, "");
    radiogeddon_scene_hopper_refresh_history(app);

    if(!radiogeddon_subghz_is_device_present(app->subghz)) {
        radiogeddon_scene_hopper_show_popup(
            app, "No radio", "Sub-GHz device not\nfound or not responding.");
        return;
    }

    uint32_t list[RG_HOP_MAX_CHANNELS];
    size_t count = radiogeddon_scene_hopper_build_list(app, list);
    if(count == 0) {
        radiogeddon_scene_hopper_show_popup(app, "No frequencies", "Check Settings >\nHop list.");
        return;
    }

    if(!app->hopper) app->hopper = radiogeddon_hopper_alloc(app->subghz);
    radiogeddon_hopper_configure(
        app->hopper,
        list,
        count,
        app->settings.hop_dwell_ms,
        app->settings.hop_hold_ms,
        app->settings.scan_threshold_db,
        app->settings.hop_auto_record);
    radiogeddon_hopper_set_callback(app->hopper, radiogeddon_scene_hopper_engine_cb, app);

    radiogeddon_subghz_set_preset(app->subghz, app->preset_index);
    radiogeddon_subghz_set_frequency(
        app->subghz, radiogeddon_hopper_current_frequency(app->hopper));
    radiogeddon_subghz_rx_start(app->subghz, radiogeddon_scene_hopper_decode_cb, app);
    radiogeddon_hopper_start(app->hopper);
    app->scanner_running = true; // "hopper active"

    radiogeddon_scene_hopper_refresh_status(app);
    view_dispatcher_switch_to_view(app->view_dispatcher, RadioGeddonViewReceiver);
}

bool radiogeddon_scene_hopper_on_event(void* context, SceneManagerEvent event) {
    RadioGeddonApp* app = context;
    bool consumed = false;

    if(event.type == SceneManagerEventTypeTick) {
        if(app->scanner_running) {
            radiogeddon_scene_hopper_refresh_status(app);
            radiogeddon_scene_hopper_show_selected(app);
        }
        consumed = true;
    } else if(event.type == SceneManagerEventTypeCustom) {
        consumed = true;
        if(!app->scanner_running) return consumed;
        switch(event.event) {
        case HopperCustomDecoded:
            radiogeddon_scene_hopper_refresh_history(app);
            notification_message(app->notifications, &sequence_blink_green_10);
            break;
        case HopperCustomActivity:
            notification_message(app->notifications, &sequence_blink_cyan_10);
            radiogeddon_scene_hopper_refresh_status(app);
            break;
        case HopperCustomRetuned:
            radiogeddon_scene_hopper_refresh_status(app);
            break;
        case HopperCustomRecordSaved:
            notification_message(app->notifications, &sequence_success);
            break;
        case HopperCustomRecordFailed:
            notification_message(app->notifications, &sequence_error);
            break;
        case HopperCustomLock:
            radiogeddon_hopper_toggle_lock(app->hopper);
            break;
        case HopperCustomNext:
            radiogeddon_hopper_next(app->hopper);
            break;
        case HopperCustomStats:
            app->receiver_preserve_history = true;
            scene_manager_next_scene(app->scene_manager, RadioGeddonSceneHopperStats);
            break;
        case HopperCustomSave: {
            size_t sel = radiogeddon_receiver_view_get_selected(app->receiver_view);
            furi_mutex_acquire(app->history_mutex, FuriWaitForever);
            bool ok = radiogeddon_history_has_serialized(app->history, sel);
            if(ok)
                furi_string_set(
                    app->temp_str, radiogeddon_history_get_serialized(app->history, sel));
            furi_mutex_release(app->history_mutex);
            if(ok) {
                app->save_is_raw = false;
                app->receiver_preserve_history = true;
                scene_manager_next_scene(app->scene_manager, RadioGeddonSceneSaveName);
            } else {
                notification_message(app->notifications, &sequence_blink_red_100);
            }
            break;
        }
        default:
            break;
        }
    }
    return consumed;
}

void radiogeddon_scene_hopper_on_exit(void* context) {
    RadioGeddonApp* app = context;
    app->scanner_running = false;
    // Stop the engine (it saves an automatic capture in progress) before the
    // radio session it drives.
    if(app->hopper) radiogeddon_hopper_stop(app->hopper);
    radiogeddon_receiver_view_set_hopping(app->receiver_view, false);
    radiogeddon_receiver_view_set_threshold(app->receiver_view, -127.0f);
    radiogeddon_subghz_rx_stop(app->subghz);
    popup_reset(app->popup);
    FURI_LOG_I(TAG, "exit, free heap %u", (unsigned)memmgr_get_free_heap());
}
