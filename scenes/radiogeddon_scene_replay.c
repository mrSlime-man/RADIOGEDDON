#include "radiogeddon_scene.h"

typedef enum {
    ReplayCustomSend = 300,
    ReplayCustomComplete,
    ReplayCustomClosePopup,
} ReplayCustomEvent;

typedef enum {
    ReplayStateIdle = 0,
    ReplayStateTransmitting = 1,
} ReplayState;

static void
    radiogeddon_scene_replay_button_cb(GuiButtonType result, InputType type, void* context) {
    RadioGeddonApp* app = context;
    if(result == GuiButtonTypeCenter && type == InputTypeShort) {
        view_dispatcher_send_custom_event(app->view_dispatcher, ReplayCustomSend);
    }
}

// Fired from the file-encoder worker thread when a RAW transmission finishes.
static void radiogeddon_scene_replay_tx_complete(void* context) {
    RadioGeddonApp* app = context;
    view_dispatcher_send_custom_event(app->view_dispatcher, ReplayCustomComplete);
}

// Result popup timed out: return to the signal's action menu.
static void radiogeddon_scene_replay_popup_cb(void* context) {
    RadioGeddonApp* app = context;
    view_dispatcher_send_custom_event(app->view_dispatcher, ReplayCustomClosePopup);
}

static void radiogeddon_scene_replay_show_idle(RadioGeddonApp* app) {
    Widget* widget = app->widget;
    widget_reset(widget);
    widget_add_string_element(
        widget, 64, 2, AlignCenter, AlignTop, FontPrimary, "Replay / Transmit");

    char freq[RG_FREQ_TEXT_SIZE];
    rg_freq_text(app->loaded.frequency, freq, sizeof(freq));
    furi_string_reset(app->temp_str);
    furi_string_cat_printf(
        app->temp_str, "%s @ %s MHz\n", furi_string_get_cstr(app->loaded.protocol), freq);
    // Say up front whether the app's own check will refuse, and why.
    RgTxVerdict verdict = radiogeddon_subghz_tx_check(app->subghz, app->loaded.frequency);
    if(verdict != RgTxAllowed) {
        rg_txpolicy_explain(
            verdict, radiogeddon_subghz_region_name(), freq, app->tx_text, sizeof(app->tx_text));
        furi_string_cat_printf(app->temp_str, "TX NOT ALLOWED:\n%s\n", app->tx_text);
    }
#if RG_FEATURE_REGION_TX_GATE
    furi_string_cat_printf(
        app->temp_str,
        "Region: %s (from firmware).\nTX only where the region\nallows it.\n",
        radiogeddon_subghz_region_name());
#else
    furi_string_cat_printf(app->temp_str, "The firmware's own TX\nrules apply.\n");
#endif
    furi_string_cat_printf(
        app->temp_str,
        "Rolling-code protocols are\nrefused; a RAW capture is\nsent exactly as recorded.\n"
        "Only transmit devices you\nare authorized to test.");
    widget_add_text_scroll_element(widget, 0, 14, 128, 38, furi_string_get_cstr(app->temp_str));
    widget_add_button_element(
        widget, GuiButtonTypeCenter, "Send", radiogeddon_scene_replay_button_cb, app);

    view_dispatcher_switch_to_view(app->view_dispatcher, RadioGeddonViewWidget);
}

void radiogeddon_scene_replay_on_enter(void* context) {
    RadioGeddonApp* app = context;
    scene_manager_set_scene_state(app->scene_manager, RadioGeddonSceneReplay, ReplayStateIdle);
    radiogeddon_scene_replay_show_idle(app);
}

static void radiogeddon_scene_replay_finish(RadioGeddonApp* app, const char* msg, bool success) {
    radiogeddon_subghz_tx_stop(app->subghz);
    scene_manager_set_scene_state(app->scene_manager, RadioGeddonSceneReplay, ReplayStateIdle);
    notification_message(app->notifications, success ? &sequence_success : &sequence_error);
    // A long explanation (several lines) starts higher and stays longer.
    bool long_text = strchr(msg, '\n') != NULL;
    popup_reset(app->popup);
    popup_set_header(
        app->popup,
        success ? "Done" : (long_text ? "TX refused" : "Error"),
        64,
        long_text ? 6 : 18,
        AlignCenter,
        AlignCenter);
    popup_set_text(app->popup, msg, 64, long_text ? 36 : 38, AlignCenter, AlignCenter);
    popup_set_context(app->popup, app);
    popup_set_callback(app->popup, radiogeddon_scene_replay_popup_cb);
    popup_set_timeout(app->popup, long_text ? 4000 : 1500);
    popup_enable_timeout(app->popup);
    view_dispatcher_switch_to_view(app->view_dispatcher, RadioGeddonViewPopup);
}

bool radiogeddon_scene_replay_on_event(void* context, SceneManagerEvent event) {
    RadioGeddonApp* app = context;
    bool consumed = false;

    if(event.type == SceneManagerEventTypeCustom) {
        if(event.event == ReplayCustomSend) {
            RadioGeddonTxResult res = radiogeddon_subghz_tx_start(
                app->subghz,
                furi_string_get_cstr(app->file_path),
                radiogeddon_scene_replay_tx_complete,
                app);
            if(res == RadioGeddonTxOk) {
                scene_manager_set_scene_state(
                    app->scene_manager, RadioGeddonSceneReplay, ReplayStateTransmitting);
                popup_reset(app->popup);
                popup_set_header(app->popup, "Transmitting", 64, 26, AlignCenter, AlignCenter);
                popup_set_text(app->popup, "Sending signal...", 64, 42, AlignCenter, AlignCenter);
                view_dispatcher_switch_to_view(app->view_dispatcher, RadioGeddonViewPopup);
                notification_message(app->notifications, &sequence_blink_start_magenta);
            } else {
                const char* msg = "Cannot transmit";
                switch(res) {
                case RadioGeddonTxErrorRegion:
                    // The firmware's radio driver refused (subghz_devices_set_tx).
                    msg = "Firmware blocked TX";
                    break;
                case RadioGeddonTxErrorPolicy: {
                    uint32_t hz = 0;
                    RgTxVerdict v = radiogeddon_subghz_tx_verdict(app->subghz, &hz);
                    char freq[RG_FREQ_TEXT_SIZE];
                    rg_freq_text(hz, freq, sizeof(freq));
                    rg_txpolicy_explain(
                        v,
                        radiogeddon_subghz_region_name(),
                        freq,
                        app->tx_text,
                        sizeof(app->tx_text));
                    msg = app->tx_text;
                    break;
                }
                case RadioGeddonTxErrorProtected:
                    msg = "Protected/rolling code";
                    break;
                case RadioGeddonTxErrorNoDevice:
                    msg = "No radio device";
                    break;
                case RadioGeddonTxErrorNoFile:
                    msg = "File not found";
                    break;
                case RadioGeddonTxErrorParse:
                    msg = "Unsupported file";
                    break;
                case RadioGeddonTxErrorBusy:
                    msg = "Radio busy";
                    break;
                case RadioGeddonTxErrorPreset:
                    msg = "Bad custom preset";
                    break;
                default:
                    break;
                }
                radiogeddon_scene_replay_finish(app, msg, false);
            }
            consumed = true;
        } else if(event.event == ReplayCustomComplete) {
            // Guard against a double-finish: the RAW end-callback and the tick
            // poll can both signal completion.
            if(scene_manager_get_scene_state(app->scene_manager, RadioGeddonSceneReplay) ==
               ReplayStateTransmitting) {
                notification_message(app->notifications, &sequence_blink_stop);
                radiogeddon_scene_replay_finish(app, "Signal sent", true);
            }
            consumed = true;
        } else if(event.event == ReplayCustomClosePopup) {
            scene_manager_previous_scene(app->scene_manager);
            consumed = true;
        }
    } else if(event.type == SceneManagerEventTypeTick) {
        uint32_t state = scene_manager_get_scene_state(app->scene_manager, RadioGeddonSceneReplay);
        if(state == ReplayStateTransmitting) {
            if(!radiogeddon_subghz_is_tx_running(app->subghz)) {
                notification_message(app->notifications, &sequence_blink_stop);
                radiogeddon_scene_replay_finish(app, "Signal sent", true);
            }
        }
        consumed = true;
    }
    return consumed;
}

void radiogeddon_scene_replay_on_exit(void* context) {
    RadioGeddonApp* app = context;
    notification_message(app->notifications, &sequence_blink_stop);
    radiogeddon_subghz_tx_stop(app->subghz); // idempotent; ensures radio is released
    widget_reset(app->widget);
    popup_reset(app->popup);
}
