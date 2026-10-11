/* Full edition module: the Bitstream Explorer's screen (see helpers/radiogeddon_modules.h). */
#include "../helpers/radiogeddon_modules.h"

#include <flipper_application/flipper_application.h>

static const RadioGeddonBitsModule radiogeddon_bits_module = {
    .alloc = radiogeddon_bits_view_alloc,
    .free = radiogeddon_bits_view_free,
    .get_view = radiogeddon_bits_view_get_view,
    .set_analysis = radiogeddon_bits_view_set_analysis,
};

static const FlipperAppPluginDescriptor radiogeddon_bits_descriptor = {
    .appid = RADIOGEDDON_MODULE_APPID,
    .ep_api_version = RADIOGEDDON_MODULE_API,
    .entry_point = &radiogeddon_bits_module,
};

const FlipperAppPluginDescriptor* radiogeddon_bits_module_ep(void) {
    return &radiogeddon_bits_descriptor;
}
