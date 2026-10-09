#include "radiogeddon_scene.h"

typedef enum {
    ScannerCustomSelect = 100,
    ScannerCustomTogglePause,
    ScannerCustomResetPeaks,
    ScannerCustomSave,
    ScannerCustomPopupDone,
    // + channel index; kept clear of every other scene's custom event values,
    // because a hit posted just as the scene closes is delivered to the next one.
    ScannerCustomHitBase = 1000,
} ScannerCustomEvent;

static void radiogeddon_scanner_view_cb(RadioGeddonScannerEvent event, void* context) {
    RadioGeddonApp* app = context;
    uint32_t custom = ScannerCustomSelect;
    switch(event) {
    case RadioGeddonScannerEventSelect:
        custom = ScannerCustomSelect;
        break;
    case RadioGeddonScannerEventTogglePause:
        custom = ScannerCustomTogglePause;
        break;
    case RadioGeddonScannerEventResetPeaks:
        custom = ScannerCustomResetPeaks;
        break;
    case RadioGeddonScannerEventSave:
        custom = ScannerCustomSave;
        break;
    }
    view_dispatcher_send_custom_event(app->view_dispatcher, custom);
}

// Scanner thread -> GUI thread.
static void radiogeddon_scanner_hit_cb(size_t index, void* context) {
    RadioGeddonApp* app = context;
    view_dispatcher_send_custom_event(app->view_dispatcher, ScannerCustomHitBase + index);
}

static void radiogeddon_scanner_popup_cb(void* context) {
    RadioGeddonApp* app = context;
    view_dispatcher_send_custom_event(app->view_dispatcher, ScannerCustomPopupDone);
}

static void radiogeddon_scene_scanner_show_message(
    RadioGeddonApp* app,
    const char* header,
    const char* text,
    bool timeout) {
    popup_reset(app->popup);
    popup_set_header(app->popup, header, 64, 18, AlignCenter, AlignCenter);
    popup_set_text(app->popup, text, 64, 38, AlignCenter, AlignCenter);
    if(timeout) {
        popup_set_context(app->popup, app);
        popup_set_callback(app->popup, radiogeddon_scanner_popup_cb);
        popup_set_timeout(app->popup, 1500);
        popup_enable_timeout(app->popup);
    }
    view_dispatcher_switch_to_view(app->view_dispatcher, RadioGeddonViewPopup);
}

// Build the scan list from the saved mask, skipping frequencies the radio
// cannot tune. Returns the number of frequencies written to @p out.
static size_t radiogeddon_scene_scanner_build_list(RadioGeddonApp* app, uint32_t* out) {
    size_t n = 0;
    for(size_t i = 0; i < radiogeddon_frequencies_count && i < 32; i++) {
        if(!(app->settings.scan_mask & (1u << i))) continue;
        uint32_t f = radiogeddon_frequencies[i];
        if(!radiogeddon_subghz_is_frequency_allowed(app->subghz, f)) continue;
        if(n < RADIOGEDDON_SCANNER_MAX_CHANNELS) out[n++] = f;
    }
    return n;
}

void radiogeddon_scene_scanner_on_enter(void* context) {
    RadioGeddonApp* app = context;
    app->scanner_running = false;

    if(!radiogeddon_subghz_is_device_present(app->subghz)) {
        radiogeddon_scene_scanner_show_message(
            app, "No radio", "Sub-GHz device not\nfound or not responding.", false);
        return;
    }

    uint32_t list[RADIOGEDDON_SCANNER_MAX_CHANNELS];
    size_t count = radiogeddon_scene_scanner_build_list(app, list);

    if(!app->scanner) app->scanner = radiogeddon_scanner_alloc(app->subghz);
    radiogeddon_scanner_configure(
        app->scanner,
        list,
        count,
        app->settings.scan_dwell_ms,
        app->settings.scan_threshold_db,
        app->settings.scan_hold_on_hit);
    radiogeddon_scanner_set_callback(app->scanner, radiogeddon_scanner_hit_cb, app);

    radiogeddon_subghz_set_preset(app->subghz, app->preset_index);
    radiogeddon_scanner_view_set_callback(app->scanner_view, radiogeddon_scanner_view_cb, app);
    radiogeddon_scanner_view_update(app->scanner_view, app->scanner);

    if(count > 0) {
        radiogeddon_scanner_start(app->scanner);
        app->scanner_running = true;
    }
    view_dispatcher_switch_to_view(app->view_dispatcher, RadioGeddonViewScanner);
}

static void radiogeddon_scene_scanner_save(RadioGeddonApp* app) {
    FuriString* path = furi_string_alloc();
    radiogeddon_storage_ensure_paths(app->storage);
    radiogeddon_storage_make_scan_path(path);
    bool ok = radiogeddon_scanner_save_csv(
        app->scanner,
        app->storage,
        furi_string_get_cstr(path),
        radiogeddon_presets[app->preset_index].label);
    if(ok) {
        notification_message(app->notifications, &sequence_success);
        // Show only the file name; the folder is always apps_data/radiogeddon/scans.
        size_t slash = furi_string_search_rchar(path, '/');
        if(slash != FURI_STRING_FAILURE) furi_string_right(path, slash + 1);
        furi_string_printf(app->temp_str, "Saved to scans/\n%s", furi_string_get_cstr(path));
        radiogeddon_scene_scanner_show_message(
            app, "Results saved", furi_string_get_cstr(app->temp_str), true);
    } else {
        notification_message(app->notifications, &sequence_error);
        radiogeddon_scene_scanner_show_message(app, "Save failed", "Check the SD card.", true);
    }
    furi_string_free(path);
}

bool radiogeddon_scene_scanner_on_event(void* context, SceneManagerEvent event) {
    RadioGeddonApp* app = context;
    bool consumed = false;

    if(event.type == SceneManagerEventTypeTick) {
        if(app->scanner_running) radiogeddon_scanner_view_update(app->scanner_view, app->scanner);
        consumed = true;
    } else if(event.type == SceneManagerEventTypeCustom) {
        consumed = true;
        if(!app->scanner) return consumed;

        if(event.event >= ScannerCustomHitBase) {
            size_t index = event.event - ScannerCustomHitBase;
            notification_message(app->notifications, &sequence_blink_cyan_10);
            // When the sweep holds on a hit, move the highlight there so OK
            // goes straight to the receiver on that frequency.
            if(radiogeddon_scanner_hold_index(app->scanner) == (int32_t)index) {
                radiogeddon_scanner_view_set_selected(app->scanner_view, index);
            }
            return consumed;
        }

        switch(event.event) {
        case ScannerCustomSelect: {
            size_t sel = radiogeddon_scanner_view_get_selected(app->scanner_view);
            uint32_t freq = radiogeddon_scanner_frequency(app->scanner, sel);
            if(freq) {
                app->frequency = freq;
                radiogeddon_subghz_set_frequency(app->subghz, app->frequency);
                scene_manager_next_scene(app->scene_manager, RadioGeddonSceneReceiver);
            }
            break;
        }
        case ScannerCustomTogglePause:
            radiogeddon_scanner_toggle_pause(app->scanner);
            radiogeddon_scanner_view_update(app->scanner_view, app->scanner);
            break;
        case ScannerCustomResetPeaks:
            radiogeddon_scanner_reset_peaks(app->scanner);
            radiogeddon_scanner_view_update(app->scanner_view, app->scanner);
            notification_message(app->notifications, &sequence_blink_blue_10);
            break;
        case ScannerCustomSave:
            radiogeddon_scene_scanner_save(app);
            break;
        case ScannerCustomPopupDone:
            view_dispatcher_switch_to_view(app->view_dispatcher, RadioGeddonViewScanner);
            break;
        default:
            break;
        }
    }
    return consumed;
}

void radiogeddon_scene_scanner_on_exit(void* context) {
    RadioGeddonApp* app = context;
    app->scanner_running = false;
    if(app->scanner) radiogeddon_scanner_stop(app->scanner);
    popup_reset(app->popup);
}
