#include "../radiogeddon.h"

/* Decoded-signal callback (worker thread). Updates counters + last protocol. */
static void radiogeddon_receiver_decode_cb(
    void* context,
    const char* protocol_name,
    SubGhzProtocolType type,
    FuriString* decoded_text) {
    UNUSED(decoded_text);
    RadioGeddon* app = context;
    app->detection_count++;
    bool rolling = (type == SubGhzProtocolTypeDynamic);
    rg_receiver_view_set_last_protocol(app->receiver_view, protocol_name, rolling);
    rg_receiver_view_set_detections(app->receiver_view, app->detection_count);
    radiogeddon_notify(app, &sequence_blink_green_10);
}

void radiogeddon_scene_receiver_on_enter(void* context) {
    RadioGeddon* app = context;

    app->detection_count = 0;
    rg_receiver_view_reset(app->receiver_view);
    rg_receiver_view_set_mode(app->receiver_view, RgReceiverModeReceiver);
    rg_receiver_view_set_ok_callback(app->receiver_view, NULL, NULL);
    rg_receiver_view_set_frequency(app->receiver_view, rg_frequencies[app->freq_index].frequency);
    rg_receiver_view_set_status(app->receiver_view, "Listening...");

    bool ok = rg_radio_begin(app->radio);
    rg_receiver_view_set_radio_ok(app->receiver_view, ok);

    if(ok) {
        rg_radio_set_frequency(app->radio, rg_frequencies[app->freq_index].frequency);
        rg_radio_set_preset(app->radio, rg_radio_presets[app->preset_index].preset);
        rg_radio_set_decode_callback(app->radio, radiogeddon_receiver_decode_cb, app);
        if(!rg_radio_start_rx(app->radio)) {
            rg_receiver_view_set_radio_ok(app->receiver_view, false);
            rg_receiver_view_set_status(app->receiver_view, "RX start failed");
        }
    }

    view_dispatcher_switch_to_view(app->view_dispatcher, RadioGeddonViewReceiver);
}

bool radiogeddon_scene_receiver_on_event(void* context, SceneManagerEvent event) {
    RadioGeddon* app = context;
    bool consumed = false;

    if(event.type == SceneManagerEventTypeTick) {
        if(rg_radio_get_state(app->radio) == RgRadioStateRx) {
            rg_receiver_view_set_rssi(app->receiver_view, rg_radio_get_rssi(app->radio));
        }
        consumed = true;
    }
    return consumed;
}

void radiogeddon_scene_receiver_on_exit(void* context) {
    RadioGeddon* app = context;
    rg_radio_stop_rx(app->radio);
    rg_radio_set_decode_callback(app->radio, NULL, NULL);
}
