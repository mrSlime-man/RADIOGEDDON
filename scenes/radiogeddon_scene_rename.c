#include "radiogeddon_scene.h"

// Rename the open Database file. The keyboard refuses names the SD card
// cannot store and names already in use, so a rename never replaces a file.

#define RENAME_RESULT_MS 1500u

typedef enum {
    RenameEventEntered = 600,
    RenameEventDone,
} RenameEvent;

/* The entered name without a typed ".sub"; false if it is not usable. */
static bool radiogeddon_scene_rename_target(
    RadioGeddonApp* app,
    const char* text,
    FuriString* path,
    FuriString* error) {
    char name[RG_DB_NAME_MAX + 1];
    strlcpy(name, text, sizeof(name));
    rg_db_strip_ext(name, RADIOGEDDON_SUB_EXTENSION);
    RgDbNameError check = rg_db_check_name(name);
    if(check != RgDbNameOk) {
        if(error) furi_string_set(error, rg_db_name_error_text(check));
        return false;
    }
    radiogeddon_storage_make_path(path, name);
    // Unchanged is fine (nothing to do); anything else must be free.
    if(furi_string_equal(path, app->file_path)) return true;
    if(storage_common_exists(app->storage, furi_string_get_cstr(path))) {
        if(error) furi_string_set(error, "Name already used");
        return false;
    }
    return true;
}

static bool
    radiogeddon_scene_rename_validator(const char* text, FuriString* error, void* context) {
    RadioGeddonApp* app = context;
    FuriString* path = furi_string_alloc();
    bool ok = radiogeddon_scene_rename_target(app, text, path, error);
    furi_string_free(path);
    return ok;
}

static void radiogeddon_scene_rename_entered(void* context) {
    RadioGeddonApp* app = context;
    view_dispatcher_send_custom_event(app->view_dispatcher, RenameEventEntered);
}

static void radiogeddon_scene_rename_popup_cb(void* context) {
    RadioGeddonApp* app = context;
    view_dispatcher_send_custom_event(app->view_dispatcher, RenameEventDone);
}

static void radiogeddon_scene_rename_result(RadioGeddonApp* app, bool ok, const char* text) {
    if(ok) {
        notification_message(app->notifications, &sequence_success);
    } else {
        notification_message(app->notifications, &sequence_error);
    }
    popup_reset(app->popup);
    popup_set_header(
        app->popup, ok ? "Renamed" : "Rename failed", 64, 14, AlignCenter, AlignCenter);
    popup_set_text(app->popup, text, 64, 38, AlignCenter, AlignCenter);
    popup_set_context(app->popup, app);
    popup_set_callback(app->popup, radiogeddon_scene_rename_popup_cb);
    popup_set_timeout(app->popup, RENAME_RESULT_MS);
    popup_enable_timeout(app->popup);
    view_dispatcher_switch_to_view(app->view_dispatcher, RadioGeddonViewPopup);
}

void radiogeddon_scene_rename_on_enter(void* context) {
    RadioGeddonApp* app = context;
    strlcpy(app->text_store, furi_string_get_cstr(app->loaded.name), RG_DB_NAME_MAX + 1);
    TextInput* text_input = app->text_input;
    text_input_reset(text_input);
    text_input_set_header_text(text_input, "New name");
    text_input_set_result_callback(
        text_input,
        radiogeddon_scene_rename_entered,
        app,
        app->text_store,
        RG_DB_NAME_MAX + 1,
        false);
    text_input_set_validator(text_input, radiogeddon_scene_rename_validator, app);
    text_input_set_minimum_length(text_input, 1);
    view_dispatcher_switch_to_view(app->view_dispatcher, RadioGeddonViewTextInput);
}

static void radiogeddon_scene_rename_apply(RadioGeddonApp* app) {
    FuriString* path = furi_string_alloc();
    FuriString* error = app->temp_str; // stays valid while the popup shows it
    furi_string_reset(error);
    if(!radiogeddon_scene_rename_target(app, app->text_store, path, error)) {
        // Taken since the keyboard checked it.
        radiogeddon_scene_rename_result(app, false, furi_string_get_cstr(error));
    } else if(furi_string_equal(path, app->file_path)) {
        scene_manager_previous_scene(app->scene_manager);
    } else {
        FS_Error err = storage_common_rename(
            app->storage, furi_string_get_cstr(app->file_path), furi_string_get_cstr(path));
        if(err == FSE_OK) {
            furi_string_set(app->file_path, path);
            rg_db_strip_ext(app->text_store, RADIOGEDDON_SUB_EXTENSION);
            furi_string_set(app->loaded.name, app->text_store);
            app->db_dirty = true; // the Database list re-indexes
            radiogeddon_scene_rename_result(app, true, furi_string_get_cstr(app->loaded.name));
        } else {
            radiogeddon_scene_rename_result(app, false, storage_error_get_desc(err));
        }
    }
    furi_string_free(path);
}

bool radiogeddon_scene_rename_on_event(void* context, SceneManagerEvent event) {
    RadioGeddonApp* app = context;
    bool consumed = false;
    if(event.type == SceneManagerEventTypeCustom) {
        if(event.event == RenameEventEntered) {
            radiogeddon_scene_rename_apply(app);
            consumed = true;
        } else if(event.event == RenameEventDone) {
            scene_manager_previous_scene(app->scene_manager);
            consumed = true;
        }
    }
    return consumed;
}

void radiogeddon_scene_rename_on_exit(void* context) {
    RadioGeddonApp* app = context;
    text_input_set_validator(app->text_input, NULL, NULL);
    text_input_reset(app->text_input);
    popup_reset(app->popup);
}
