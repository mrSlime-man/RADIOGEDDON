/* Full edition module: tool screens (see helpers/radiogeddon_modules.h). */
#include "../scenes/radiogeddon_scene.h"

#include <flipper_application/flipper_application.h>

const RadioGeddonHost* radiogeddon_host;

static void radiogeddon_analyze_set_host(const RadioGeddonHost* host) {
    radiogeddon_host = host;
}

static const RadioGeddonToolModule radiogeddon_analyze_module = {
    .set_host = radiogeddon_analyze_set_host,
    .on_enter =
        {
            radiogeddon_scene_analyze_menu_on_enter,
            radiogeddon_scene_multi_on_enter,
            radiogeddon_scene_multi_result_on_enter,
            radiogeddon_scene_unknown_on_enter,
            radiogeddon_scene_decode_on_enter,
        },
    .on_event =
        {
            radiogeddon_scene_analyze_menu_on_event,
            radiogeddon_scene_multi_on_event,
            radiogeddon_scene_multi_result_on_event,
            radiogeddon_scene_unknown_on_event,
            radiogeddon_scene_decode_on_event,
        },
    .on_exit =
        {
            radiogeddon_scene_analyze_menu_on_exit,
            radiogeddon_scene_multi_on_exit,
            radiogeddon_scene_multi_result_on_exit,
            radiogeddon_scene_unknown_on_exit,
            radiogeddon_scene_decode_on_exit,
        },
};

static const FlipperAppPluginDescriptor radiogeddon_analyze_descriptor = {
    .appid = RADIOGEDDON_MODULE_APPID,
    .ep_api_version = RADIOGEDDON_MODULE_API,
    .entry_point = &radiogeddon_analyze_module,
};

const FlipperAppPluginDescriptor* radiogeddon_analyze_module_ep(void) {
    return &radiogeddon_analyze_descriptor;
}
