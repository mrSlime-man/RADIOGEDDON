/* Full edition module: the Dolphin Lab room's screen (see helpers/radiogeddon_modules.h). */
#include "../helpers/radiogeddon_modules.h"

#include <flipper_application/flipper_application.h>

static const RadioGeddonLabModule radiogeddon_lab_module = {
    .alloc = radiogeddon_room_view_alloc,
    .free = radiogeddon_room_view_free,
    .get_view = radiogeddon_room_view_get_view,
    .set_callback = radiogeddon_room_view_set_callback,
    .load_terminal = radiogeddon_room_load_terminal,
    .load_scope = radiogeddon_room_load_scope,
    .get_station = radiogeddon_room_view_get_station,
    .get_gallery = radiogeddon_room_view_get_gallery,
    .update = radiogeddon_room_view_update,
};

static const FlipperAppPluginDescriptor radiogeddon_lab_descriptor = {
    .appid = RADIOGEDDON_MODULE_APPID,
    .ep_api_version = RADIOGEDDON_MODULE_API,
    .entry_point = &radiogeddon_lab_module,
};

const FlipperAppPluginDescriptor* radiogeddon_lab_module_ep(void) {
    return &radiogeddon_lab_descriptor;
}
