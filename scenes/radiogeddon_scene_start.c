#include "radiogeddon_scene.h"

typedef enum {
    StartIndexScanner,
    StartIndexRange, // Full edition
    StartIndexReceiver,
    StartIndexHopper,
    StartIndexFavorites, // Full edition
    StartIndexDatabase,
    StartIndexSettings,
    StartIndexAbout,
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
    Submenu* submenu = app->submenu;
    submenu_reset(submenu);
    submenu_set_header(submenu, RG_EDITION_FULL ? "RadioGeddon Full" : "RadioGeddon");

    submenu_add_item(
        submenu, "Scanner", StartIndexScanner, radiogeddon_scene_start_submenu_callback, app);
#if RG_FEATURE_RANGE_SCAN
    submenu_add_item(
        submenu, "Range Scanner", StartIndexRange, radiogeddon_scene_start_submenu_callback, app);
#endif
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
#if RG_FEATURE_FAVORITES
    submenu_add_item(
        submenu, "Favorites", StartIndexFavorites, radiogeddon_scene_start_submenu_callback, app);
#endif
    submenu_add_item(
        submenu, "Database", StartIndexDatabase, radiogeddon_scene_start_submenu_callback, app);
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
    if(event.type == SceneManagerEventTypeCustom && event.event <= StartIndexAbout) {
        scene_manager_set_scene_state(app->scene_manager, RadioGeddonSceneStart, event.event);
        switch(event.event) {
        case StartIndexScanner:
            scene_manager_next_scene(app->scene_manager, RadioGeddonSceneScanner);
            break;
        case StartIndexReceiver:
            scene_manager_next_scene(app->scene_manager, RadioGeddonSceneReceiver);
            break;
#if RG_FEATURE_RANGE_SCAN
        case StartIndexRange:
            scene_manager_set_scene_state(app->scene_manager, RadioGeddonSceneRangeSetup, 0);
            scene_manager_next_scene(app->scene_manager, RadioGeddonSceneRangeSetup);
            break;
#endif
#if RG_FEATURE_FAVORITES
        case StartIndexFavorites:
            scene_manager_set_scene_state(app->scene_manager, RadioGeddonSceneFavorites, 0);
            scene_manager_next_scene(app->scene_manager, RadioGeddonSceneFavorites);
            break;
#endif
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
