#include "radiogeddon_scene.h"

// Hopping cadence/behaviour (in 100 ms UI ticks).
#define HOPPER_DWELL_TICKS    2 // ~200 ms listening per frequency when idle
#define HOPPER_HOLD_TICKS     20 // ~2 s dwell once activity is detected
#define HOPPER_RSSI_THRESHOLD (-90.0f) // dBm above noise floor => "activity"

typedef enum {
    HopperCustomDecoded = 500,
    HopperCustomSave,
} HopperCustomEvent;

// Packed scene state: [hold:8][dwell:8][index:8]
static inline uint32_t hopper_pack(uint8_t index, uint8_t dwell, uint8_t hold) {
    return ((uint32_t)hold << 16) | ((uint32_t)dwell << 8) | index;
}
static inline uint8_t hopper_index(uint32_t s) {
    return s & 0xFF;
}
static inline uint8_t hopper_dwell(uint32_t s) {
    return (s >> 8) & 0xFF;
}
static inline uint8_t hopper_hold(uint32_t s) {
    return (s >> 16) & 0xFF;
}

static void radiogeddon_scene_hopper_decode_cb(
    const char* protocol_name,
    uint8_t hash,
    FuriString* text,
    FuriString* serialized,
    void* context) {
    RadioGeddonApp* app = context;
    furi_mutex_acquire(app->history_mutex, FuriWaitForever);
    bool added = radiogeddon_history_add(app->history, protocol_name, hash, text, serialized);
    furi_mutex_release(app->history_mutex);
    if(added) view_dispatcher_send_custom_event(app->view_dispatcher, HopperCustomDecoded);
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
    // Recording is not meaningful while hopping; only handle Save (OK).
    if(event == RadioGeddonReceiverEventSave) {
        view_dispatcher_send_custom_event(app->view_dispatcher, HopperCustomSave);
    }
}

void radiogeddon_scene_hopper_on_enter(void* context) {
    RadioGeddonApp* app = context;

    if(!app->receiver_preserve_history) {
        furi_mutex_acquire(app->history_mutex, FuriWaitForever);
        radiogeddon_history_reset(app->history);
        furi_mutex_release(app->history_mutex);
    }
    app->receiver_preserve_history = false;

    radiogeddon_receiver_view_set_callback(
        app->receiver_view, radiogeddon_scene_hopper_view_cb, app);
    radiogeddon_receiver_view_set_hopping(app->receiver_view, true);
    radiogeddon_receiver_view_set_recording(app->receiver_view, false, 0, false);
    radiogeddon_scene_hopper_refresh_history(app);

    radiogeddon_subghz_set_preset(app->subghz, app->preset_index);

    if(!radiogeddon_subghz_is_device_present(app->subghz)) {
        app->scanner_running = false;
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

    // Start on the first hopper frequency.
    uint32_t freq = radiogeddon_hopper_frequencies[0];
    radiogeddon_subghz_set_frequency(app->subghz, freq);
    app->scanner_running = true; // reuse flag as "hopper active"
    scene_manager_set_scene_state(
        app->scene_manager, RadioGeddonSceneHopper, hopper_pack(0, 0, 0));

    radiogeddon_receiver_view_set_config(
        app->receiver_view, freq, radiogeddon_presets[app->preset_index].label);
    radiogeddon_subghz_rx_start(app->subghz, radiogeddon_scene_hopper_decode_cb, app);

    view_dispatcher_switch_to_view(app->view_dispatcher, RadioGeddonViewReceiver);
}

bool radiogeddon_scene_hopper_on_event(void* context, SceneManagerEvent event) {
    RadioGeddonApp* app = context;
    bool consumed = false;

    if(event.type == SceneManagerEventTypeTick) {
        if(!app->scanner_running) return true;

        float rssi = radiogeddon_subghz_get_rssi(app->subghz);
        radiogeddon_receiver_view_set_rssi(app->receiver_view, rssi);
        radiogeddon_scene_hopper_show_selected(app);

        uint32_t s = scene_manager_get_scene_state(app->scene_manager, RadioGeddonSceneHopper);
        uint8_t index = hopper_index(s);
        uint8_t dwell = hopper_dwell(s);
        uint8_t hold = hopper_hold(s);

        if(rssi >= HOPPER_RSSI_THRESHOLD) {
            // Activity: hold on this frequency to let decoding complete.
            hold = HOPPER_HOLD_TICKS;
            dwell = 0;
        } else if(hold > 0) {
            hold--;
        } else if(++dwell >= HOPPER_DWELL_TICKS) {
            // Idle long enough: hop to the next frequency.
            dwell = 0;
            index = (uint8_t)((index + 1) % radiogeddon_hopper_frequencies_count);
            uint32_t freq = radiogeddon_hopper_frequencies[index];
            radiogeddon_subghz_rx_retune(app->subghz, freq);
            radiogeddon_receiver_view_set_config(
                app->receiver_view, freq, radiogeddon_presets[app->preset_index].label);
        }

        scene_manager_set_scene_state(
            app->scene_manager, RadioGeddonSceneHopper, hopper_pack(index, dwell, hold));
        consumed = true;
    } else if(event.type == SceneManagerEventTypeCustom) {
        switch(event.event) {
        case HopperCustomDecoded:
            radiogeddon_scene_hopper_refresh_history(app);
            notification_message(app->notifications, &sequence_blink_green_10);
            consumed = true;
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
            consumed = true;
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
    radiogeddon_receiver_view_set_hopping(app->receiver_view, false);
    radiogeddon_subghz_rx_stop(app->subghz);
}
