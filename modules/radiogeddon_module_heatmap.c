/* Full edition module: tool screens (see helpers/radiogeddon_modules.h). */
#include "../scenes/radiogeddon_scene.h"

#include <flipper_application/flipper_application.h>

const RadioGeddonHost* radiogeddon_host;

static void radiogeddon_heatmap_set_host(const RadioGeddonHost* host) {
    radiogeddon_host = host;
}

static const RadioGeddonToolModule radiogeddon_heatmap_module = {
    .set_host = radiogeddon_heatmap_set_host,
    .on_enter = {radiogeddon_scene_heatmap_on_enter},
    .on_event = {radiogeddon_scene_heatmap_on_event},
    .on_exit = {radiogeddon_scene_heatmap_on_exit},
};

static const FlipperAppPluginDescriptor radiogeddon_heatmap_descriptor = {
    .appid = RADIOGEDDON_MODULE_APPID,
    .ep_api_version = RADIOGEDDON_MODULE_API,
    .entry_point = &radiogeddon_heatmap_module,
};

const FlipperAppPluginDescriptor* radiogeddon_heatmap_module_ep(void) {
    return &radiogeddon_heatmap_descriptor;
}
