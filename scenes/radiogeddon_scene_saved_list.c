#include "radiogeddon_scene.h"

void radiogeddon_scene_saved_list_on_enter(void* context) {
    RadioGeddonApp* app = context;

    DialogsFileBrowserOptions options;
    dialog_file_browser_set_basic_options(
        &options, RADIOGEDDON_SUB_EXTENSION, NULL);
    options.base_path = RADIOGEDDON_SIGNALS_FOLDER;
    options.hide_ext = false;

    FuriString* selected = furi_string_alloc();
    FuriString* start = furi_string_alloc_set(RADIOGEDDON_SIGNALS_FOLDER);

    bool chosen = dialog_file_browser_show(app->dialogs, selected, start, &options);

    if(chosen) {
        radiogeddon_loaded_signal_reset(&app->loaded);
        radiogeddon_loaded_signal_init(&app->loaded);
        bool ok = radiogeddon_storage_load(
            app->storage, furi_string_get_cstr(selected), &app->loaded);
        if(ok) {
            furi_string_set(app->file_path, selected);
            app->have_loaded_signal = true;
            scene_manager_next_scene(app->scene_manager, RadioGeddonSceneSavedInfo);
        } else {
            notification_message(app->notifications, &sequence_error);
            scene_manager_previous_scene(app->scene_manager);
        }
    } else {
        scene_manager_previous_scene(app->scene_manager);
    }

    furi_string_free(selected);
    furi_string_free(start);
}

bool radiogeddon_scene_saved_list_on_event(void* context, SceneManagerEvent event) {
    UNUSED(context);
    UNUSED(event);
    return false;
}

void radiogeddon_scene_saved_list_on_exit(void* context) {
    UNUSED(context);
}
