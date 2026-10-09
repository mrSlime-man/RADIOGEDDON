#include "../radiogeddon.h"

#define RG_TMP_CAPTURE_PATH EXT_PATH("subghz/.rg_capture.sub")

typedef enum {
    RadioGeddonNameEventConfirmed = 200,
    RadioGeddonNameEventPopupDone = 201,
} RadioGeddonNameEvent;

static void radiogeddon_name_input_callback(void* context) {
    RadioGeddon* app = context;
    view_dispatcher_send_custom_event(app->view_dispatcher, RadioGeddonNameEventConfirmed);
}

static void radiogeddon_name_popup_callback(void* context) {
    RadioGeddon* app = context;
    view_dispatcher_send_custom_event(app->view_dispatcher, RadioGeddonNameEventPopupDone);
}

void radiogeddon_scene_record_name_on_enter(void* context) {
    RadioGeddon* app = context;

    strncpy(app->text_store, "signal", RG_TEXT_STORE_SIZE - 1);
    app->text_store[RG_TEXT_STORE_SIZE - 1] = '\0';

    text_input_reset(app->text_input);
    text_input_set_header_text(app->text_input, "Name the recording");
    text_input_set_minimum_length(app->text_input, 1);
    text_input_set_result_callback(
        app->text_input,
        radiogeddon_name_input_callback,
        app,
        app->text_store,
        RG_TEXT_STORE_SIZE,
        true);

    view_dispatcher_switch_to_view(app->view_dispatcher, RadioGeddonViewTextInput);
}

bool radiogeddon_scene_record_name_on_event(void* context, SceneManagerEvent event) {
    RadioGeddon* app = context;
    bool consumed = false;

    if(event.type == SceneManagerEventTypeCustom) {
        if(event.event == RadioGeddonNameEventConfirmed) {
            FuriString* target = furi_string_alloc();
            furi_string_printf(
                target, "%s/%s%s", RG_SUBGHZ_FOLDER, app->text_store, RG_SUBGHZ_EXTENSION);

            /* Don't silently overwrite an existing recording. */
            FS_Error err;
            if(storage_file_exists(app->storage, furi_string_get_cstr(target))) {
                storage_common_remove(app->storage, furi_string_get_cstr(target));
            }
            err = storage_common_rename(
                app->storage, RG_TMP_CAPTURE_PATH, furi_string_get_cstr(target));

            if(err == FSE_OK) {
                furi_string_set(app->file_path, target);
                app->have_last_save = true;
                radiogeddon_notify(app, &sequence_success);

                popup_reset(app->popup);
                popup_set_header(app->popup, "Saved", 64, 10, AlignCenter, AlignTop);
                popup_set_text(app->popup, app->text_store, 64, 32, AlignCenter, AlignCenter);
                popup_set_callback(app->popup, radiogeddon_name_popup_callback);
                popup_set_context(app->popup, app);
                popup_set_timeout(app->popup, 1500);
                popup_enable_timeout(app->popup);
                view_dispatcher_switch_to_view(app->view_dispatcher, RadioGeddonViewPopup);
            } else {
                radiogeddon_notify(app, &sequence_error);
                popup_reset(app->popup);
                popup_set_header(app->popup, "Save failed", 64, 10, AlignCenter, AlignTop);
                popup_set_text(app->popup, "Check SD card", 64, 32, AlignCenter, AlignCenter);
                popup_set_callback(app->popup, radiogeddon_name_popup_callback);
                popup_set_context(app->popup, app);
                popup_set_timeout(app->popup, 1500);
                popup_enable_timeout(app->popup);
                view_dispatcher_switch_to_view(app->view_dispatcher, RadioGeddonViewPopup);
            }
            furi_string_free(target);
            consumed = true;
        } else if(event.event == RadioGeddonNameEventPopupDone) {
            scene_manager_search_and_switch_to_previous_scene(
                app->scene_manager, RadioGeddonSceneStart);
            consumed = true;
        }
    }
    return consumed;
}

void radiogeddon_scene_record_name_on_exit(void* context) {
    RadioGeddon* app = context;
    text_input_reset(app->text_input);
    popup_reset(app->popup);
}
