#include "../radiogeddon.h"

/* Ticks (RG_TICK_PERIOD_MS each) to dwell on a frequency before hopping. */
#define RG_HOPPER_DWELL_TICKS 2

static uint32_t rg_hopper_dwell = 0;

static void radiogeddon_hopper_decode_cb(
    void* context,
    const char* protocol_name,
    SubGhzProtocolType type,
    FuriString* decoded_text) {
    UNUSED(decoded_text);
    RadioGeddon* app = context;
    app->detection_count++;
    rg_receiver_view_set_last_protocol(
        app->receiver_view, protocol_name, type == SubGhzProtocolTypeDynamic);
    rg_receiver_view_set_detections(app->receiver_view, app->detection_count);
    radiogeddon_notify(app, &sequence_blink_green_10);
    /* Linger a little on an active frequency by resetting the dwell timer. */
    rg_hopper_dwell = 0;
}

/* Tune to hopper frequency at `index`, skipping any the radio rejects. */
static void radiogeddon_hopper_tune(RadioGeddon* app, size_t index) {
    rg_radio_stop_rx(app->radio);

    size_t tries = 0;
    uint32_t freq = rg_hopper_frequencies[index];
    while(tries < rg_hopper_frequencies_count && !rg_radio_is_frequency_valid(app->radio, freq)) {
        index = (index + 1) % rg_hopper_frequencies_count;
        freq = rg_hopper_frequencies[index];
        tries++;
    }

    scene_manager_set_scene_state(app->scene_manager, RadioGeddonSceneHopper, index);
    rg_radio_set_frequency(app->radio, freq);
    rg_receiver_view_set_frequency(app->receiver_view, freq);
    rg_radio_start_rx(app->radio);
}

void radiogeddon_scene_hopper_on_enter(void* context) {
    RadioGeddon* app = context;

    app->detection_count = 0;
    rg_hopper_dwell = 0;
    rg_receiver_view_reset(app->receiver_view);
    rg_receiver_view_set_mode(app->receiver_view, RgReceiverModeHopper);
    rg_receiver_view_set_ok_callback(app->receiver_view, NULL, NULL);
    rg_receiver_view_set_status(app->receiver_view, "Scanning...");

    bool ok = rg_radio_begin(app->radio);
    rg_receiver_view_set_radio_ok(app->receiver_view, ok);

    if(ok) {
        rg_radio_set_preset(app->radio, rg_radio_presets[app->preset_index].preset);
        rg_radio_set_decode_callback(app->radio, radiogeddon_hopper_decode_cb, app);
        radiogeddon_hopper_tune(app, 0);
    }

    view_dispatcher_switch_to_view(app->view_dispatcher, RadioGeddonViewReceiver);
}

bool radiogeddon_scene_hopper_on_event(void* context, SceneManagerEvent event) {
    RadioGeddon* app = context;
    bool consumed = false;

    if(event.type == SceneManagerEventTypeTick) {
        if(rg_radio_get_state(app->radio) == RgRadioStateRx) {
            rg_receiver_view_set_rssi(app->receiver_view, rg_radio_get_rssi(app->radio));
            rg_hopper_dwell++;
            if(rg_hopper_dwell >= RG_HOPPER_DWELL_TICKS) {
                rg_hopper_dwell = 0;
                size_t index =
                    scene_manager_get_scene_state(app->scene_manager, RadioGeddonSceneHopper);
                index = (index + 1) % rg_hopper_frequencies_count;
                radiogeddon_hopper_tune(app, index);
            }
        }
        consumed = true;
    }
    return consumed;
}

void radiogeddon_scene_hopper_on_exit(void* context) {
    RadioGeddon* app = context;
    rg_radio_stop_rx(app->radio);
    rg_radio_set_decode_callback(app->radio, NULL, NULL);
}
