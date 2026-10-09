#include "radiogeddon_scene.h"

typedef enum {
    SavedInfoIndexAnalyze,
    SavedInfoIndexCrypto,
    SavedInfoIndexCompare,
    SavedInfoIndexReplay,
    SavedInfoIndexDelete,
} SavedInfoIndex;

static void radiogeddon_scene_saved_info_cb(void* context, uint32_t index) {
    RadioGeddonApp* app = context;
    view_dispatcher_send_custom_event(app->view_dispatcher, index);
}

void radiogeddon_scene_saved_info_on_enter(void* context) {
    RadioGeddonApp* app = context;
    Submenu* submenu = app->submenu;
    submenu_reset(submenu);
    submenu_set_header(submenu, furi_string_get_cstr(app->loaded.name));

    submenu_add_item(
        submenu,
        "Signal Info & Analysis",
        SavedInfoIndexAnalyze,
        radiogeddon_scene_saved_info_cb,
        app);
    submenu_add_item(
        submenu, "Crypto Analysis", SavedInfoIndexCrypto, radiogeddon_scene_saved_info_cb, app);
    submenu_add_item(
        submenu, "Compare with...", SavedInfoIndexCompare, radiogeddon_scene_saved_info_cb, app);
    submenu_add_item(
        submenu, "Replay (TX)", SavedInfoIndexReplay, radiogeddon_scene_saved_info_cb, app);
    submenu_add_item(
        submenu, "Delete", SavedInfoIndexDelete, radiogeddon_scene_saved_info_cb, app);

    submenu_set_selected_item(
        submenu, scene_manager_get_scene_state(app->scene_manager, RadioGeddonSceneSavedInfo));
    view_dispatcher_switch_to_view(app->view_dispatcher, RadioGeddonViewSubmenu);
}

bool radiogeddon_scene_saved_info_on_event(void* context, SceneManagerEvent event) {
    RadioGeddonApp* app = context;
    bool consumed = false;

    if(event.type == SceneManagerEventTypeCustom) {
        scene_manager_set_scene_state(app->scene_manager, RadioGeddonSceneSavedInfo, event.event);
        switch(event.event) {
        case SavedInfoIndexAnalyze:
            scene_manager_next_scene(app->scene_manager, RadioGeddonSceneAnalyze);
            consumed = true;
            break;
        case SavedInfoIndexCrypto:
            scene_manager_next_scene(app->scene_manager, RadioGeddonSceneCrypto);
            consumed = true;
            break;
        case SavedInfoIndexCompare: {
            // Pick a second file to compare against.
            DialogsFileBrowserOptions options;
            dialog_file_browser_set_basic_options(&options, RADIOGEDDON_SUB_EXTENSION, NULL);
            options.base_path = RADIOGEDDON_SIGNALS_FOLDER;
            FuriString* sel = furi_string_alloc();
            FuriString* start = furi_string_alloc_set(RADIOGEDDON_SIGNALS_FOLDER);
            bool chosen = dialog_file_browser_show(app->dialogs, sel, start, &options);
            if(chosen) {
                radiogeddon_loaded_signal_reset(&app->loaded_b);
                radiogeddon_loaded_signal_init(&app->loaded_b);
                if(radiogeddon_storage_load(
                       app->storage, furi_string_get_cstr(sel), &app->loaded_b)) {
                    furi_string_set(app->file_path_b, sel);
                    scene_manager_next_scene(app->scene_manager, RadioGeddonSceneCompareResult);
                } else {
                    notification_message(app->notifications, &sequence_error);
                }
            }
            furi_string_free(sel);
            furi_string_free(start);
            consumed = true;
            break;
        }
        case SavedInfoIndexReplay:
            scene_manager_next_scene(app->scene_manager, RadioGeddonSceneReplay);
            consumed = true;
            break;
        case SavedInfoIndexDelete:
            storage_simply_remove(app->storage, furi_string_get_cstr(app->file_path));
            notification_message(app->notifications, &sequence_success);
            app->have_loaded_signal = false;
            scene_manager_previous_scene(app->scene_manager);
            consumed = true;
            break;
        default:
            break;
        }
    }
    return consumed;
}

void radiogeddon_scene_saved_info_on_exit(void* context) {
    RadioGeddonApp* app = context;
    submenu_reset(app->submenu);
}
