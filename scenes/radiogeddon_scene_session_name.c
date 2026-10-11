#include "radiogeddon_scene.h"

#if RG_FEATURE_SESSIONS

// Full edition: name a new session, or rename the open one.

typedef enum {
    SessionNameEventEntered = 990,
} SessionNameEvent;

static bool
    radiogeddon_scene_session_name_validator(const char* text, FuriString* error, void* context) {
    RadioGeddonApp* app = context;
    if(!rg_session_name_valid(text)) {
        furi_string_set(error, "No / : * ? and no\nspace at either end");
        return false;
    }
    bool same = app->session_name_mode == RadioGeddonSessionNameRename && app->session &&
                strcmp(app->session->name, text) == 0;
    if(!same && strcasecmp(app->session ? app->session->name : "", text) != 0 &&
       radiogeddon_session_exists(app->storage, text)) {
        furi_string_set(error, "A session with\nthis name exists.");
        return false;
    }
    return true;
}

static void radiogeddon_scene_session_name_cb(void* context) {
    RadioGeddonApp* app = context;
    view_dispatcher_send_custom_event(app->view_dispatcher, SessionNameEventEntered);
}

void radiogeddon_scene_session_name_on_enter(void* context) {
    RadioGeddonApp* app = context;
    bool rename = app->session_name_mode == RadioGeddonSessionNameRename && app->session;
    strlcpy(app->text_store, rename ? app->session->name : "", sizeof(app->text_store));
    TextInput* text_input = app->text_input;
    text_input_reset(text_input);
    text_input_set_header_text(text_input, rename ? "Rename session" : "New session name");
    text_input_set_result_callback(
        text_input,
        radiogeddon_scene_session_name_cb,
        app,
        app->text_store,
        RG_SESSION_NAME_MAX,
        !rename);
    text_input_set_minimum_length(text_input, 1);
    text_input_set_validator(text_input, radiogeddon_scene_session_name_validator, app);
    view_dispatcher_switch_to_view(app->view_dispatcher, RadioGeddonViewTextInput);
}

bool radiogeddon_scene_session_name_on_event(void* context, SceneManagerEvent event) {
    RadioGeddonApp* app = context;
    if(event.type != SceneManagerEventTypeCustom || event.event != SessionNameEventEntered)
        return false;
    if(app->session_name_mode == RadioGeddonSessionNameRename && app->session) {
        if(strcmp(app->session->name, app->text_store) == 0 ||
           radiogeddon_session_rename(app->storage, app->session, app->text_store)) {
            strlcpy(app->session_name, app->session->name, sizeof(app->session_name));
            scene_manager_previous_scene(app->scene_manager);
        } else {
            scene_manager_previous_scene(app->scene_manager);
            radiogeddon_scene_show_message(app, "Rename failed", "Check the SD card.");
        }
        return true;
    }
    // A new, empty session, opened at once (Back from it returns to the list).
    if(!app->session) app->session = malloc(sizeof(RgSession));
    char created[20];
    radiogeddon_session_now(created, sizeof(created));
    rg_session_init(app->session, app->text_store, created);
    if(radiogeddon_session_save(app->storage, app->session)) {
        strlcpy(app->session_name, app->session->name, sizeof(app->session_name));
        scene_manager_set_scene_state(app->scene_manager, RadioGeddonSceneSessionMenu, 0);
        scene_manager_search_and_switch_to_another_scene(
            app->scene_manager, RadioGeddonSceneSessionMenu);
    } else {
        free(app->session);
        app->session = NULL;
        scene_manager_previous_scene(app->scene_manager);
        radiogeddon_scene_show_message(
            app, "Not saved", "Could not write the\nsession. Check the\nSD card.");
    }
    return true;
}

void radiogeddon_scene_session_name_on_exit(void* context) {
    RadioGeddonApp* app = context;
    text_input_set_validator(app->text_input, NULL, NULL);
    text_input_reset(app->text_input);
}

#endif
