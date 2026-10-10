#include "radiogeddon_scene.h"

#if RG_FEATURE_WATERFALL

// Full edition: the Waterfall, a history of the range scan's sweeps. It runs
// the Range Scanner's engine (no second radio session) over the range set up
// in Range Scanner setup, with a history buffer of its own. Engine, buffer
// and screen exist only while this scene is open, and are freed on exit
// (also when leaving for Receive), so a receive session never shares the
// heap with them. The history starts again on return.

typedef enum {
    WaterfallEventPause = 950,
    WaterfallEventReceive,
    WaterfallEventSave,
    WaterfallEventSettings,
    WaterfallEventPopupDone,
    WaterfallEventFailed, // on_enter could not start: leave, then explain
} WaterfallEvent;

/* History buffer: up to this much, at least one screen of rows. */
#define WATERFALL_BUF_MAX     (8u * 1024u)
/* Kept free beyond the engine, the history and the screen. */
#define WATERFALL_HEAP_MARGIN (12u * 1024u)

size_t radiogeddon_scene_waterfall_min_bytes(uint32_t points) {
    return rg_waterfall_bytes(points, RADIOGEDDON_WF_VIEW_H);
}

size_t radiogeddon_scene_waterfall_fixed_bytes(uint32_t points) {
    // Engine, screen (object, view and its model) besides the history.
    return radiogeddon_rangescan_memory(points) + sizeof(RadioGeddonWaterfallFrame) + 512u;
}

static void radiogeddon_scene_waterfall_view_cb(RadioGeddonWaterfallEvent event, void* context) {
    RadioGeddonApp* app = context;
    static const uint32_t map[] = {
        [RadioGeddonWaterfallEventTogglePause] = WaterfallEventPause,
        [RadioGeddonWaterfallEventReceive] = WaterfallEventReceive,
        [RadioGeddonWaterfallEventSave] = WaterfallEventSave,
        [RadioGeddonWaterfallEventSettings] = WaterfallEventSettings,
    };
    view_dispatcher_send_custom_event(app->view_dispatcher, map[event]);
}

static void radiogeddon_scene_waterfall_popup_cb(void* context) {
    RadioGeddonApp* app = context;
    view_dispatcher_send_custom_event(app->view_dispatcher, WaterfallEventPopupDone);
}

static void radiogeddon_scene_waterfall_release(RadioGeddonApp* app) {
    if(app->rangescan) {
        radiogeddon_rangescan_free(app->rangescan); // stops the thread first
        app->rangescan = NULL;
    }
    free(app->wf_buf);
    app->wf_buf = NULL;
    if(app->waterfall_view) {
        app->wf_cursor = radiogeddon_waterfall_view_get_cursor(app->waterfall_view);
        view_dispatcher_remove_view(app->view_dispatcher, RadioGeddonViewWaterfall);
        radiogeddon_waterfall_view_free(app->waterfall_view);
        app->waterfall_view = NULL;
    }
}

void radiogeddon_scene_waterfall_on_enter(void* context) {
    RadioGeddonApp* app = context;
    // Setup checked the plan and the memory; the radio may have changed since.
    radiogeddon_scene_probe_bands(app);
    if(radiogeddon_scene_plan_range(app) != RgRangeOk) {
        app->message_header = "Bad range";
        app->message_text = "Check the range in\nRange Scanner setup.";
        view_dispatcher_send_custom_event(app->view_dispatcher, WaterfallEventFailed);
        return;
    }
    uint32_t points = app->range.points;
    size_t fixed = radiogeddon_scene_waterfall_fixed_bytes(points);
    size_t free_block = memmgr_heap_get_max_free_block();
    size_t room = free_block > fixed + WATERFALL_HEAP_MARGIN ?
                      free_block - fixed - WATERFALL_HEAP_MARGIN :
                      0;
    size_t bytes = room < WATERFALL_BUF_MAX ? room : WATERFALL_BUF_MAX;
    if(bytes < radiogeddon_scene_waterfall_min_bytes(points)) {
        app->message_header = "Not enough memory";
        app->message_text = "Free memory is too low\nfor the waterfall.\nUse fewer points.";
        view_dispatcher_send_custom_event(app->view_dispatcher, WaterfallEventFailed);
        return;
    }

    app->wf_buf = malloc(bytes);
    radiogeddon_subghz_set_preset(app->subghz, app->preset_index);
    // Never hold on a hit: a waterfall row needs complete sweeps.
    app->rangescan = radiogeddon_rangescan_alloc(
        app->subghz,
        &app->range,
        app->settings.range_dwell_ms,
        app->settings.scan_threshold_db,
        false);
    radiogeddon_rangescan_waterfall_attach(app->rangescan, app->wf_buf, bytes);

    app->waterfall_view = radiogeddon_waterfall_view_alloc();
    view_dispatcher_add_view(
        app->view_dispatcher,
        RadioGeddonViewWaterfall,
        radiogeddon_waterfall_view_get_view(app->waterfall_view));
    radiogeddon_waterfall_view_set_callback(
        app->waterfall_view, radiogeddon_scene_waterfall_view_cb, app);
    radiogeddon_waterfall_view_set_style(
        app->waterfall_view, app->wf_span_index, app->wf_noise_comp);
    radiogeddon_waterfall_view_set_external(
        app->waterfall_view,
        radiogeddon_subghz_get_radio(app->subghz) == RadioGeddonRadioExternal);
    radiogeddon_waterfall_view_set_cursor(app->waterfall_view, app->wf_cursor);
    radiogeddon_waterfall_view_update(app->waterfall_view, app->rangescan);

    radiogeddon_rangescan_start(app->rangescan);
    radiogeddon_memdiag_sample("Waterfall");
    view_dispatcher_switch_to_view(app->view_dispatcher, RadioGeddonViewWaterfall);
}

static void
    radiogeddon_scene_waterfall_popup(RadioGeddonApp* app, const char* header, const char* text) {
    popup_reset(app->popup);
    popup_set_header(app->popup, header, 64, 18, AlignCenter, AlignCenter);
    popup_set_text(app->popup, text, 64, 38, AlignCenter, AlignCenter);
    popup_set_context(app->popup, app);
    popup_set_callback(app->popup, radiogeddon_scene_waterfall_popup_cb);
    popup_set_timeout(app->popup, 1500);
    popup_enable_timeout(app->popup);
    view_dispatcher_switch_to_view(app->view_dispatcher, RadioGeddonViewPopup);
}

static void radiogeddon_scene_waterfall_save(RadioGeddonApp* app) {
    FuriString* path = furi_string_alloc();
    radiogeddon_storage_ensure_paths(app->storage);
    radiogeddon_storage_make_scan_path_prefix(path, "WF");
    bool ok = radiogeddon_rangescan_waterfall_save_csv(
        app->rangescan,
        app->storage,
        furi_string_get_cstr(path),
        radiogeddon_presets[app->preset_index].label);
    if(ok) {
        notification_message(app->notifications, &sequence_success);
        size_t slash = furi_string_search_rchar(path, '/');
        if(slash != FURI_STRING_FAILURE) furi_string_right(path, slash + 1);
        furi_string_printf(app->temp_str, "Saved to scans/\n%s", furi_string_get_cstr(path));
        radiogeddon_scene_waterfall_popup(
            app, "History saved", furi_string_get_cstr(app->temp_str));
    } else {
        notification_message(app->notifications, &sequence_error);
        radiogeddon_scene_waterfall_popup(app, "Save failed", "Check the SD card.");
    }
    furi_string_free(path);
}

bool radiogeddon_scene_waterfall_on_event(void* context, SceneManagerEvent event) {
    RadioGeddonApp* app = context;
    if(!app->rangescan) {
        // Not started: go back to setup and say why there (never back here,
        // which would only fail again).
        if(event.type == SceneManagerEventTypeCustom && event.event == WaterfallEventFailed) {
            const char* header = app->message_header;
            const char* text = app->message_text;
            scene_manager_previous_scene(app->scene_manager);
            radiogeddon_scene_show_message(app, header, text);
            return true;
        }
        return false;
    }

    if(event.type == SceneManagerEventTypeTick) {
        radiogeddon_waterfall_view_update(app->waterfall_view, app->rangescan);
        return true;
    }
    if(event.type != SceneManagerEventTypeCustom) return false;
    switch(event.event) {
    case WaterfallEventPause:
        radiogeddon_rangescan_toggle_pause(app->rangescan);
        radiogeddon_waterfall_view_update(app->waterfall_view, app->rangescan);
        break;
    case WaterfallEventReceive: {
        uint32_t freq = radiogeddon_waterfall_view_get_cursor_hz(app->waterfall_view);
        if(freq && radiogeddon_subghz_is_frequency_allowed(app->subghz, freq)) {
            app->frequency = freq;
            radiogeddon_subghz_set_frequency(app->subghz, freq);
            app->receiver_autorecord = false;
            scene_manager_next_scene(app->scene_manager, RadioGeddonSceneReceiver);
        }
        break;
    }
    case WaterfallEventSave:
        radiogeddon_scene_waterfall_save(app);
        break;
    case WaterfallEventSettings:
        radiogeddon_waterfall_view_get_style(
            app->waterfall_view, &app->wf_span_index, &app->wf_noise_comp);
        radiogeddon_waterfall_view_update(app->waterfall_view, app->rangescan);
        break;
    case WaterfallEventPopupDone:
        view_dispatcher_switch_to_view(app->view_dispatcher, RadioGeddonViewWaterfall);
        break;
    default:
        return false;
    }
    return true;
}

void radiogeddon_scene_waterfall_on_exit(void* context) {
    RadioGeddonApp* app = context;
    popup_reset(app->popup);
    radiogeddon_scene_waterfall_release(app);
}

#endif
