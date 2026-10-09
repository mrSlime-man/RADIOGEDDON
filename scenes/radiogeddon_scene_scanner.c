#include "radiogeddon_scene.h"

typedef enum {
    ScannerCustomSelect = 100,
} ScannerCustomEvent;

static void radiogeddon_scanner_view_cb(RadioGeddonScannerEvent event, void* context) {
    RadioGeddonApp* app = context;
    if(event == RadioGeddonScannerEventSelect) {
        view_dispatcher_send_custom_event(app->view_dispatcher, ScannerCustomSelect);
    }
}

void radiogeddon_scene_scanner_on_enter(void* context) {
    RadioGeddonApp* app = context;

    radiogeddon_scanner_view_set_callback(app->scanner_view, radiogeddon_scanner_view_cb, app);
    radiogeddon_scanner_view_set_frequencies(
        app->scanner_view, radiogeddon_frequencies, radiogeddon_frequencies_count);

    scene_manager_set_scene_state(app->scene_manager, RadioGeddonSceneScanner, 0);

    if(radiogeddon_subghz_is_device_present(app->subghz)) {
        radiogeddon_subghz_set_preset(app->subghz, app->preset_index);
        radiogeddon_subghz_scan_begin(app->subghz);
        app->scanner_running = true;
        view_dispatcher_switch_to_view(app->view_dispatcher, RadioGeddonViewScanner);
    } else {
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
    }
}

bool radiogeddon_scene_scanner_on_event(void* context, SceneManagerEvent event) {
    RadioGeddonApp* app = context;
    bool consumed = false;

    if(event.type == SceneManagerEventTypeTick) {
        if(!app->scanner_running) return true;
        // Sweep one frequency per tick for a responsive, low-jitter display.
        uint32_t cursor =
            scene_manager_get_scene_state(app->scene_manager, RadioGeddonSceneScanner);
        if(cursor < radiogeddon_frequencies_count) {
            float rssi =
                radiogeddon_subghz_probe_rssi(app->subghz, radiogeddon_frequencies[cursor]);
            radiogeddon_scanner_view_set_rssi(app->scanner_view, cursor, rssi);
        }
        cursor = (cursor + 1) % radiogeddon_frequencies_count;
        scene_manager_set_scene_state(app->scene_manager, RadioGeddonSceneScanner, cursor);
        consumed = true;
    } else if(event.type == SceneManagerEventTypeCustom) {
        if(event.event == ScannerCustomSelect) {
            size_t sel = radiogeddon_scanner_view_get_selected(app->scanner_view);
            if(sel < radiogeddon_frequencies_count) {
                app->frequency = radiogeddon_frequencies[sel];
                radiogeddon_subghz_set_frequency(app->subghz, app->frequency);
            }
            scene_manager_next_scene(app->scene_manager, RadioGeddonSceneReceiver);
            consumed = true;
        }
    }
    return consumed;
}

void radiogeddon_scene_scanner_on_exit(void* context) {
    RadioGeddonApp* app = context;
    app->scanner_running = false;
    radiogeddon_subghz_scan_end(app->subghz);
}
