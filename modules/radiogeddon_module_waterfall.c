/* Full edition module: the Waterfall's screen (see helpers/radiogeddon_modules.h). */
#include "../helpers/radiogeddon_modules.h"

#include <flipper_application/flipper_application.h>

static const RadioGeddonWaterfallModule radiogeddon_waterfall_module = {
    .alloc = radiogeddon_waterfall_view_alloc,
    .free = radiogeddon_waterfall_view_free,
    .get_view = radiogeddon_waterfall_view_get_view,
    .set_callback = radiogeddon_waterfall_view_set_callback,
    .update = radiogeddon_waterfall_view_update,
    .set_style = radiogeddon_waterfall_view_set_style,
    .get_style = radiogeddon_waterfall_view_get_style,
    .set_external = radiogeddon_waterfall_view_set_external,
    .get_cursor_hz = radiogeddon_waterfall_view_get_cursor_hz,
    .get_cursor = radiogeddon_waterfall_view_get_cursor,
    .set_cursor = radiogeddon_waterfall_view_set_cursor,
};

static const FlipperAppPluginDescriptor radiogeddon_waterfall_descriptor = {
    .appid = RADIOGEDDON_MODULE_APPID,
    .ep_api_version = RADIOGEDDON_MODULE_API,
    .entry_point = &radiogeddon_waterfall_module,
};

const FlipperAppPluginDescriptor* radiogeddon_waterfall_module_ep(void) {
    return &radiogeddon_waterfall_descriptor;
}
