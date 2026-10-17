#include "radiogeddon_scene.h"

typedef enum {
    ScannerCustomSelect = 100,
    ScannerCustomRecord,
    ScannerCustomTogglePause,
    ScannerCustomResetPeaks,
    ScannerCustomSave,
    ScannerCustomPopupDone,
    // + channel index; kept clear of every other scene's custom event values,
    // because a hit posted just as the scene closes is delivered to the next one.
    ScannerCustomHitBase = 1000,
    // Full edition: start once the module that opened the Scanner is
    // unloaded (above every hit index; handled first).
    ScannerCustomBegin = 0x7101,
} ScannerCustomEvent;

static RG_UI_TEXT void radiogeddon_scanner_view_cb(RadioGeddonScannerEvent event, void* context) {
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
    case RadioGeddonScannerEventRecord:
        custom = ScannerCustomRecord;
        break;
    }
    view_dispatcher_send_custom_event(app->view_dispatcher, custom);
}

// Scanner thread -> GUI thread.
static RG_UI_TEXT void radiogeddon_scanner_hit_cb(size_t index, void* context) {
    RadioGeddonApp* app = context;
    view_dispatcher_send_custom_event(app->view_dispatcher, ScannerCustomHitBase + index);
}

static RG_UI_TEXT void radiogeddon_scanner_popup_cb(void* context) {
    RadioGeddonApp* app = context;
    view_dispatcher_send_custom_event(app->view_dispatcher, ScannerCustomPopupDone);
}

static RG_UI_TEXT void radiogeddon_scene_scanner_show_message(
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

// Dolphin Lab: the header badge follows the mascot (nothing in Classic).
static RG_UI_TEXT void radiogeddon_scene_scanner_badge(RadioGeddonApp* app) {
#if RG_FEATURE_DOLPHIN_LAB
    RgMascotFrame f;
    bool cyber = false;
    bool on = radiogeddon_ui_badge(app, &f, &cyber);
    radiogeddon_scanner_view_set_badge(app->scanner_view, on ? &f : NULL, cyber);
#else
    UNUSED(app);
#endif
}

static RG_UI_TEXT void radiogeddon_scene_scanner_begin(RadioGeddonApp* app);

RG_UI_TEXT void radiogeddon_scene_scanner_on_enter(void* context) {
    RadioGeddonApp* app = context;
    app->scanner_running = false;

    if(!radiogeddon_subghz_is_device_present(app->subghz)) {
        radiogeddon_scene_scanner_show_message(
            app, "No radio", "Sub-GHz device not\nfound or not responding.", false);
        return;
    }
#if RG_EDITION_FULL
    // Opened from a tool module's screen (the Adaptive Scanner's setup): that
    // module's code is still running now and is unloaded only afterwards.
    // The scanner and its schedule are kept until the main menu, so made
    // now they would sit above the module's blocks and split the free space
    // once it is gone (the RF Activity Logger then no longer fit). Start
    // from the event loop instead, after unloading it.
    if(!radiogeddon_scene_tools_settled(app)) {
        view_dispatcher_send_custom_event(app->view_dispatcher, ScannerCustomBegin);
        return;
    }
#endif
    radiogeddon_scene_scanner_begin(app);
}

static RG_UI_TEXT void radiogeddon_scene_scanner_begin(RadioGeddonApp* app) {
    uint32_t list[RADIOGEDDON_SCANNER_MAX_CHANNELS];
    size_t count = radiogeddon_scene_build_list(
        app,
        app->settings.scan_source,
        app->settings.scan_mask,
        list,
        RADIOGEDDON_SCANNER_MAX_CHANNELS);

#if RG_FEATURE_ADAPTIVE_SCAN
    uint8_t mode = app->scan_mode_run;
#else
    uint8_t mode = 0;
#endif
    // Firmware malloc() stops the device when it fails: check first, with
    // room left for the screen, the GUI and the radio driver's own work.
    size_t need = radiogeddon_scanner_start_cost(app->scanner, mode) + 4096u;
    if(memmgr_heap_get_max_free_block() < need) {
        FURI_LOG_W(
            "RadioGeddon",
            "scanner needs %u, largest block %u",
            (unsigned)need,
            (unsigned)memmgr_heap_get_max_free_block());
        radiogeddon_scene_scanner_show_message(
            app, "Not enough memory", "Close other apps or\nrestart the Flipper.", false);
        return;
    }
    if(!app->scanner) app->scanner = radiogeddon_scanner_alloc(app->subghz);
#if RG_FEATURE_ADAPTIVE_SCAN
    // Standard, or the Adaptive Scanner's schedule (chosen in its setup).
    radiogeddon_scanner_set_mode(app->scanner, app->scan_mode_run);
#endif
    radiogeddon_scanner_configure(
        app->scanner,
        list,
        count,
        app->settings.scan_dwell_ms,
        app->settings.scan_threshold_db,
        app->settings.scan_hold_on_hit);
    radiogeddon_scanner_set_callback(app->scanner, radiogeddon_scanner_hit_cb, app);
#if RG_FEATURE_ADAPTIVE_SCAN
    if(app->scan_mode_run == RgAdaptCustom) {
        // Custom: the favorites come first (still never alone).
        RadioGeddonFavorites* fav = radiogeddon_scene_favorites(app);
        for(size_t i = 0; i < count; i++) {
            uint8_t prio = 0;
            for(size_t k = 0; k < fav->count; k++)
                if(fav->freq[k] == list[i]) prio = 3;
            radiogeddon_scanner_set_priority(app->scanner, i, prio);
        }
    }
#endif
#if RG_FEATURE_JOURNAL
    if(count > 0) radiogeddon_research_record(app, RgJrnScan, 1, NULL);
#endif

    radiogeddon_subghz_set_preset(app->subghz, app->preset_index);
    radiogeddon_scanner_view_set_callback(app->scanner_view, radiogeddon_scanner_view_cb, app);
    radiogeddon_scanner_view_set_external(
        app->scanner_view, radiogeddon_subghz_get_radio(app->subghz) == RadioGeddonRadioExternal);
    radiogeddon_scanner_view_update(app->scanner_view, app->scanner);

    if(count > 0) {
        radiogeddon_scanner_start(app->scanner);
        app->scanner_running = true;
    }
    radiogeddon_ui_set_scanning(
        app, app->scanner_running && !radiogeddon_scanner_is_paused(app->scanner));
    radiogeddon_scene_scanner_badge(app);
#if RG_FEATURE_ADAPTIVE_SCAN
    radiogeddon_memdiag_checkpoint(
        app->scan_mode_run == RgAdaptStandard ? "Scanner started" : "Adaptive Scanner started");
#else
    radiogeddon_memdiag_checkpoint("Scanner started");
#endif
    view_dispatcher_switch_to_view(app->view_dispatcher, RadioGeddonViewScanner);
}

static RG_UI_TEXT void radiogeddon_scene_scanner_save(RadioGeddonApp* app) {
    FuriString* path = furi_string_alloc();
    radiogeddon_storage_ensure_paths(app->storage);
    radiogeddon_storage_make_scan_path(path);
    bool ok = radiogeddon_scanner_save_csv(
        app->scanner,
        app->storage,
        furi_string_get_cstr(path),
        radiogeddon_presets[app->preset_index].label);
    radiogeddon_ui_event(app, ok ? RgMascotEventSaveCompleted : RgMascotEventOperationFailed, 0);
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

RG_UI_TEXT bool radiogeddon_scene_scanner_on_event(void* context, SceneManagerEvent event) {
    RadioGeddonApp* app = context;
    bool consumed = false;

    if(event.type == SceneManagerEventTypeTick) {
        if(app->scanner_running) {
            // Collecting data only while the sweep runs, not when paused.
            radiogeddon_ui_set_scanning(app, !radiogeddon_scanner_is_paused(app->scanner));
            // The sweep thread keeps every reading; only the copy shown waits.
            if(radiogeddon_scene_redraw_due(app)) {
                radiogeddon_scanner_view_update(app->scanner_view, app->scanner);
                radiogeddon_scene_scanner_badge(app);
            }
        }
        consumed = true;
    } else if(event.type == SceneManagerEventTypeCustom) {
        consumed = true;
#if RG_EDITION_FULL
        if(event.event == ScannerCustomBegin) {
            // No module code runs from the event loop: unload, then start.
            radiogeddon_scene_tools_tick(app);
            radiogeddon_scene_scanner_begin(app);
            return consumed;
        }
#endif
        if(!app->scanner) return consumed;

        if(event.event >= ScannerCustomHitBase) {
            size_t index = event.event - ScannerCustomHitBase;
            notification_message(app->notifications, &sequence_blink_cyan_10);
            radiogeddon_ui_event(app, RgMascotEventSignalActivity, index);
            // When the sweep holds on a hit, move the highlight there so OK
            // goes straight to the receiver on that frequency.
            if(radiogeddon_scanner_hold_index(app->scanner) == (int32_t)index) {
                radiogeddon_scanner_view_set_selected(app->scanner_view, index);
            }
            return consumed;
        }

        switch(event.event) {
        case ScannerCustomSelect:
        case ScannerCustomRecord: {
            size_t sel = radiogeddon_scanner_view_get_selected(app->scanner_view);
            uint32_t freq = radiogeddon_scanner_frequency(app->scanner, sel);
            if(freq) {
                app->receiver_autorecord = (event.event == ScannerCustomRecord);
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

RG_UI_TEXT void radiogeddon_scene_scanner_on_exit(void* context) {
    RadioGeddonApp* app = context;
    app->scanner_running = false;
    radiogeddon_ui_set_scanning(app, false);
    if(app->scanner) radiogeddon_scanner_stop(app->scanner);
    popup_reset(app->popup);
    radiogeddon_memdiag_checkpoint("Scanner stopped");
}
