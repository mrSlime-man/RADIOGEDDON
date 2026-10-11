#include "radiogeddon_scene.h"

#if RG_FEATURE_SESSIONS

// Full edition: the app's side of the Sessions screens. Their code is a
// module (radiogeddon_sessions.fal) loaded when Sessions opens and kept while
// any Sessions screen is on the stack (also under a recording opened from
// it); it is unloaded with the session data on returning to the main menu.
// These handlers forward to it. When it cannot be loaded (not enough
// memory, a damaged install), the screen closes and says why.

#define SESSIONS_FAILED_EVENT 0x5E55u
/* Kept free for a session, its save buffers and a recording's file menu. */
#define SESSIONS_WORK         (sizeof(RgSession) + RADIOGEDDON_SESSION_SCRATCH + 6u * 1024u)

static const RadioGeddonHost radiogeddon_sessions_host = {
    .show_message = radiogeddon_scene_show_message,
    .show_confirm = radiogeddon_scene_show_confirm,
    .show_busy = radiogeddon_scene_show_busy,
    .show_progress = radiogeddon_scene_show_progress,
    .progress = radiogeddon_scene_progress,
    .progress_end = radiogeddon_scene_progress_end,
    .module_load = radiogeddon_scene_module_load,
    .module_unload = radiogeddon_scene_module_unload,
    .multi_add = radiogeddon_multi_add,
    .multi_clear = radiogeddon_multi_clear,
    .multi_memory = radiogeddon_scene_multi_memory,
};

void radiogeddon_scene_sessions_release(RadioGeddonApp* app) {
    free(app->session_list);
    app->session_list = NULL;
    free(app->session);
    app->session = NULL;
    radiogeddon_module_unload(app->sessions_module);
    app->sessions_module = NULL;
    app->sessions_api = NULL;
}

static const RadioGeddonSessionsModule* radiogeddon_sessions_api(RadioGeddonApp* app) {
    if(!app->sessions_module) {
        RadioGeddonModuleStatus status;
        app->sessions_module = radiogeddon_module_load(
            app->storage, RADIOGEDDON_MODULE_SESSIONS, SESSIONS_WORK, &app->sessions_api, &status);
        if(!app->sessions_module) {
            radiogeddon_module_error(status, &app->message_header, &app->message_text);
            return NULL;
        }
        ((const RadioGeddonSessionsModule*)app->sessions_api)->set_host(&radiogeddon_sessions_host);
        radiogeddon_memdiag_sample("Sessions");
    }
    return app->sessions_api;
}

static void radiogeddon_sessions_enter(void* context, RadioGeddonSessionsScene scene) {
    RadioGeddonApp* app = context;
    const RadioGeddonSessionsModule* api = radiogeddon_sessions_api(app);
    if(api) {
        api->on_enter[scene](context);
    } else {
        view_dispatcher_send_custom_event(app->view_dispatcher, SESSIONS_FAILED_EVENT);
    }
}

static bool radiogeddon_sessions_event(
    void* context,
    SceneManagerEvent event,
    RadioGeddonSessionsScene scene) {
    RadioGeddonApp* app = context;
    if(app->sessions_api) {
        return ((const RadioGeddonSessionsModule*)app->sessions_api)
            ->on_event[scene](context, event);
    }
    if(event.type == SceneManagerEventTypeCustom && event.event == SESSIONS_FAILED_EVENT) {
        const char* header = app->message_header;
        const char* text = app->message_text;
        scene_manager_previous_scene(app->scene_manager);
        radiogeddon_scene_show_message(app, header, text);
        return true;
    }
    return false;
}

static void radiogeddon_sessions_exit(void* context, RadioGeddonSessionsScene scene) {
    RadioGeddonApp* app = context;
    if(app->sessions_api)
        ((const RadioGeddonSessionsModule*)app->sessions_api)->on_exit[scene](context);
}

#define RADIOGEDDON_SESSIONS_FORWARD(name, scene)                                      \
    void radiogeddon_scene_##name##_on_enter(void* context) {                          \
        radiogeddon_sessions_enter(context, scene);                                    \
    }                                                                                  \
    bool radiogeddon_scene_##name##_on_event(void* context, SceneManagerEvent event) { \
        return radiogeddon_sessions_event(context, event, scene);                      \
    }                                                                                  \
    void radiogeddon_scene_##name##_on_exit(void* context) {                           \
        radiogeddon_sessions_exit(context, scene);                                     \
    }

RADIOGEDDON_SESSIONS_FORWARD(sessions, RadioGeddonSessionsSceneList)
RADIOGEDDON_SESSIONS_FORWARD(session_name, RadioGeddonSessionsSceneName)
RADIOGEDDON_SESSIONS_FORWARD(session_menu, RadioGeddonSessionsSceneMenu)
RADIOGEDDON_SESSIONS_FORWARD(session_signals, RadioGeddonSessionsSceneSignals)
RADIOGEDDON_SESSIONS_FORWARD(session_groups, RadioGeddonSessionsSceneGroups)

#endif
