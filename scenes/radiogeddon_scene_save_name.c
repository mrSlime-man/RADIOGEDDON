#include "radiogeddon_scene.h"

static void radiogeddon_scene_save_name_cb(void* context) {
    RadioGeddonApp* app = context;
    view_dispatcher_send_custom_event(app->view_dispatcher, 0);
}

void radiogeddon_scene_save_name_on_enter(void* context) {
    RadioGeddonApp* app = context;
    TextInput* text_input = app->text_input;

    // Seed with a timestamped default name.
    FuriString* def = furi_string_alloc();
    radiogeddon_storage_default_name(def);
    strncpy(app->text_store, furi_string_get_cstr(def), sizeof(app->text_store) - 1);
    app->text_store[sizeof(app->text_store) - 1] = '\0';
    furi_string_free(def);

    text_input_reset(text_input);
    text_input_set_header_text(
        text_input, app->save_is_raw ? "Name RAW capture" : "Name signal");
    text_input_set_result_callback(
        text_input,
        radiogeddon_scene_save_name_cb,
        app,
        app->text_store,
        sizeof(app->text_store),
        false);
    // Reject an empty file name.
    text_input_set_minimum_length(text_input, 1);

    view_dispatcher_switch_to_view(app->view_dispatcher, RadioGeddonViewTextInput);
}

bool radiogeddon_scene_save_name_on_event(void* context, SceneManagerEvent event) {
    RadioGeddonApp* app = context;
    bool consumed = false;

    if(event.type == SceneManagerEventTypeCustom) {
        FuriString* path = furi_string_alloc();
        radiogeddon_storage_make_path(path, app->text_store);

        bool ok;
        if(app->save_is_raw) {
            ok = radiogeddon_subghz_record_flush_to_file(
                app->subghz, furi_string_get_cstr(path));
        } else {
            ok = radiogeddon_storage_write_serialized(
                app->storage, furi_string_get_cstr(path), app->temp_str);
        }

        if(ok) {
            furi_string_set(app->file_path, path);
            notification_message(app->notifications, &sequence_success);
        } else {
            notification_message(app->notifications, &sequence_error);
        }
        furi_string_free(path);

        // Return to whatever launched the save (the receiver).
        scene_manager_previous_scene(app->scene_manager);
        consumed = true;
    }
    return consumed;
}

void radiogeddon_scene_save_name_on_exit(void* context) {
    RadioGeddonApp* app = context;
    text_input_reset(app->text_input);
}
