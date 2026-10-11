#include "radiogeddon_scene.h"

#if RG_FEATURE_SESSIONS

// Full edition: the list of research sessions. The list and the open session
// are kept while the Sessions screens are shown and freed on returning to
// the main menu (radiogeddon_scene_sessions_release).

typedef enum {
    SessionsIndexNew = 0,
    SessionsIndexSuggest,
    SessionsIndexSession = 100, // + list position
} SessionsIndex;

void radiogeddon_scene_sessions_release(RadioGeddonApp* app) {
    free(app->session_list);
    app->session_list = NULL;
    free(app->session);
    app->session = NULL;
}

static void radiogeddon_scene_sessions_cb(void* context, uint32_t index) {
    RadioGeddonApp* app = context;
    view_dispatcher_send_custom_event(app->view_dispatcher, index);
}

void radiogeddon_scene_sessions_on_enter(void* context) {
    RadioGeddonApp* app = context;
    if(!app->session_list) app->session_list = malloc(sizeof(RadioGeddonSessionList));
    radiogeddon_sessions_list(app->storage, app->session_list);
    char active[RG_SESSION_NAME_MAX];
    radiogeddon_session_active(app->storage, active, sizeof(active));

    Submenu* submenu = app->submenu;
    submenu_reset(submenu);
    // More files than the list holds: say so instead of hiding them.
    submenu_set_header(
        submenu, app->session_list->truncated ? "Sessions (first 16)" : "Research sessions");
    submenu_add_item(submenu, "New session", SessionsIndexNew, radiogeddon_scene_sessions_cb, app);
    submenu_add_item(
        submenu, "Suggest groups", SessionsIndexSuggest, radiogeddon_scene_sessions_cb, app);
    char label[RG_SESSION_NAME_MAX + 4];
    for(size_t i = 0; i < app->session_list->count; i++) {
        const char* name = app->session_list->name[i];
        // "*" marks the session new recordings join.
        snprintf(label, sizeof(label), "%s%s", strcmp(name, active) == 0 ? "* " : "", name);
        submenu_add_item(
            submenu, label, SessionsIndexSession + i, radiogeddon_scene_sessions_cb, app);
    }
    submenu_set_selected_item(
        submenu, scene_manager_get_scene_state(app->scene_manager, RadioGeddonSceneSessions));
    view_dispatcher_switch_to_view(app->view_dispatcher, RadioGeddonViewSubmenu);
}

bool radiogeddon_scene_sessions_on_event(void* context, SceneManagerEvent event) {
    RadioGeddonApp* app = context;
    if(event.type != SceneManagerEventTypeCustom) return false;
    if(event.event == SessionsIndexNew) {
        scene_manager_set_scene_state(app->scene_manager, RadioGeddonSceneSessions, event.event);
        app->session_name_mode = RadioGeddonSessionNameNew;
        scene_manager_next_scene(app->scene_manager, RadioGeddonSceneSessionName);
        return true;
    }
    if(event.event == SessionsIndexSuggest) {
        scene_manager_set_scene_state(app->scene_manager, RadioGeddonSceneSessions, event.event);
        scene_manager_next_scene(app->scene_manager, RadioGeddonSceneSessionGroups);
        return true;
    }
    if(app->session_list && event.event >= SessionsIndexSession &&
       event.event < SessionsIndexSession + app->session_list->count) {
        scene_manager_set_scene_state(app->scene_manager, RadioGeddonSceneSessions, event.event);
        strlcpy(
            app->session_name,
            app->session_list->name[event.event - SessionsIndexSession],
            sizeof(app->session_name));
        scene_manager_set_scene_state(app->scene_manager, RadioGeddonSceneSessionMenu, 0);
        scene_manager_next_scene(app->scene_manager, RadioGeddonSceneSessionMenu);
        return true;
    }
    return false;
}

void radiogeddon_scene_sessions_on_exit(void* context) {
    RadioGeddonApp* app = context;
    submenu_reset(app->submenu);
}

#endif
