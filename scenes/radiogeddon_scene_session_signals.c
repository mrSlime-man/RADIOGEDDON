#include "radiogeddon_scene.h"

#if RG_FEATURE_SESSIONS

// Full edition: the recordings of the open session. Scene state 0: OK opens
// a recording (its usual file menu: analysis, explorer, compare, replay);
// 1: OK takes it out of the session (the file itself stays).

#define SESSION_SIGNALS_BUSY_BYTES 32768u

static void radiogeddon_scene_session_signals_cb(void* context, uint32_t index) {
    RadioGeddonApp* app = context;
    view_dispatcher_send_custom_event(app->view_dispatcher, index);
}

static void radiogeddon_scene_session_signals_show(RadioGeddonApp* app, uint32_t selected) {
    bool remove =
        scene_manager_get_scene_state(app->scene_manager, RadioGeddonSceneSessionSignals);
    Submenu* submenu = app->submenu;
    submenu_reset(submenu);
    submenu_set_header(submenu, remove ? "Remove from session" : app->session->name);
    char label[RG_SESSION_SIGNAL_MAX + 4];
    for(size_t i = 0; i < app->session->count; i++) {
        // A recording deleted or renamed elsewhere is marked, not hidden.
        bool here = radiogeddon_session_signal_exists(app->storage, app->session->signal[i]);
        snprintf(label, sizeof(label), "%s%s", here ? "" : "? ", app->session->signal[i]);
        submenu_add_item(submenu, label, i, radiogeddon_scene_session_signals_cb, app);
    }
    if(selected < app->session->count) submenu_set_selected_item(submenu, selected);
    view_dispatcher_switch_to_view(app->view_dispatcher, RadioGeddonViewSubmenu);
}

void radiogeddon_scene_session_signals_on_enter(void* context) {
    RadioGeddonApp* app = context;
    furi_check(app->session);
    radiogeddon_scene_session_signals_show(app, 0);
}

static void radiogeddon_scene_session_signals_open(RadioGeddonApp* app, const char* file) {
    furi_string_printf(app->file_path, "%s/%s", RADIOGEDDON_SIGNALS_FOLDER, file);
    if(!radiogeddon_session_signal_exists(app->storage, file)) {
        radiogeddon_scene_show_message(
            app,
            "Recording missing",
            "It was deleted or\nrenamed outside the\napp. Remove it here.");
        return;
    }
    FileInfo info;
    if(storage_common_stat(app->storage, furi_string_get_cstr(app->file_path), &info) == FSE_OK &&
       info.size > SESSION_SIGNALS_BUSY_BYTES)
        radiogeddon_scene_show_busy(app, "Opening...");
    radiogeddon_loaded_signal_reset(&app->loaded);
    radiogeddon_loaded_signal_init(&app->loaded);
    bool loaded =
        radiogeddon_storage_load(app->storage, furi_string_get_cstr(app->file_path), &app->loaded);
    app->file_damaged = !loaded;
    app->have_loaded_signal = loaded;
    scene_manager_set_scene_state(app->scene_manager, RadioGeddonSceneSavedInfo, 0);
    scene_manager_next_scene(app->scene_manager, RadioGeddonSceneSavedInfo);
}

bool radiogeddon_scene_session_signals_on_event(void* context, SceneManagerEvent event) {
    RadioGeddonApp* app = context;
    if(event.type != SceneManagerEventTypeCustom || !app->session ||
       event.event >= app->session->count)
        return false;
    const char* file = app->session->signal[event.event];
    if(!scene_manager_get_scene_state(app->scene_manager, RadioGeddonSceneSessionSignals)) {
        radiogeddon_scene_session_signals_open(app, file);
        return true;
    }
    char removed[RG_SESSION_SIGNAL_MAX];
    strlcpy(removed, file, sizeof(removed));
    rg_session_remove(app->session, removed);
    if(!radiogeddon_session_save(app->storage, app->session)) {
        rg_session_add(app->session, removed); // the file on the card still has it
        radiogeddon_scene_show_message(app, "Not saved", "Check the SD card.");
        return true;
    }
    notification_message(app->notifications, &sequence_blink_blue_10);
    if(app->session->count == 0) {
        scene_manager_previous_scene(app->scene_manager);
    } else {
        radiogeddon_scene_session_signals_show(app, event.event);
    }
    return true;
}

void radiogeddon_scene_session_signals_on_exit(void* context) {
    RadioGeddonApp* app = context;
    submenu_reset(app->submenu);
}

#endif
