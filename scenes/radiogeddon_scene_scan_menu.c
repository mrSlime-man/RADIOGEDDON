#include "radiogeddon_scene.h"

#if RG_EDITION_FULL

// Full edition: Scan, the tools that listen across frequencies.

typedef enum {
    ScanMenuScanner,
    ScanMenuRange,
    ScanMenuWaterfall,
    ScanMenuHopper,
    ScanMenuFavorites,
} ScanMenuIndex;

static void radiogeddon_scene_scan_menu_cb(void* context, uint32_t index) {
    RadioGeddonApp* app = context;
    view_dispatcher_send_custom_event(app->view_dispatcher, index);
}

void radiogeddon_scene_scan_menu_on_enter(void* context) {
    RadioGeddonApp* app = context;
    Submenu* submenu = app->submenu;
    submenu_reset(submenu);
    submenu_set_header(submenu, "Scan");
    submenu_add_item(
        submenu, "Frequency Scanner", ScanMenuScanner, radiogeddon_scene_scan_menu_cb, app);
    submenu_add_item(submenu, "Range Scanner", ScanMenuRange, radiogeddon_scene_scan_menu_cb, app);
    submenu_add_item(submenu, "Waterfall", ScanMenuWaterfall, radiogeddon_scene_scan_menu_cb, app);
    submenu_add_item(
        submenu, "Frequency Hopper", ScanMenuHopper, radiogeddon_scene_scan_menu_cb, app);
    submenu_add_item(submenu, "Favorites", ScanMenuFavorites, radiogeddon_scene_scan_menu_cb, app);
    submenu_set_selected_item(
        submenu, scene_manager_get_scene_state(app->scene_manager, RadioGeddonSceneScanMenu));
    view_dispatcher_switch_to_view(app->view_dispatcher, RadioGeddonViewSubmenu);
}

bool radiogeddon_scene_scan_menu_on_event(void* context, SceneManagerEvent event) {
    RadioGeddonApp* app = context;
    if(event.type != SceneManagerEventTypeCustom || event.event > ScanMenuFavorites) return false;
    scene_manager_set_scene_state(app->scene_manager, RadioGeddonSceneScanMenu, event.event);
    switch(event.event) {
    case ScanMenuScanner:
        scene_manager_next_scene(app->scene_manager, RadioGeddonSceneScanner);
        break;
    case ScanMenuRange:
        scene_manager_set_scene_state(app->scene_manager, RadioGeddonSceneRangeSetup, 0);
        scene_manager_next_scene(app->scene_manager, RadioGeddonSceneRangeSetup);
        break;
    case ScanMenuWaterfall:
        // The Waterfall sweeps the Range Scanner's range: its setup opens on
        // "Start waterfall".
        scene_manager_set_scene_state(
            app->scene_manager,
            RadioGeddonSceneRangeSetup,
            radiogeddon_scene_range_setup_waterfall_item());
        scene_manager_next_scene(app->scene_manager, RadioGeddonSceneRangeSetup);
        break;
    case ScanMenuHopper:
        scene_manager_next_scene(app->scene_manager, RadioGeddonSceneHopper);
        break;
    default:
        scene_manager_set_scene_state(app->scene_manager, RadioGeddonSceneFavorites, 0);
        scene_manager_next_scene(app->scene_manager, RadioGeddonSceneFavorites);
        break;
    }
    return true;
}

void radiogeddon_scene_scan_menu_on_exit(void* context) {
    RadioGeddonApp* app = context;
    submenu_reset(app->submenu);
}

#endif
