#include "radiogeddon_scene.h"

#if RG_EDITION_FULL

// Full edition: Analyze, the tools that work on saved recordings. The
// recording in context (the last saved or opened) comes first, so the
// workflow continues without picking it again; every tool below asks for a
// file of the signals folder and opens on it. Back returns here. "Open
// recording" shows a file's whole menu, as the Database does.

typedef enum {
    AnalyzeMenuOpen,
    AnalyzeMenuBitstream,
    AnalyzeMenuMulti,
    AnalyzeMenuUnknown,
    AnalyzeMenuDecode,
    AnalyzeMenuTimeline,
    AnalyzeMenuStudio,
    AnalyzeMenuSimilar,
    AnalyzeMenuJournal,
    AnalyzeMenuCurrent, // the recording in context
} AnalyzeMenuIndex;

static RG_UI_TEXT void radiogeddon_scene_analyze_menu_cb(void* context, uint32_t index) {
    RadioGeddonApp* app = context;
    view_dispatcher_send_custom_event(app->view_dispatcher, index);
}

RG_UI_TEXT void radiogeddon_scene_analyze_menu_on_enter(void* context) {
    RadioGeddonApp* app = context;
    Submenu* submenu = app->submenu;
    submenu_reset(submenu);
    submenu_set_header(submenu, "Analyze recordings");
#if RG_FEATURE_JOURNAL
    FuriString* ctx = furi_string_alloc();
    if(radiogeddon_research_context_path(app, ctx)) {
        char line[80];
        snprintf(line, sizeof(line), "Current: %s", app->research.ctx_name);
        submenu_add_item(
            submenu, line, AnalyzeMenuCurrent, radiogeddon_scene_analyze_menu_cb, app);
    }
    furi_string_free(ctx);
#endif
    submenu_add_item(
        submenu, "Open recording...", AnalyzeMenuOpen, radiogeddon_scene_analyze_menu_cb, app);
#if RG_FEATURE_PULSE_STUDIO
    submenu_add_item(
        submenu, "Pulse Studio...", AnalyzeMenuStudio, radiogeddon_scene_analyze_menu_cb, app);
#endif
#if RG_FEATURE_FINGERPRINT
    submenu_add_item(
        submenu, "Find Similar...", AnalyzeMenuSimilar, radiogeddon_scene_analyze_menu_cb, app);
#endif
    submenu_add_item(
        submenu,
        "Bitstream Explorer...",
        AnalyzeMenuBitstream,
        radiogeddon_scene_analyze_menu_cb,
        app);
    submenu_add_item(
        submenu, "Multi-Capture Compare", AnalyzeMenuMulti, radiogeddon_scene_analyze_menu_cb, app);
    submenu_add_item(
        submenu,
        "Protocol Discovery...",
        AnalyzeMenuUnknown,
        radiogeddon_scene_analyze_menu_cb,
        app);
    submenu_add_item(
        submenu,
        "Decode with Firmware...",
        AnalyzeMenuDecode,
        radiogeddon_scene_analyze_menu_cb,
        app);
    submenu_add_item(
        submenu, "Pulse Timeline...", AnalyzeMenuTimeline, radiogeddon_scene_analyze_menu_cb, app);
#if RG_FEATURE_JOURNAL
    submenu_add_item(
        submenu, "Research Journal", AnalyzeMenuJournal, radiogeddon_scene_analyze_menu_cb, app);
#endif
    submenu_set_selected_item(
        submenu, scene_manager_get_scene_state(app->scene_manager, RadioGeddonSceneAnalyzeMenu));
    view_dispatcher_switch_to_view(app->view_dispatcher, RadioGeddonViewSubmenu);
}

typedef enum {
    AnalyzePickOk,
    AnalyzePickCancelled,
    AnalyzePickFailed, // a message is shown
} AnalyzePick;

/* Ask for a recording (or take the one in context when @p current) and read
 * its metadata into app->loaded. */
static RG_UI_TEXT AnalyzePick
    radiogeddon_scene_analyze_menu_pick(RadioGeddonApp* app, bool current) {
#if RG_FEATURE_JOURNAL
    if(current) {
        if(!radiogeddon_research_context_path(app, app->file_path)) return AnalyzePickCancelled;
        goto load;
    }
#else
    UNUSED(current);
#endif
    DialogsFileBrowserOptions options;
    dialog_file_browser_set_basic_options(&options, RADIOGEDDON_SUB_EXTENSION, NULL);
    options.base_path = RADIOGEDDON_SIGNALS_FOLDER;
    FuriString* start = furi_string_alloc_set(
        furi_string_size(app->file_path) ? furi_string_get_cstr(app->file_path) :
                                           RADIOGEDDON_SIGNALS_FOLDER);
    bool chosen = dialog_file_browser_show(app->dialogs, app->file_path, start, &options);
    furi_string_free(start);
    if(!chosen) return AnalyzePickCancelled;
#if RG_FEATURE_JOURNAL
load:
#endif
    radiogeddon_scene_show_busy(app, "Opening...");
    radiogeddon_loaded_signal_reset(&app->loaded);
    radiogeddon_loaded_signal_init(&app->loaded);
    bool loaded =
        radiogeddon_storage_load(app->storage, furi_string_get_cstr(app->file_path), &app->loaded);
    app->file_damaged = !loaded;
    app->have_loaded_signal = loaded;
    if(!loaded) {
        radiogeddon_scene_show_message(app, "Cannot open", "Not a readable\n.sub file.");
        return AnalyzePickFailed;
    }
#if RG_FEATURE_JOURNAL
    // The recording picked is now the one in context.
    radiogeddon_research_set_context(
        app, furi_string_get_cstr(app->file_path), app->loaded.kind == RadioGeddonSignalKindRaw);
#endif
    return AnalyzePickOk;
}

RG_UI_TEXT bool radiogeddon_scene_analyze_menu_on_event(void* context, SceneManagerEvent event) {
    RadioGeddonApp* app = context;
    if(event.type != SceneManagerEventTypeCustom || event.event > AnalyzeMenuCurrent) return false;
    scene_manager_set_scene_state(app->scene_manager, RadioGeddonSceneAnalyzeMenu, event.event);
    if(event.event == AnalyzeMenuMulti) {
        scene_manager_set_scene_state(app->scene_manager, RadioGeddonSceneMulti, 0);
        scene_manager_next_scene(app->scene_manager, RadioGeddonSceneMulti);
        return true;
    }
#if RG_FEATURE_JOURNAL
    if(event.event == AnalyzeMenuJournal) {
        scene_manager_next_scene(app->scene_manager, RadioGeddonSceneJournal);
        return true;
    }
#endif
    bool current = event.event == AnalyzeMenuCurrent;
    AnalyzePick pick = radiogeddon_scene_analyze_menu_pick(app, current);
    if(pick == AnalyzePickCancelled) radiogeddon_scene_analyze_menu_on_enter(app);
    if(pick != AnalyzePickOk) return true;
    bool raw = app->loaded.kind == RadioGeddonSignalKindRaw;
    switch(event.event) {
    case AnalyzeMenuOpen:
    case AnalyzeMenuCurrent:
        scene_manager_set_scene_state(app->scene_manager, RadioGeddonSceneSavedInfo, 0);
        scene_manager_next_scene(app->scene_manager, RadioGeddonSceneSavedInfo);
        break;
#if RG_FEATURE_FINGERPRINT
    case AnalyzeMenuSimilar:
        scene_manager_set_scene_state(app->scene_manager, RadioGeddonSceneSimilar, 0);
        scene_manager_next_scene(app->scene_manager, RadioGeddonSceneSimilar);
        break;
#endif
    case AnalyzeMenuUnknown:
        scene_manager_next_scene(app->scene_manager, RadioGeddonSceneUnknown);
        break;
    default:
        if(!raw) {
            radiogeddon_scene_show_message(
                app,
                "Needs a RAW capture",
                "This tool reads RAW\ntiming. Record one in\nReceive & Record.");
        } else if(event.event == AnalyzeMenuBitstream) {
            scene_manager_next_scene(app->scene_manager, RadioGeddonSceneBitstream);
        } else if(event.event == AnalyzeMenuDecode) {
            scene_manager_next_scene(app->scene_manager, RadioGeddonSceneDecode);
#if RG_FEATURE_PULSE_STUDIO
        } else if(event.event == AnalyzeMenuStudio) {
            scene_manager_next_scene(app->scene_manager, RadioGeddonSceneStudio);
#endif
        } else {
            scene_manager_next_scene(app->scene_manager, RadioGeddonSceneTimeline);
        }
        break;
    }
    return true;
}

RG_UI_TEXT void radiogeddon_scene_analyze_menu_on_exit(void* context) {
    RadioGeddonApp* app = context;
    submenu_reset(app->submenu);
}

#endif
