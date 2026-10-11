#include "radiogeddon_scene.h"

#if RG_FEATURE_RANGE_SCAN

// Full edition: the range scan itself. The engine and its screen are
// allocated on entry and freed on exit (also when leaving for Receive), so
// their memory is never held while a receive session runs.

typedef enum {
    RangeScanEventReceive = 900,
    RangeScanEventRecord,
    RangeScanEventPause,
    RangeScanEventRecalibrate,
    RangeScanEventReset,
    RangeScanEventSave,
    RangeScanEventPopupDone,
    RangeScanEventFailed, // on_enter could not start: leave, then explain
    // + point index; clear of every other scene's events, because a hit
    // posted just as the scene closes is delivered to the next one.
    RangeScanEventHitBase = 10000,
} RangeScanEvent;

static void radiogeddon_scene_range_scan_view_cb(RadioGeddonSpectrumEvent event, void* context) {
    RadioGeddonApp* app = context;
    static const uint32_t map[] = {
        [RadioGeddonSpectrumEventReceive] = RangeScanEventReceive,
        [RadioGeddonSpectrumEventRecord] = RangeScanEventRecord,
        [RadioGeddonSpectrumEventTogglePause] = RangeScanEventPause,
        [RadioGeddonSpectrumEventRecalibrate] = RangeScanEventRecalibrate,
        [RadioGeddonSpectrumEventResetPeaks] = RangeScanEventReset,
        [RadioGeddonSpectrumEventSave] = RangeScanEventSave,
    };
    view_dispatcher_send_custom_event(app->view_dispatcher, map[event]);
}

// Scan thread -> GUI thread.
static void radiogeddon_scene_range_scan_hit_cb(uint32_t index, void* context) {
    RadioGeddonApp* app = context;
    view_dispatcher_send_custom_event(app->view_dispatcher, RangeScanEventHitBase + index);
}

static void radiogeddon_scene_range_scan_popup_cb(void* context) {
    RadioGeddonApp* app = context;
    view_dispatcher_send_custom_event(app->view_dispatcher, RangeScanEventPopupDone);
}

void radiogeddon_scene_range_scan_on_enter(void* context) {
    RadioGeddonApp* app = context;
    // The plan was checked (points, memory) by the setup screen; check the
    // bands again in case the radio changed meanwhile.
    radiogeddon_scene_probe_bands(app);
    if(radiogeddon_scene_plan_range(app) != RgRangeOk) {
        // Leave from the event loop: a message shown from here would return
        // to this scene, which would fail again.
        view_dispatcher_send_custom_event(app->view_dispatcher, RangeScanEventFailed);
        return;
    }

    radiogeddon_subghz_set_preset(app->subghz, app->preset_index);
    app->rangescan = radiogeddon_rangescan_alloc(
        app->subghz,
        &app->range,
        app->settings.range_dwell_ms,
        app->settings.scan_threshold_db,
        app->settings.range_hold_on_hit);
    radiogeddon_rangescan_set_callback(app->rangescan, radiogeddon_scene_range_scan_hit_cb, app);

    app->spectrum_view = radiogeddon_spectrum_view_alloc();
    view_dispatcher_add_view(
        app->view_dispatcher,
        RadioGeddonViewSpectrum,
        radiogeddon_spectrum_view_get_view(app->spectrum_view));
    radiogeddon_spectrum_view_set_callback(
        app->spectrum_view, radiogeddon_scene_range_scan_view_cb, app);
    radiogeddon_spectrum_view_set_external(
        app->spectrum_view, radiogeddon_subghz_get_radio(app->subghz) == RadioGeddonRadioExternal);
    radiogeddon_spectrum_view_update(app->spectrum_view, app->rangescan);
    radiogeddon_spectrum_view_set_cursor(app->spectrum_view, app->range_cursor);

    radiogeddon_rangescan_start(app->rangescan);
    radiogeddon_memdiag_sample("Range scan");
    view_dispatcher_switch_to_view(app->view_dispatcher, RadioGeddonViewSpectrum);
}

static void
    radiogeddon_scene_range_scan_popup(RadioGeddonApp* app, const char* header, const char* text) {
    popup_reset(app->popup);
    popup_set_header(app->popup, header, 64, 18, AlignCenter, AlignCenter);
    popup_set_text(app->popup, text, 64, 38, AlignCenter, AlignCenter);
    popup_set_context(app->popup, app);
    popup_set_callback(app->popup, radiogeddon_scene_range_scan_popup_cb);
    popup_set_timeout(app->popup, 1500);
    popup_enable_timeout(app->popup);
    view_dispatcher_switch_to_view(app->view_dispatcher, RadioGeddonViewPopup);
}

static void radiogeddon_scene_range_scan_save(RadioGeddonApp* app) {
    FuriString* path = furi_string_alloc();
    radiogeddon_storage_ensure_paths(app->storage);
    radiogeddon_storage_make_scan_path(path);
    bool ok = radiogeddon_rangescan_save_csv(
        app->rangescan,
        app->storage,
        furi_string_get_cstr(path),
        radiogeddon_presets[app->preset_index].label);
    if(ok) {
        notification_message(app->notifications, &sequence_success);
        size_t slash = furi_string_search_rchar(path, '/');
        if(slash != FURI_STRING_FAILURE) furi_string_right(path, slash + 1);
        furi_string_printf(app->temp_str, "Saved to scans/\n%s", furi_string_get_cstr(path));
        radiogeddon_scene_range_scan_popup(
            app, "Results saved", furi_string_get_cstr(app->temp_str));
    } else {
        notification_message(app->notifications, &sequence_error);
        radiogeddon_scene_range_scan_popup(app, "Save failed", "Check the SD card.");
    }
    furi_string_free(path);
}

bool radiogeddon_scene_range_scan_on_event(void* context, SceneManagerEvent event) {
    RadioGeddonApp* app = context;
    if(!app->rangescan) {
        if(event.type == SceneManagerEventTypeCustom && event.event == RangeScanEventFailed) {
            scene_manager_previous_scene(app->scene_manager);
            radiogeddon_scene_show_message(
                app, "Bad range", "Check the range in\nRange Scanner setup.");
            return true;
        }
        return false;
    }

    if(event.type == SceneManagerEventTypeTick) {
        radiogeddon_spectrum_view_update(app->spectrum_view, app->rangescan);
        return true;
    }
    if(event.type != SceneManagerEventTypeCustom) return false;

    if(event.event >= RangeScanEventHitBase) {
        uint32_t index = event.event - RangeScanEventHitBase;
        notification_message(app->notifications, &sequence_blink_cyan_10);
        // Holding on a hit: put the cursor there, so OK goes to it.
        if(app->settings.range_hold_on_hit) {
            radiogeddon_spectrum_view_set_cursor(app->spectrum_view, index);
        }
        return true;
    }
    switch(event.event) {
    case RangeScanEventReceive:
    case RangeScanEventRecord: {
        uint32_t cursor = radiogeddon_spectrum_view_get_cursor(app->spectrum_view);
        uint32_t freq = radiogeddon_rangescan_frequency(app->rangescan, cursor);
        if(freq && radiogeddon_subghz_is_frequency_allowed(app->subghz, freq)) {
            app->range_cursor = cursor;
            app->frequency = freq;
            radiogeddon_subghz_set_frequency(app->subghz, freq);
            app->receiver_autorecord = (event.event == RangeScanEventRecord);
            scene_manager_next_scene(app->scene_manager, RadioGeddonSceneReceiver);
        }
        break;
    }
    case RangeScanEventPause:
        radiogeddon_rangescan_toggle_pause(app->rangescan);
        radiogeddon_spectrum_view_update(app->spectrum_view, app->rangescan);
        break;
    case RangeScanEventRecalibrate:
        radiogeddon_rangescan_recalibrate(app->rangescan);
        notification_message(app->notifications, &sequence_blink_blue_10);
        break;
    case RangeScanEventReset:
        radiogeddon_rangescan_reset_peaks(app->rangescan);
        notification_message(app->notifications, &sequence_blink_blue_10);
        break;
    case RangeScanEventSave:
        radiogeddon_scene_range_scan_save(app);
        break;
    case RangeScanEventPopupDone:
        view_dispatcher_switch_to_view(app->view_dispatcher, RadioGeddonViewSpectrum);
        break;
    default:
        return false;
    }
    return true;
}

void radiogeddon_scene_range_scan_on_exit(void* context) {
    RadioGeddonApp* app = context;
    popup_reset(app->popup);
    if(app->rangescan) {
        radiogeddon_rangescan_free(app->rangescan); // stops the thread first
        app->rangescan = NULL;
    }
    if(app->spectrum_view) {
        app->range_cursor = radiogeddon_spectrum_view_get_cursor(app->spectrum_view);
        view_dispatcher_remove_view(app->view_dispatcher, RadioGeddonViewSpectrum);
        radiogeddon_spectrum_view_free(app->spectrum_view);
        app->spectrum_view = NULL;
    }
}

#endif
