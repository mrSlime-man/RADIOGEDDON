#include "radiogeddon_scene.h"

// The Catalog edition lists its tools directly. The Full edition groups the
// scanning tools under Scan and the file tools under Analyze, so the main
// menu stays one screen long.
typedef enum {
    StartIndexScanner,
    StartIndexRange, // unused: under Scan in the Full edition
    StartIndexReceiver,
    StartIndexHopper,
    StartIndexFavorites, // unused: under Scan in the Full edition
    StartIndexDatabase,
    StartIndexSettings,
    StartIndexAbout,
    StartIndexScan, // Full edition
    StartIndexAnalyze, // Full edition
    StartIndexSessions, // Full edition
} StartIndex;

static void radiogeddon_scene_start_submenu_callback(void* context, uint32_t index) {
    RadioGeddonApp* app = context;
    view_dispatcher_send_custom_event(app->view_dispatcher, index);
}

void radiogeddon_scene_start_on_enter(void* context) {
    RadioGeddonApp* app = context;
    // Back at the main menu: scanner results and the Database index are no
    // longer needed.
    radiogeddon_scene_db_release(app);
    if(app->scanner) {
        radiogeddon_scanner_free(app->scanner);
        app->scanner = NULL;
    }
    if(app->hopper) {
        radiogeddon_hopper_free(app->hopper);
        app->hopper = NULL;
    }
#if RG_FEATURE_SESSIONS
    radiogeddon_scene_sessions_release(app);
#endif
    Submenu* submenu = app->submenu;
    submenu_reset(submenu);
    submenu_set_header(submenu, RG_EDITION_FULL ? "RadioGeddon Full" : "RadioGeddon");

#if RG_EDITION_FULL
    submenu_add_item(
        submenu, "Scan", StartIndexScan, radiogeddon_scene_start_submenu_callback, app);
    submenu_add_item(
        submenu,
        "Receive & Record",
        StartIndexReceiver,
        radiogeddon_scene_start_submenu_callback,
        app);
    submenu_add_item(
        submenu, "Analyze", StartIndexAnalyze, radiogeddon_scene_start_submenu_callback, app);
    submenu_add_item(
        submenu, "Database", StartIndexDatabase, radiogeddon_scene_start_submenu_callback, app);
    submenu_add_item(
        submenu, "Sessions", StartIndexSessions, radiogeddon_scene_start_submenu_callback, app);
#else
    submenu_add_item(
        submenu, "Scanner", StartIndexScanner, radiogeddon_scene_start_submenu_callback, app);
    submenu_add_item(
        submenu,
        "Receive & Record",
        StartIndexReceiver,
        radiogeddon_scene_start_submenu_callback,
        app);
    submenu_add_item(
        submenu,
        "Frequency Hopper",
        StartIndexHopper,
        radiogeddon_scene_start_submenu_callback,
        app);
    submenu_add_item(
        submenu, "Database", StartIndexDatabase, radiogeddon_scene_start_submenu_callback, app);
#endif
    submenu_add_item(
        submenu, "Settings", StartIndexSettings, radiogeddon_scene_start_submenu_callback, app);
    submenu_add_item(
        submenu, "About", StartIndexAbout, radiogeddon_scene_start_submenu_callback, app);

    submenu_set_selected_item(
        submenu, scene_manager_get_scene_state(app->scene_manager, RadioGeddonSceneStart));

    view_dispatcher_switch_to_view(app->view_dispatcher, RadioGeddonViewSubmenu);
}

bool radiogeddon_scene_start_on_event(void* context, SceneManagerEvent event) {
    RadioGeddonApp* app = context;
    bool consumed = false;

    // Only menu indices are ours; ignore late events from a scene just closed.
    if(event.type == SceneManagerEventTypeCustom && event.event <= StartIndexSessions) {
        scene_manager_set_scene_state(app->scene_manager, RadioGeddonSceneStart, event.event);
        switch(event.event) {
        case StartIndexScanner:
            scene_manager_next_scene(app->scene_manager, RadioGeddonSceneScanner);
            break;
        case StartIndexReceiver:
            scene_manager_next_scene(app->scene_manager, RadioGeddonSceneReceiver);
            break;
        case StartIndexHopper:
            scene_manager_next_scene(app->scene_manager, RadioGeddonSceneHopper);
            break;
        case StartIndexDatabase:
            scene_manager_next_scene(app->scene_manager, RadioGeddonSceneSavedList);
            break;
        case StartIndexSettings:
            scene_manager_set_scene_state(app->scene_manager, RadioGeddonSceneConfig, 0);
            scene_manager_next_scene(app->scene_manager, RadioGeddonSceneConfig);
            break;
        case StartIndexAbout:
            scene_manager_next_scene(app->scene_manager, RadioGeddonSceneAbout);
            break;
#if RG_EDITION_FULL
        case StartIndexScan:
            scene_manager_next_scene(app->scene_manager, RadioGeddonSceneScanMenu);
            break;
        case StartIndexAnalyze:
            scene_manager_next_scene(app->scene_manager, RadioGeddonSceneAnalyzeMenu);
            break;
        case StartIndexSessions:
            scene_manager_set_scene_state(app->scene_manager, RadioGeddonSceneSessions, 0);
            scene_manager_next_scene(app->scene_manager, RadioGeddonSceneSessions);
            break;
#endif
        default:
            break;
        }
        consumed = true;
    }
    return consumed;
}

void radiogeddon_scene_start_on_exit(void* context) {
    RadioGeddonApp* app = context;
    submenu_reset(app->submenu);
}
