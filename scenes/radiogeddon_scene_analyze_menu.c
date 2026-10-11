#include "radiogeddon_scene.h"

#if RG_EDITION_FULL

// Full edition: Analyze, the tools that work on saved recordings. Each asks
// for a file of the signals folder and opens the tool on it; Back returns
// here. "Open recording" shows a file's whole menu, as the Database does.

typedef enum {
    AnalyzeMenuOpen,
    AnalyzeMenuBitstream,
    AnalyzeMenuMulti,
    AnalyzeMenuUnknown,
    AnalyzeMenuDecode,
    AnalyzeMenuTimeline,
} AnalyzeMenuIndex;

static void radiogeddon_scene_analyze_menu_cb(void* context, uint32_t index) {
    RadioGeddonApp* app = context;
    view_dispatcher_send_custom_event(app->view_dispatcher, index);
}

void radiogeddon_scene_analyze_menu_on_enter(void* context) {
    RadioGeddonApp* app = context;
    Submenu* submenu = app->submenu;
    submenu_reset(submenu);
    submenu_set_header(submenu, "Analyze recordings");
    submenu_add_item(
        submenu, "Open recording...", AnalyzeMenuOpen, radiogeddon_scene_analyze_menu_cb, app);
    submenu_add_item(
        submenu,
        "Bitstream Explorer...",
        AnalyzeMenuBitstream,
        radiogeddon_scene_analyze_menu_cb,
        app);
    submenu_add_item(
        submenu, "Multi-Capture Compare", AnalyzeMenuMulti, radiogeddon_scene_analyze_menu_cb, app);
    submenu_add_item(
        submenu, "Unknown Analysis...", AnalyzeMenuUnknown, radiogeddon_scene_analyze_menu_cb, app);
    submenu_add_item(
        submenu,
        "Decode with Firmware...",
        AnalyzeMenuDecode,
        radiogeddon_scene_analyze_menu_cb,
        app);
    submenu_add_item(
        submenu, "Pulse Timeline...", AnalyzeMenuTimeline, radiogeddon_scene_analyze_menu_cb, app);
    submenu_set_selected_item(
        submenu, scene_manager_get_scene_state(app->scene_manager, RadioGeddonSceneAnalyzeMenu));
    view_dispatcher_switch_to_view(app->view_dispatcher, RadioGeddonViewSubmenu);
}

typedef enum {
    AnalyzePickOk,
    AnalyzePickCancelled,
    AnalyzePickFailed, // a message is shown
} AnalyzePick;

/* Ask for a recording and read its metadata into app->loaded. */
static AnalyzePick radiogeddon_scene_analyze_menu_pick(RadioGeddonApp* app) {
    DialogsFileBrowserOptions options;
    dialog_file_browser_set_basic_options(&options, RADIOGEDDON_SUB_EXTENSION, NULL);
    options.base_path = RADIOGEDDON_SIGNALS_FOLDER;
    FuriString* start = furi_string_alloc_set(
        furi_string_size(app->file_path) ? furi_string_get_cstr(app->file_path) :
                                           RADIOGEDDON_SIGNALS_FOLDER);
    bool chosen = dialog_file_browser_show(app->dialogs, app->file_path, start, &options);
    furi_string_free(start);
    if(!chosen) return AnalyzePickCancelled;
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
    return AnalyzePickOk;
}

bool radiogeddon_scene_analyze_menu_on_event(void* context, SceneManagerEvent event) {
    RadioGeddonApp* app = context;
    if(event.type != SceneManagerEventTypeCustom || event.event > AnalyzeMenuTimeline)
        return false;
    scene_manager_set_scene_state(app->scene_manager, RadioGeddonSceneAnalyzeMenu, event.event);
    if(event.event == AnalyzeMenuMulti) {
        scene_manager_set_scene_state(app->scene_manager, RadioGeddonSceneMulti, 0);
        scene_manager_next_scene(app->scene_manager, RadioGeddonSceneMulti);
        return true;
    }
    AnalyzePick pick = radiogeddon_scene_analyze_menu_pick(app);
    if(pick == AnalyzePickCancelled) radiogeddon_scene_analyze_menu_on_enter(app);
    if(pick != AnalyzePickOk) return true;
    bool raw = app->loaded.kind == RadioGeddonSignalKindRaw;
    switch(event.event) {
    case AnalyzeMenuOpen:
        scene_manager_set_scene_state(app->scene_manager, RadioGeddonSceneSavedInfo, 0);
        scene_manager_next_scene(app->scene_manager, RadioGeddonSceneSavedInfo);
        break;
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
        } else {
            scene_manager_next_scene(app->scene_manager, RadioGeddonSceneTimeline);
        }
        break;
    }
    return true;
}

void radiogeddon_scene_analyze_menu_on_exit(void* context) {
    RadioGeddonApp* app = context;
    submenu_reset(app->submenu);
}

#endif
