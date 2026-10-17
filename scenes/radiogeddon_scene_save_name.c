#include "radiogeddon_scene.h"

typedef enum {
    SaveNameEventEntered,
    SaveNameEventDone,
} SaveNameEvent;

#define SAVE_NAME_RESULT_MS 2500

static RG_UI_TEXT void radiogeddon_scene_save_name_cb(void* context) {
    RadioGeddonApp* app = context;
    view_dispatcher_send_custom_event(app->view_dispatcher, SaveNameEventEntered);
}

static RG_UI_TEXT void radiogeddon_scene_save_name_popup_cb(void* context) {
    RadioGeddonApp* app = context;
    view_dispatcher_send_custom_event(app->view_dispatcher, SaveNameEventDone);
}

RG_UI_TEXT void radiogeddon_scene_save_name_on_enter(void* context) {
    RadioGeddonApp* app = context;
    TextInput* text_input = app->text_input;

    // Seed with a timestamped default name.
    FuriString* def = furi_string_alloc();
    radiogeddon_storage_default_name(def);
    strncpy(app->text_store, furi_string_get_cstr(def), sizeof(app->text_store) - 1);
    app->text_store[sizeof(app->text_store) - 1] = '\0';
    furi_string_free(def);

    text_input_reset(text_input);
    text_input_set_header_text(text_input, app->save_is_raw ? "Name RAW capture" : "Name signal");
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

// Save under a name that does not exist yet ("name", else "name_2" ...), so
// an existing recording is never replaced.
static RG_UI_TEXT bool radiogeddon_scene_save_name_save(RadioGeddonApp* app, FuriString* path) {
    if(!radiogeddon_storage_make_unique_path(app->storage, path, app->text_store)) return false;
    if(app->save_is_raw) {
        return radiogeddon_subghz_record_save(app->subghz, furi_string_get_cstr(path));
    }
    return radiogeddon_storage_write_serialized(
        app->storage, furi_string_get_cstr(path), app->temp_str);
}

// Result popup: where it went and, for RAW, how much was captured or lost.
static RG_UI_TEXT void
    radiogeddon_scene_save_name_show_result(RadioGeddonApp* app, bool ok, FuriString* path) {
    furi_string_reset(app->temp_str);
    if(ok) {
        const char* p = furi_string_get_cstr(path);
        const char* slash = strrchr(p, '/');
        furi_string_cat_str(app->temp_str, slash ? slash + 1 : p);
#if RG_FEATURE_JOURNAL
        // The new recording is the one in context: tips and tools use it.
        radiogeddon_research_set_context(app, p, app->save_is_raw);
        radiogeddon_research_record(app, RgJrnCapture, 1, slash ? slash + 1 : p);
#endif
#if RG_FEATURE_SESSIONS
        // The active session collects what is saved here.
        char session[RG_SESSION_NAME_MAX];
        if(radiogeddon_session_capture_saved(
               app->storage, slash ? slash + 1 : p, session, sizeof(session))) {
            furi_string_cat_printf(app->temp_str, "\n+ session %s", session);
#if RG_FEATURE_JOURNAL
            radiogeddon_research_session_added(app);
            radiogeddon_research_step(app, p, RadioGeddonStepInSession, 0);
#endif
        }
#endif
        if(app->save_is_raw) {
            RadioGeddonRecordStats st;
            radiogeddon_subghz_record_status(app->subghz, &st);
            furi_string_cat_printf(
                app->temp_str,
                "\n%lu samples, %lu.%lu s",
                (unsigned long)st.samples,
                (unsigned long)(st.elapsed_ms / 1000),
                (unsigned long)(st.elapsed_ms % 1000 / 100));
            if(st.lost > 0) {
                furi_string_cat_printf(
                    app->temp_str, "\n%lu lost (SD too slow)", (unsigned long)st.lost);
            }
        }
    } else {
        furi_string_set(app->temp_str, "Could not write\nto the SD card.");
    }

    radiogeddon_ui_event(app, ok ? RgMascotEventSaveCompleted : RgMascotEventOperationFailed, 0);
#if RG_FEATURE_DOLPHIN_LAB
    // The dolphin celebrates (or worries) beside the same text, then the
    // card returns by itself like the popup.
    if(radiogeddon_ui_card(
           app,
           RadioGeddonLabCardSaved,
           ok ? "Saved" : "Save failed",
           furi_string_get_cstr(app->temp_str),
           SAVE_NAME_RESULT_MS,
           SaveNameEventDone,
           true))
        return;
#endif
    popup_reset(app->popup);
    popup_set_header(app->popup, ok ? "Saved" : "Save failed", 64, 10, AlignCenter, AlignCenter);
    popup_set_text(
        app->popup, furi_string_get_cstr(app->temp_str), 64, 38, AlignCenter, AlignCenter);
    popup_set_context(app->popup, app);
    popup_set_callback(app->popup, radiogeddon_scene_save_name_popup_cb);
    popup_set_timeout(app->popup, SAVE_NAME_RESULT_MS);
    popup_enable_timeout(app->popup);
    view_dispatcher_switch_to_view(app->view_dispatcher, RadioGeddonViewPopup);
}

RG_UI_TEXT bool radiogeddon_scene_save_name_on_event(void* context, SceneManagerEvent event) {
    RadioGeddonApp* app = context;
    bool consumed = false;

    if(event.type == SceneManagerEventTypeCustom && event.event == SaveNameEventEntered) {
        FuriString* path = furi_string_alloc();
        bool ok = radiogeddon_scene_save_name_save(app, path);
        if(ok) {
            furi_string_set(app->file_path, path);
            notification_message(app->notifications, &sequence_success);
        } else {
            notification_message(app->notifications, &sequence_error);
        }
        radiogeddon_scene_save_name_show_result(app, ok, path);
        furi_string_free(path);
        consumed = true;
    } else if(event.type == SceneManagerEventTypeCustom && event.event == SaveNameEventDone) {
        // Return to whatever launched the save (the receiver).
        scene_manager_previous_scene(app->scene_manager);
        consumed = true;
    }
    return consumed;
}

RG_UI_TEXT void radiogeddon_scene_save_name_on_exit(void* context) {
    RadioGeddonApp* app = context;
    radiogeddon_ui_cancel(app);
    text_input_reset(app->text_input);
    popup_reset(app->popup);
    // Leaving without saving (Back, or a failed save) drops the RAW capture.
    if(app->save_is_raw) radiogeddon_subghz_record_discard(app->subghz);
}
