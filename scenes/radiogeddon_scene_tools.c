#include "radiogeddon_scene.h"

#if RG_EDITION_FULL

#if RG_FEATURE_DOLPHIN_LAB
#include "../views/radiogeddon_mascot_draw.h"
#endif

/*
 * Full edition: the app's side of the tool screens built into modules
 * (radiogeddon_modules.h), and the host table every screen module calls
 * back through (radiogeddon_host.h).
 *
 * Each such scene has a forwarder here: on entry it loads its module (or
 * finds it loaded) and hands the call over; events and exit follow. A
 * module stays loaded while one of its screens is shown. On exit it is only
 * marked as not shown: the call that leaves a screen comes from that
 * module's own code (a key handler opening another screen), so the module
 * is unloaded on the next tick, when none of its code is running. A screen
 * of another module opened from one (a tip opening Find Similar Signals)
 * takes the second slot meanwhile. A module that cannot be loaded (not
 * enough memory, a damaged install) closes its screen and says why.
 */

#define TOOLS_FAILED_EVENT 0x7001u

/* Engines a tool screen makes and the app keeps after the screen closes
 * (until the main menu). They are made before its module is loaded: made
 * by the screen, they would sit above the module's blocks and split the
 * heap once it is unloaded. */
typedef enum {
    ToolPrepNone,
    ToolPrepScanner, // app->scanner, in the schedule of the settings
    ToolPrepHopper, // app->hopper
} ToolPrep;

typedef struct {
    uint32_t scene;
    const char* file;
    uint8_t index;
    uint8_t work_kb; // heap its work needs besides the module
    uint8_t prep; // ToolPrep
} ToolScene;

static const ToolScene tool_scenes[] = {
#if RG_FEATURE_FINGERPRINT
    {RadioGeddonSceneSimilar, RADIOGEDDON_MODULE_FP, 0, 16, ToolPrepNone},
#endif
#if RG_FEATURE_PULSE_STUDIO
    {RadioGeddonSceneStudio, RADIOGEDDON_MODULE_STUDIO, 0, 20, ToolPrepNone},
    {RadioGeddonSceneTimeline, RADIOGEDDON_MODULE_STUDIO, 1, 16, ToolPrepNone},
#endif
    {RadioGeddonSceneAnalyze, RADIOGEDDON_MODULE_REPORTS, 0, 14, ToolPrepNone},
    {RadioGeddonSceneCrypto, RADIOGEDDON_MODULE_REPORTS, 1, 2, ToolPrepNone},
    {RadioGeddonSceneCompareResult, RADIOGEDDON_MODULE_REPORTS, 2, 14, ToolPrepNone},
    {RadioGeddonSceneReport, RADIOGEDDON_MODULE_REPORTS, 3, 14, ToolPrepNone},
#if RG_FEATURE_ACTIVITY_LOG
    {RadioGeddonSceneActlog, RADIOGEDDON_MODULE_LOG, 0, 8, ToolPrepScanner},
    {RadioGeddonSceneAdaptive, RADIOGEDDON_MODULE_LOG, 1, 1, ToolPrepNone},
    {RadioGeddonSceneHeatmap, RADIOGEDDON_MODULE_HEATMAP, 0, 12, ToolPrepNone},
#endif
    {RadioGeddonSceneAbout, RADIOGEDDON_MODULE_ABOUT, 0, 2, ToolPrepNone},
#if RG_FEATURE_DOLPHIN_LAB
    {RadioGeddonSceneRoom, RADIOGEDDON_MODULE_LAB, 0, 4, ToolPrepNone},
#endif
#if RG_FEATURE_JOURNAL
    {RadioGeddonSceneJournal, RADIOGEDDON_MODULE_LAB, 1, 3, ToolPrepNone},
#endif
#if RG_FEATURE_DOLPHIN_LAB
    {RadioGeddonSceneCustomize, RADIOGEDDON_MODULE_LAB, 2, 1, ToolPrepNone},
#endif
#if RG_FEATURE_ASSISTANT
    {RadioGeddonSceneAssistant, RADIOGEDDON_MODULE_LAB, 3, 1, ToolPrepNone},
#endif
#if RG_FEATURE_RANGE_SCAN
    // The setup checks the engine's memory itself before starting a scan.
    {RadioGeddonSceneRangeSetup, RADIOGEDDON_MODULE_RANGE, 0, 1, ToolPrepNone},
    {RadioGeddonSceneRangeScan, RADIOGEDDON_MODULE_RANGE, 1, 4, ToolPrepNone},
    {RadioGeddonSceneProfiles, RADIOGEDDON_MODULE_RANGE, 2, 1, ToolPrepNone},
    {RadioGeddonSceneProfileName, RADIOGEDDON_MODULE_RANGE, 3, 1, ToolPrepNone},
#endif
#if RG_FEATURE_WATERFALL
    {RadioGeddonSceneWaterfall, RADIOGEDDON_MODULE_RANGE, 4, 4, ToolPrepNone},
#endif
    {RadioGeddonSceneConfig, RADIOGEDDON_MODULE_SETTINGS, 0, 1, ToolPrepNone},
    {RadioGeddonSceneBands, RADIOGEDDON_MODULE_SETTINGS, 1, 1, ToolPrepNone},
    {RadioGeddonSceneFavorites, RADIOGEDDON_MODULE_SETTINGS, 2, 1, ToolPrepNone},
    {RadioGeddonSceneFavoriteOptions, RADIOGEDDON_MODULE_SETTINGS, 3, 1, ToolPrepNone},
    {RadioGeddonSceneFrequency, RADIOGEDDON_MODULE_SETTINGS, 4, 1, ToolPrepNone},
    // The analysis menu: Multi-Compare loads its engine and Smart Protocol
    // Discovery its module (app->module) with their own memory checks, and
    // Decode checks the decoders' memory before running them.
    {RadioGeddonSceneAnalyzeMenu, RADIOGEDDON_MODULE_ANALYZE, 0, 1, ToolPrepNone},
    {RadioGeddonSceneMulti, RADIOGEDDON_MODULE_ANALYZE, 1, 1, ToolPrepNone},
    {RadioGeddonSceneMultiResult, RADIOGEDDON_MODULE_ANALYZE, 2, 1, ToolPrepNone},
    {RadioGeddonSceneUnknown, RADIOGEDDON_MODULE_ANALYZE, 3, 1, ToolPrepNone},
    {RadioGeddonSceneDecode, RADIOGEDDON_MODULE_ANALYZE, 4, 1, ToolPrepNone},
    // The Scanner checks the sweep's memory itself before starting it.
    {RadioGeddonSceneScanner, RADIOGEDDON_MODULE_SCAN, 0, 1, ToolPrepScanner},
    // The Database list sizes its index to the heap left (radiogeddon_db.h).
    {RadioGeddonSceneSavedList, RADIOGEDDON_MODULE_DB, 0, 4, ToolPrepNone},
    {RadioGeddonSceneDbOptions, RADIOGEDDON_MODULE_DB, 1, 1, ToolPrepNone},
    {RadioGeddonSceneFileDetails, RADIOGEDDON_MODULE_DB, 2, 1, ToolPrepNone},
    {RadioGeddonSceneRename, RADIOGEDDON_MODULE_DB, 3, 1, ToolPrepNone},
    // The Hopper checks the receive session's memory itself.
    {RadioGeddonSceneHopper, RADIOGEDDON_MODULE_HOPPER, 0, 1, ToolPrepHopper},
    {RadioGeddonSceneHopperStats, RADIOGEDDON_MODULE_HOPPER, 1, 1, ToolPrepNone},
#if RG_FEATURE_BITSTREAM
    {RadioGeddonSceneBitstream,
     RADIOGEDDON_MODULE_BITS,
     0,
     (RADIOGEDDON_BITSTREAM_WORK + 1023u) / 1024u,
     ToolPrepNone},
#endif
};

/* ---- the host table -------------------------------------------------------- */

static RG_UI_TEXT void
    host_research_record(RadioGeddonApp* app, uint32_t ev, uint32_t n, const char* note) {
#if RG_FEATURE_JOURNAL
    radiogeddon_research_record(app, (RgJournalEvent)ev, n, note);
#else
    UNUSED(app);
    UNUSED(ev);
    UNUSED(n);
    UNUSED(note);
#endif
}

#if RG_FEATURE_JOURNAL
static RG_UI_TEXT void
    host_research_step(RadioGeddonApp* app, const char* path, uint32_t step, uint32_t arg) {
    radiogeddon_research_step(app, path, (RadioGeddonResearchStep)step, arg);
}
#endif

static RG_UI_TEXT void host_ui_event(RadioGeddonApp* app, uint32_t event, uint32_t arg) {
    radiogeddon_ui_event(app, (RgMascotEvent)event, arg);
}

static RG_UI_TEXT void host_ui_cancel(RadioGeddonApp* app) {
    radiogeddon_ui_cancel(app);
}

static RG_UI_TEXT void host_ui_set_scanning(RadioGeddonApp* app, bool scanning) {
    radiogeddon_ui_set_scanning(app, scanning);
}

static RG_UI_TEXT void host_ui_apply_style(RadioGeddonApp* app) {
#if RG_FEATURE_DOLPHIN_LAB
    radiogeddon_ui_apply_style(app);
#else
    UNUSED(app);
#endif
}

static RG_UI_TEXT uint8_t host_subghz_get_radio(RadioGeddonSubGhz* subghz) {
    return (uint8_t)radiogeddon_subghz_get_radio(subghz);
}

static RG_UI_TEXT uint8_t
    host_subghz_set_radio(RadioGeddonSubGhz* subghz, uint8_t radio, bool ext_power) {
    return (uint8_t)radiogeddon_subghz_set_radio(subghz, (RadioGeddonRadio)radio, ext_power);
}

static RG_UI_TEXT uint8_t host_subghz_record_start(RadioGeddonSubGhz* subghz) {
    return (uint8_t)radiogeddon_subghz_record_start(subghz);
}

static RG_UI_TEXT bool host_subghz_record_status(RadioGeddonSubGhz* subghz, void* stats) {
    return radiogeddon_subghz_record_status(subghz, stats);
}

static RG_UI_TEXT const char* host_recorder_error_text(uint8_t error) {
    return radiogeddon_recorder_error_text((RadioGeddonRecordError)error);
}

static RG_UI_TEXT bool host_ui_active(RadioGeddonApp* app) {
    return radiogeddon_ui_active(app);
}

static RG_UI_TEXT bool host_ui_badge(RadioGeddonApp* app, void* frame, bool* cyber) {
#if RG_FEATURE_DOLPHIN_LAB
    return radiogeddon_ui_badge(app, frame, cyber);
#else
    UNUSED(app);
    UNUSED(frame);
    *cyber = false;
    return false;
#endif
}

static RG_UI_TEXT uint8_t host_plan_range(RadioGeddonApp* app) {
    return (uint8_t)radiogeddon_scene_plan_range(app);
}

static RG_UI_TEXT const char* host_preset_label(uint8_t index) {
    return radiogeddon_presets[index].label;
}

static RG_UI_TEXT bool
    host_session_capture_saved(Storage* storage, const char* file, char* out, size_t size) {
#if RG_FEATURE_SESSIONS
    return radiogeddon_session_capture_saved(storage, file, out, size);
#else
    UNUSED(storage);
    UNUSED(file);
    if(size) out[0] = '\0';
    return false;
#endif
}

static RG_UI_TEXT void host_session_active(Storage* storage, char* out, size_t size) {
#if RG_FEATURE_SESSIONS
    radiogeddon_session_active(storage, out, size);
#else
    UNUSED(storage);
    if(size) out[0] = '\0';
#endif
}

static RG_UI_TEXT void
    host_sessions_rename_signal(Storage* storage, const char* from, const char* to) {
#if RG_FEATURE_SESSIONS
    radiogeddon_sessions_rename_signal(storage, from, to);
#else
    UNUSED(storage);
    UNUSED(from);
    UNUSED(to);
#endif
}

static RG_UI_TEXT void* host_favorites(RadioGeddonApp* app) {
    return radiogeddon_scene_favorites(app);
}

static RG_UI_TEXT bool host_favorites_save(Storage* storage, const void* favorites) {
    return radiogeddon_favorites_save(storage, favorites);
}

static RG_UI_TEXT void host_scanner_snapshot(RadioGeddonScanner* scanner, void* snapshot) {
    radiogeddon_scanner_snapshot(scanner, snapshot);
}

static RG_UI_TEXT void
    host_draw_badge(void* canvas, int32_t x, int32_t y, bool cyber, const void* frame) {
#if RG_FEATURE_DOLPHIN_LAB
    rg_draw_badge(canvas, x, y, cyber, frame);
#else
    UNUSED(canvas);
    UNUSED(x);
    UNUSED(y);
    UNUSED(cyber);
    UNUSED(frame);
#endif
}

const RadioGeddonHost radiogeddon_host_table = {
    .show_message = radiogeddon_scene_show_message,
    .show_confirm = radiogeddon_scene_show_confirm,
    .show_busy = radiogeddon_scene_show_busy,
    .show_progress = radiogeddon_scene_show_progress,
    .progress = radiogeddon_scene_progress,
    .progress_end = radiogeddon_scene_progress_end,
    .module_load = radiogeddon_scene_module_load,
    .module_unload = radiogeddon_scene_module_unload,
#if RG_FEATURE_MULTI_COMPARE
    .multi_add = radiogeddon_multi_add,
    .multi_clear = radiogeddon_multi_clear,
    .multi_memory = radiogeddon_scene_multi_memory,
#endif
    .research_record = host_research_record,
#if RG_FEATURE_JOURNAL
    .research_step = host_research_step,
    .research_set_context = radiogeddon_research_set_context,
    .research_context_path = radiogeddon_research_context_path,
    .research_set_similar = radiogeddon_research_set_similar,
    .research_session_added = radiogeddon_research_session_added,
    .research_clear = radiogeddon_research_clear,
    .research_dismiss_tip = radiogeddon_research_dismiss_tip,
#endif
    .ui_event = host_ui_event,
    .ui_cancel = host_ui_cancel,
    .ui_set_scanning = host_ui_set_scanning,
    .ui_apply_style = host_ui_apply_style,
    .ui_active = host_ui_active,
    .redraw_due = radiogeddon_scene_redraw_due,
    .ui_badge = host_ui_badge,
    .probe_bands = radiogeddon_scene_probe_bands,
    .plan_range = host_plan_range,
    .save_settings = radiogeddon_app_save_settings,
    .build_list = radiogeddon_scene_build_list,
    .memdiag_sample = radiogeddon_memdiag_sample,
    .preset_label = host_preset_label,
    .scanner_alloc = radiogeddon_scanner_alloc,
    .scanner_configure = radiogeddon_scanner_configure,
    .scanner_set_callback = radiogeddon_scanner_set_callback,
#if RG_FEATURE_ADAPTIVE_SCAN
    .scanner_set_mode = radiogeddon_scanner_set_mode,
    .scanner_reset_learning = radiogeddon_scanner_reset_learning,
#endif
    .scanner_set_observer = radiogeddon_scanner_set_observer,
    .scanner_start = radiogeddon_scanner_start,
    .scanner_stop = radiogeddon_scanner_stop,
    .scanner_lock = radiogeddon_scanner_lock,
    .scanner_unlock = radiogeddon_scanner_unlock,
    .scanner_reset_peaks = radiogeddon_scanner_reset_peaks,
    .subghz_is_device_present = radiogeddon_subghz_is_device_present,
    .subghz_set_preset = radiogeddon_subghz_set_preset,
    .subghz_set_frequency = radiogeddon_subghz_set_frequency,
    .subghz_is_frequency_allowed = radiogeddon_subghz_is_frequency_allowed,
    .subghz_get_radio = host_subghz_get_radio,
    .subghz_scan_begin = radiogeddon_subghz_scan_begin,
    .subghz_scan_end = radiogeddon_subghz_scan_end,
    .subghz_probe_rssi_dwell = radiogeddon_subghz_probe_rssi_dwell,
    .subghz_set_radio = host_subghz_set_radio,
    .subghz_device_name = radiogeddon_subghz_device_name,
    .subghz_region_name = radiogeddon_subghz_region_name,
    .settings_default_hop_mask = radiogeddon_settings_default_hop_mask,
    .subghz_rx_start = radiogeddon_subghz_rx_start,
    .subghz_rx_stop = radiogeddon_subghz_rx_stop,
    .subghz_rx_retune = radiogeddon_subghz_rx_retune,
    .subghz_get_rssi = radiogeddon_subghz_get_rssi,
    .subghz_record_start = host_subghz_record_start,
    .subghz_record_stop = radiogeddon_subghz_record_stop,
    .subghz_record_status = host_subghz_record_status,
    .subghz_record_pending = radiogeddon_subghz_record_pending,
    .subghz_record_save = radiogeddon_subghz_record_save,
    .subghz_record_discard = radiogeddon_subghz_record_discard,
    .recorder_error_text = host_recorder_error_text,
    .radio_memory_ok = radiogeddon_scene_radio_memory_ok,
    .radio_memory_learn = radiogeddon_scene_radio_memory_learn,
    .receiver_badge = radiogeddon_scene_receiver_badge,
    .session_capture_saved = host_session_capture_saved,
    .session_active = host_session_active,
    .show_report = radiogeddon_scene_show_report,
    .unknown_begin = radiogeddon_scene_unknown_begin,
    .unknown_end = radiogeddon_scene_unknown_end,
    .decoders_fit = radiogeddon_scene_decoders_fit,
    .analysis_decode = radiogeddon_analysis_decode,
    .memdiag_stats = radiogeddon_memdiag_stats,
    .memdiag_diags = radiogeddon_memdiag_diags,
    .subghz_session_cost = radiogeddon_subghz_session_cost,
    .radio_cost = radiogeddon_scene_radio_cost,
    .var_list_release = radiogeddon_scene_var_list_release,
    .db_release = radiogeddon_scene_db_release,
    .sessions_rename_signal = host_sessions_rename_signal,
    .favorites = host_favorites,
    .favorites_save = host_favorites_save,
    .memdiag_checkpoint = radiogeddon_memdiag_checkpoint,
    .scanner_start_cost = radiogeddon_scanner_start_cost,
    .scanner_frequency = radiogeddon_scanner_frequency,
    .scanner_hold_index = radiogeddon_scanner_hold_index,
    .scanner_is_paused = radiogeddon_scanner_is_paused,
    .scanner_toggle_pause = radiogeddon_scanner_toggle_pause,
#if RG_FEATURE_ADAPTIVE_SCAN
    .scanner_set_priority = radiogeddon_scanner_set_priority,
#endif
    .scanner_snapshot = host_scanner_snapshot,
    .scanner_save_csv = radiogeddon_scanner_save_csv,
    .tools_settled = radiogeddon_scene_tools_settled,
    .tools_tick = radiogeddon_scene_tools_tick,
    .draw_badge = host_draw_badge,
    .decoders_memory_ok = radiogeddon_scene_decoders_memory_ok,
};

/* ---- slots ----------------------------------------------------------------- */

static RG_UI_TEXT const ToolScene* tools_find(uint32_t scene) {
    for(size_t i = 0; i < COUNT_OF(tool_scenes); i++)
        if(tool_scenes[i].scene == scene) return &tool_scenes[i];
    return NULL;
}

static RG_UI_TEXT void tools_unload_slot(RadioGeddonApp* app, size_t i) {
    radiogeddon_module_unload(app->tool[i].module);
    app->tool[i].module = NULL;
    app->tool[i].api = NULL;
    app->tool[i].file = NULL;
    app->tool[i].shown = false;
}

/* Free heap kept when an engine is made ahead of its screen's module (the
 * module, its work and the GUI still to come). */
#define TOOLS_PREP_MARGIN (8u * 1024u)

/* Make @p t's kept engine before its module is loaded (ToolPrep), when it
 * fits with room to spare; otherwise the screen makes it later, as before. */
static RG_UI_TEXT void tools_prepare(RadioGeddonApp* app, const ToolScene* t) {
    size_t largest = memmgr_heap_get_max_free_block();
    if(t->prep == ToolPrepScanner && !app->scanner) {
#if RG_FEATURE_ADAPTIVE_SCAN
        // The Scanner runs the schedule its setup chose; the Logger the
        // settings' one.
        uint8_t mode = t->scene == RadioGeddonSceneScanner ? app->scan_mode_run :
                                                             app->settings.scan_mode;
#else
        uint8_t mode = 0;
#endif
        if(largest < radiogeddon_scanner_start_cost(NULL, mode) + TOOLS_PREP_MARGIN) return;
        app->scanner = radiogeddon_scanner_alloc(app->subghz);
#if RG_FEATURE_ADAPTIVE_SCAN
        radiogeddon_scanner_set_mode(app->scanner, mode);
#endif
    } else if(t->prep == ToolPrepHopper && !app->hopper) {
        if(largest < radiogeddon_hopper_object_cost() + TOOLS_PREP_MARGIN) return;
        app->hopper = radiogeddon_hopper_alloc(app->subghz);
    }
}

/* The module of @p t, loaded if needed; NULL with the reason set. */
static RG_UI_TEXT const RadioGeddonToolModule* tools_get(RadioGeddonApp* app, const ToolScene* t) {
    for(size_t i = 0; i < COUNT_OF(app->tool); i++) {
        if(app->tool[i].module && app->tool[i].file == t->file) {
            app->tool[i].shown = true;
            return app->tool[i].api;
        }
    }
    for(size_t i = 0; i < COUNT_OF(app->tool); i++) {
        if(app->tool[i].module) continue;
        tools_prepare(app, t);
        RadioGeddonModuleStatus status;
        app->tool[i].module = radiogeddon_module_load(
            app->storage, t->file, (size_t)t->work_kb * 1024u, &app->tool[i].api, &status);
        if(!app->tool[i].module) {
            radiogeddon_module_error(status, &app->message_header, &app->message_text);
            return NULL;
        }
        app->tool[i].file = t->file;
        app->tool[i].shown = true;
        ((const RadioGeddonToolModule*)app->tool[i].api)->set_host(&radiogeddon_host_table);
        radiogeddon_memdiag_sample(t->file);
        return app->tool[i].api;
    }
    // Both slots hold a module still running code: never more than two.
    app->message_header = "Busy";
    app->message_text = "Close this screen\nand try again.";
    return NULL;
}

/* The loaded module of @p t, without loading it. */
static RG_UI_TEXT const RadioGeddonToolModule*
    tools_loaded(RadioGeddonApp* app, const ToolScene* t) {
    for(size_t i = 0; i < COUNT_OF(app->tool); i++)
        if(app->tool[i].module && app->tool[i].file == t->file) return app->tool[i].api;
    return NULL;
}

RG_UI_TEXT void radiogeddon_scene_tools_tick(RadioGeddonApp* app) {
    for(size_t i = 0; i < COUNT_OF(app->tool); i++)
        if(app->tool[i].module && !app->tool[i].shown) tools_unload_slot(app, i);
}

RG_UI_TEXT bool radiogeddon_scene_tools_settled(RadioGeddonApp* app) {
    for(size_t i = 0; i < COUNT_OF(app->tool); i++)
        if(app->tool[i].module && !app->tool[i].shown) return false;
    return true;
}

RG_UI_TEXT void radiogeddon_scene_tools_release(RadioGeddonApp* app) {
    for(size_t i = 0; i < COUNT_OF(app->tool); i++)
        if(app->tool[i].module) tools_unload_slot(app, i);
}

/* ---- the forwarders -------------------------------------------------------- */

static RG_UI_TEXT void tools_enter(void* context, uint32_t scene) {
    RadioGeddonApp* app = context;
    const ToolScene* t = tools_find(scene);
    const RadioGeddonToolModule* api = t ? tools_get(app, t) : NULL;
    if(api) {
        api->on_enter[t->index](context);
    } else {
        radiogeddon_ui_event(app, RgMascotEventOperationFailed, 0);
        // Leave from the event loop, then say why.
        view_dispatcher_send_custom_event(app->view_dispatcher, TOOLS_FAILED_EVENT);
    }
}

static RG_UI_TEXT bool tools_event(void* context, uint32_t scene, SceneManagerEvent event) {
    RadioGeddonApp* app = context;
    const ToolScene* t = tools_find(scene);
    const RadioGeddonToolModule* api = t ? tools_loaded(app, t) : NULL;
    if(api) return api->on_event[t->index](context, event);
    if(event.type == SceneManagerEventTypeCustom && event.event == TOOLS_FAILED_EVENT) {
        const char* header = app->message_header;
        const char* text = app->message_text;
        scene_manager_previous_scene(app->scene_manager);
        radiogeddon_scene_show_message(app, header, text);
        return true;
    }
    return false;
}

static RG_UI_TEXT void tools_exit(void* context, uint32_t scene) {
    RadioGeddonApp* app = context;
    const ToolScene* t = tools_find(scene);
    const RadioGeddonToolModule* api = t ? tools_loaded(app, t) : NULL;
    if(!api) return;
    api->on_exit[t->index](context);
    // Not shown any more: unloaded on the next tick (its code may still be
    // on the stack now).
    for(size_t i = 0; i < COUNT_OF(app->tool); i++)
        if(app->tool[i].file == t->file) app->tool[i].shown = false;
}

#define TOOL_SCENE(name, id)                                                                      \
    RG_UI_TEXT void radiogeddon_scene_##name##_on_enter(void* context) {                          \
        tools_enter(context, RadioGeddonScene##id);                                               \
    }                                                                                             \
    RG_UI_TEXT bool radiogeddon_scene_##name##_on_event(void* context, SceneManagerEvent event) { \
        return tools_event(context, RadioGeddonScene##id, event);                                 \
    }                                                                                             \
    RG_UI_TEXT void radiogeddon_scene_##name##_on_exit(void* context) {                           \
        tools_exit(context, RadioGeddonScene##id);                                                \
    }

#if RG_FEATURE_FINGERPRINT
TOOL_SCENE(similar, Similar)
#endif
#if RG_FEATURE_PULSE_STUDIO
TOOL_SCENE(studio, Studio)
TOOL_SCENE(timeline, Timeline)
#endif
TOOL_SCENE(analyze, Analyze)
TOOL_SCENE(crypto, Crypto)
TOOL_SCENE(compare_result, CompareResult)
TOOL_SCENE(report, Report)
TOOL_SCENE(about, About)
#if RG_FEATURE_ACTIVITY_LOG
TOOL_SCENE(actlog, Actlog)
TOOL_SCENE(adaptive, Adaptive)
TOOL_SCENE(heatmap, Heatmap)
#endif
#if RG_FEATURE_DOLPHIN_LAB
TOOL_SCENE(room, Room)
TOOL_SCENE(customize, Customize)
#endif
#if RG_FEATURE_JOURNAL
TOOL_SCENE(journal, Journal)
#endif
#if RG_FEATURE_ASSISTANT
TOOL_SCENE(assistant, Assistant)
#endif
#if RG_FEATURE_RANGE_SCAN
TOOL_SCENE(range_setup, RangeSetup)
TOOL_SCENE(range_scan, RangeScan)
TOOL_SCENE(profiles, Profiles)
TOOL_SCENE(profile_name, ProfileName)
#endif
TOOL_SCENE(config, Config)
TOOL_SCENE(bands, Bands)
TOOL_SCENE(favorites, Favorites)
TOOL_SCENE(favorite_options, FavoriteOptions)
TOOL_SCENE(frequency, Frequency)
TOOL_SCENE(analyze_menu, AnalyzeMenu)
TOOL_SCENE(multi, Multi)
TOOL_SCENE(multi_result, MultiResult)
TOOL_SCENE(unknown, Unknown)
TOOL_SCENE(decode, Decode)
TOOL_SCENE(hopper, Hopper)
TOOL_SCENE(hopper_stats, HopperStats)
TOOL_SCENE(scanner, Scanner)
TOOL_SCENE(saved_list, SavedList)
TOOL_SCENE(db_options, DbOptions)
TOOL_SCENE(file_details, FileDetails)
TOOL_SCENE(rename, Rename)
#if RG_FEATURE_BITSTREAM
TOOL_SCENE(bitstream, Bitstream)
#endif
#if RG_FEATURE_WATERFALL
TOOL_SCENE(waterfall, Waterfall)

RG_UI_TEXT uint32_t radiogeddon_scene_range_setup_waterfall_item(void) {
    return RADIOGEDDON_RANGE_ITEM_WATERFALL;
}
#endif

#endif
