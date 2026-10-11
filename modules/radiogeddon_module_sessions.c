/* Full edition module: the Sessions screens (see helpers/radiogeddon_modules.h). */
#include "../scenes/radiogeddon_scene.h"

#include <flipper_application/flipper_application.h>

const RadioGeddonHost* radiogeddon_host;

static void radiogeddon_sessions_set_host(const RadioGeddonHost* host) {
    radiogeddon_host = host;
}

static const RadioGeddonSessionsModule radiogeddon_sessions_module = {
    .set_host = radiogeddon_sessions_set_host,
    .on_enter =
        {
            radiogeddon_scene_sessions_on_enter,
            radiogeddon_scene_session_name_on_enter,
            radiogeddon_scene_session_menu_on_enter,
            radiogeddon_scene_session_signals_on_enter,
            radiogeddon_scene_session_groups_on_enter,
        },
    .on_event =
        {
            radiogeddon_scene_sessions_on_event,
            radiogeddon_scene_session_name_on_event,
            radiogeddon_scene_session_menu_on_event,
            radiogeddon_scene_session_signals_on_event,
            radiogeddon_scene_session_groups_on_event,
        },
    .on_exit =
        {
            radiogeddon_scene_sessions_on_exit,
            radiogeddon_scene_session_name_on_exit,
            radiogeddon_scene_session_menu_on_exit,
            radiogeddon_scene_session_signals_on_exit,
            radiogeddon_scene_session_groups_on_exit,
        },
};

static const FlipperAppPluginDescriptor radiogeddon_sessions_descriptor = {
    .appid = RADIOGEDDON_MODULE_APPID,
    .ep_api_version = RADIOGEDDON_MODULE_API,
    .entry_point = &radiogeddon_sessions_module,
};

const FlipperAppPluginDescriptor* radiogeddon_sessions_module_ep(void) {
    return &radiogeddon_sessions_descriptor;
}
