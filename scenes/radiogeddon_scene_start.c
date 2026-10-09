#include "../radiogeddon.h"

typedef enum {
    RadioGeddonMenuReceiver,
    RadioGeddonMenuRecord,
    RadioGeddonMenuHopper,
    RadioGeddonMenuSaved,
    RadioGeddonMenuCompare,
    RadioGeddonMenuReplay,
    RadioGeddonMenuSettings,
    RadioGeddonMenuAbout,
} RadioGeddonMenuIndex;

static void radiogeddon_scene_start_submenu_callback(void* context, uint32_t index) {
    RadioGeddon* app = context;
    view_dispatcher_send_custom_event(app->view_dispatcher, index);
}

void radiogeddon_scene_start_on_enter(void* context) {
    RadioGeddon* app = context;
    Submenu* submenu = app->submenu;

    submenu_reset(submenu);
    submenu_set_header(submenu, "RadioGeddon");
    submenu_add_item(
        submenu,
        "Receiver",
        RadioGeddonMenuReceiver,
        radiogeddon_scene_start_submenu_callback,
        app);
    submenu_add_item(
        submenu,
        "RAW Record",
        RadioGeddonMenuRecord,
        radiogeddon_scene_start_submenu_callback,
        app);
    submenu_add_item(
        submenu,
        "Frequency Hopper",
        RadioGeddonMenuHopper,
        radiogeddon_scene_start_submenu_callback,
        app);
    submenu_add_item(
        submenu,
        "Saved Signals",
        RadioGeddonMenuSaved,
        radiogeddon_scene_start_submenu_callback,
        app);
    submenu_add_item(
        submenu,
        "Compare Signals",
        RadioGeddonMenuCompare,
        radiogeddon_scene_start_submenu_callback,
        app);
    submenu_add_item(
        submenu,
        "Replay Signal",
        RadioGeddonMenuReplay,
        radiogeddon_scene_start_submenu_callback,
        app);
    submenu_add_item(
        submenu,
        "Settings",
        RadioGeddonMenuSettings,
        radiogeddon_scene_start_submenu_callback,
        app);
    submenu_add_item(
        submenu, "About", RadioGeddonMenuAbout, radiogeddon_scene_start_submenu_callback, app);

    submenu_set_selected_item(
        submenu, scene_manager_get_scene_state(app->scene_manager, RadioGeddonSceneStart));

    view_dispatcher_switch_to_view(app->view_dispatcher, RadioGeddonViewSubmenu);
}

bool radiogeddon_scene_start_on_event(void* context, SceneManagerEvent event) {
    RadioGeddon* app = context;
    bool consumed = false;

    if(event.type == SceneManagerEventTypeCustom) {
        scene_manager_set_scene_state(app->scene_manager, RadioGeddonSceneStart, event.event);
        consumed = true;
        switch(event.event) {
        case RadioGeddonMenuReceiver:
            scene_manager_next_scene(app->scene_manager, RadioGeddonSceneReceiver);
            break;
        case RadioGeddonMenuRecord:
            scene_manager_next_scene(app->scene_manager, RadioGeddonSceneRecord);
            break;
        case RadioGeddonMenuHopper:
            scene_manager_next_scene(app->scene_manager, RadioGeddonSceneHopper);
            break;
        case RadioGeddonMenuSaved:
            scene_manager_next_scene(app->scene_manager, RadioGeddonSceneSaved);
            break;
        case RadioGeddonMenuCompare:
            /* Fresh comparison: let the Compare scene prompt for both files. */
            furi_string_reset(app->compare_a);
            furi_string_reset(app->compare_b);
            scene_manager_next_scene(app->scene_manager, RadioGeddonSceneCompare);
            break;
        case RadioGeddonMenuReplay:
            /* Entered from the menu -> force a file picker (don't reuse the
             * last selection, which Saved -> Replay relies on). */
            furi_string_reset(app->file_path);
            scene_manager_next_scene(app->scene_manager, RadioGeddonSceneReplay);
            break;
        case RadioGeddonMenuSettings:
            scene_manager_next_scene(app->scene_manager, RadioGeddonSceneSettings);
            break;
        case RadioGeddonMenuAbout:
            scene_manager_next_scene(app->scene_manager, RadioGeddonSceneAbout);
            break;
        default:
            consumed = false;
            break;
        }
    }
    return consumed;
}

void radiogeddon_scene_start_on_exit(void* context) {
    RadioGeddon* app = context;
    submenu_reset(app->submenu);
}
