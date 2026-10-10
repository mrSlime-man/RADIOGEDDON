#include "radiogeddon_scene.h"

// Collect handler function pointers into the three tables.
#define ADD_SCENE(prefix, name, id) prefix##_scene_##name##_on_enter,
void (*const radiogeddon_scene_on_enter_handlers[])(void*) = {
#include "radiogeddon_scene_config.h"
};
#undef ADD_SCENE

#define ADD_SCENE(prefix, name, id) prefix##_scene_##name##_on_event,
bool (*const radiogeddon_scene_on_event_handlers[])(void*, SceneManagerEvent) = {
#include "radiogeddon_scene_config.h"
};
#undef ADD_SCENE

#define ADD_SCENE(prefix, name, id) prefix##_scene_##name##_on_exit,
void (*const radiogeddon_scene_on_exit_handlers[])(void*) = {
#include "radiogeddon_scene_config.h"
};
#undef ADD_SCENE

const SceneManagerHandlers radiogeddon_scene_handlers = {
    .on_enter_handlers = radiogeddon_scene_on_enter_handlers,
    .on_event_handlers = radiogeddon_scene_on_event_handlers,
    .on_exit_handlers = radiogeddon_scene_on_exit_handlers,
    .scene_num = RadioGeddonSceneNum,
};

void radiogeddon_scene_show_busy(RadioGeddonApp* app, const char* text) {
    popup_reset(app->popup);
    popup_set_header(app->popup, text, 64, 32, AlignCenter, AlignCenter);
    view_dispatcher_switch_to_view(app->view_dispatcher, RadioGeddonViewPopup);
}
