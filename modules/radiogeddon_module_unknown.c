/* Full edition module: Unknown Protocol Analysis (see helpers/radiogeddon_modules.h). */
#include "../helpers/radiogeddon_modules.h"

#include <flipper_application/flipper_application.h>

static const RadioGeddonUnknownModule radiogeddon_unknown_module = {
    .unknown = radiogeddon_analysis_unknown,
};

static const FlipperAppPluginDescriptor radiogeddon_unknown_descriptor = {
    .appid = RADIOGEDDON_MODULE_APPID,
    .ep_api_version = RADIOGEDDON_MODULE_API,
    .entry_point = &radiogeddon_unknown_module,
};

const FlipperAppPluginDescriptor* radiogeddon_unknown_module_ep(void) {
    return &radiogeddon_unknown_descriptor;
}
