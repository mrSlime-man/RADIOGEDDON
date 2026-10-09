#include "radiogeddon_scene.h"

typedef enum {
    SavedListCustomOpen = 400,
    SavedListCustomBack,
} SavedListCustomEvent;

void radiogeddon_scene_saved_list_on_enter(void* context) {
    RadioGeddonApp* app = context;

    DialogsFileBrowserOptions options;
    dialog_file_browser_set_basic_options(&options, RADIOGEDDON_SUB_EXTENSION, NULL);
    options.base_path = RADIOGEDDON_SIGNALS_FOLDER;
    options.hide_ext = false;

    FuriString* selected = furi_string_alloc();
    FuriString* start = furi_string_alloc_set(RADIOGEDDON_SIGNALS_FOLDER);

    bool chosen = dialog_file_browser_show(app->dialogs, selected, start, &options);

    bool opened = false;
    if(chosen) {
        radiogeddon_loaded_signal_reset(&app->loaded);
        radiogeddon_loaded_signal_init(&app->loaded);
        if(radiogeddon_storage_load(app->storage, furi_string_get_cstr(selected), &app->loaded)) {
            furi_string_set(app->file_path, selected);
            app->have_loaded_signal = true;
            opened = true;
        } else {
            notification_message(app->notifications, &sequence_error);
        }
    }

    furi_string_free(selected);
    furi_string_free(start);

    // Defer navigation out of on_enter — the scene manager processes these
    // custom events on the next event-loop iteration, which avoids reentrant
    // next_scene/previous_scene calls from inside a scene's own on_enter.
    view_dispatcher_send_custom_event(
        app->view_dispatcher, opened ? SavedListCustomOpen : SavedListCustomBack);
}

bool radiogeddon_scene_saved_list_on_event(void* context, SceneManagerEvent event) {
    RadioGeddonApp* app = context;
    bool consumed = false;

    if(event.type == SceneManagerEventTypeCustom) {
        if(event.event == SavedListCustomOpen) {
            scene_manager_next_scene(app->scene_manager, RadioGeddonSceneSavedInfo);
            consumed = true;
        } else if(event.event == SavedListCustomBack) {
            scene_manager_previous_scene(app->scene_manager);
            consumed = true;
        }
    }
    return consumed;
}

void radiogeddon_scene_saved_list_on_exit(void* context) {
    UNUSED(context);
}
