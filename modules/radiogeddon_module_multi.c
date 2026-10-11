/* Full edition module: Multi-Capture Compare (see helpers/radiogeddon_modules.h). */
#include "../helpers/radiogeddon_multi.h"

#include <flipper_application/flipper_application.h>

static const RadioGeddonMultiModule radiogeddon_multi_module = {
    .memory = radiogeddon_multi_memory,
    .compare = radiogeddon_multi_compare_files,
};

static const FlipperAppPluginDescriptor radiogeddon_multi_descriptor = {
    .appid = RADIOGEDDON_MODULE_APPID,
    .ep_api_version = RADIOGEDDON_MODULE_API,
    .entry_point = &radiogeddon_multi_module,
};

const FlipperAppPluginDescriptor* radiogeddon_multi_module_ep(void) {
    return &radiogeddon_multi_descriptor;
}
