/* Full edition module: recording recovery at startup (see helpers/radiogeddon_recovery.h). */
#include "../helpers/radiogeddon_modules.h"
#include "../helpers/radiogeddon_recovery.h"

#include <flipper_application/flipper_application.h>

static const RadioGeddonRecoverModule radiogeddon_recover_module = {
    .offer = radiogeddon_recovery_offer,
};

static const FlipperAppPluginDescriptor radiogeddon_recover_descriptor = {
    .appid = RADIOGEDDON_MODULE_APPID,
    .ep_api_version = RADIOGEDDON_MODULE_API,
    .entry_point = &radiogeddon_recover_module,
};

const FlipperAppPluginDescriptor* radiogeddon_recover_module_ep(void) {
    return &radiogeddon_recover_descriptor;
}
