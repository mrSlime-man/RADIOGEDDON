#include "../radiogeddon.h"

#define RG_TMP_CAPTURE_NAME ".rg_capture"
#define RG_TMP_CAPTURE_PATH EXT_PATH("subghz/.rg_capture.sub")

typedef enum {
    RadioGeddonRecordEventStopSave = 100,
} RadioGeddonRecordEvent;

/* OK on the live view -> request stop + save. Runs on the UI thread. */
static void radiogeddon_record_ok_callback(void* context) {
    RadioGeddon* app = context;
    view_dispatcher_send_custom_event(app->view_dispatcher, RadioGeddonRecordEventStopSave);
}

static void radiogeddon_record_decode_cb(
    void* context,
    const char* protocol_name,
    SubGhzProtocolType type,
    FuriString* decoded_text) {
    UNUSED(decoded_text);
    RadioGeddon* app = context;
    app->detection_count++;
    rg_receiver_view_set_last_protocol(
        app->receiver_view, protocol_name, type == SubGhzProtocolTypeDynamic);
}

void radiogeddon_scene_record_on_enter(void* context) {
    RadioGeddon* app = context;

    scene_manager_set_scene_state(app->scene_manager, RadioGeddonSceneRecord, 0);
    app->detection_count = 0;

    rg_receiver_view_reset(app->receiver_view);
    rg_receiver_view_set_mode(app->receiver_view, RgReceiverModeRecord);
    rg_receiver_view_set_frequency(app->receiver_view, rg_frequencies[app->freq_index].frequency);
    rg_receiver_view_set_ok_callback(app->receiver_view, radiogeddon_record_ok_callback, app);
    rg_receiver_view_set_status(app->receiver_view, "Recording...");

    bool ok = rg_radio_begin(app->radio);
    rg_receiver_view_set_radio_ok(app->receiver_view, ok);

    if(ok) {
        rg_radio_set_frequency(app->radio, rg_frequencies[app->freq_index].frequency);
        rg_radio_set_preset(app->radio, rg_radio_presets[app->preset_index].preset);
        rg_radio_set_decode_callback(app->radio, radiogeddon_record_decode_cb, app);

        if(!rg_radio_start_rx(app->radio)) {
            rg_receiver_view_set_radio_ok(app->receiver_view, false);
            rg_receiver_view_set_status(app->receiver_view, "RX start failed");
        } else if(!rg_radio_record_start(app->radio, RG_TMP_CAPTURE_NAME)) {
            rg_radio_stop_rx(app->radio);
            rg_receiver_view_set_radio_ok(app->receiver_view, false);
            rg_receiver_view_set_status(app->receiver_view, "SD write failed");
        } else {
            radiogeddon_notify(app, &sequence_blink_start_red);
        }
    }

    view_dispatcher_switch_to_view(app->view_dispatcher, RadioGeddonViewReceiver);
}

bool radiogeddon_scene_record_on_event(void* context, SceneManagerEvent event) {
    RadioGeddon* app = context;
    bool consumed = false;

    if(event.type == SceneManagerEventTypeTick) {
        if(rg_radio_get_state(app->radio) == RgRadioStateRx) {
            rg_receiver_view_set_rssi(app->receiver_view, rg_radio_get_rssi(app->radio));
            rg_receiver_view_set_samples(
                app->receiver_view, (uint32_t)rg_radio_record_sample_count(app->radio));
        }
        consumed = true;
    } else if(event.type == SceneManagerEventTypeCustom) {
        if(event.event == RadioGeddonRecordEventStopSave) {
            size_t samples = rg_radio_record_sample_count(app->radio);
            rg_radio_record_stop(app->radio);
            rg_radio_stop_rx(app->radio);
            radiogeddon_notify(app, &sequence_blink_stop);

            if(samples == 0) {
                rg_receiver_view_set_status(app->receiver_view, "No samples - keep waiting");
                /* Nothing captured: resume so the user can try again. */
                rg_radio_start_rx(app->radio);
                rg_radio_record_start(app->radio, RG_TMP_CAPTURE_NAME);
            } else {
                /* Mark that the temp capture is being saved (keep the file). */
                scene_manager_set_scene_state(app->scene_manager, RadioGeddonSceneRecord, 1);
                scene_manager_next_scene(app->scene_manager, RadioGeddonSceneRecordName);
            }
            consumed = true;
        }
    }
    return consumed;
}

void radiogeddon_scene_record_on_exit(void* context) {
    RadioGeddon* app = context;

    if(rg_radio_is_recording(app->radio)) rg_radio_record_stop(app->radio);
    rg_radio_stop_rx(app->radio);
    rg_radio_set_decode_callback(app->radio, NULL, NULL);
    radiogeddon_notify(app, &sequence_blink_stop);

    /* If we are not in the middle of saving, discard the temp capture. */
    uint32_t state = scene_manager_get_scene_state(app->scene_manager, RadioGeddonSceneRecord);
    if(state != 1) {
        storage_common_remove(app->storage, RG_TMP_CAPTURE_PATH);
    }
}
