#include "radiogeddon_scene.h"

#define ADD_SCENE(prefix, name, id) prefix##_scene_##name##_on_enter,
static void (*const radiogeddon_scene_on_enter_handlers[])(void*) = {
#include "radiogeddon_scene_config.h"
};
#undef ADD_SCENE

#define ADD_SCENE(prefix, name, id) prefix##_scene_##name##_on_event,
static bool (*const radiogeddon_scene_on_event_handlers[])(
    void* context,
    SceneManagerEvent event) = {
#include "radiogeddon_scene_config.h"
};
#undef ADD_SCENE

#define ADD_SCENE(prefix, name, id) prefix##_scene_##name##_on_exit,
static void (*const radiogeddon_scene_on_exit_handlers[])(void* context) = {
#include "radiogeddon_scene_config.h"
};
#undef ADD_SCENE

const SceneManagerHandlers radiogeddon_scene_handlers = {
    .on_enter_handlers = radiogeddon_scene_on_enter_handlers,
    .on_event_handlers = radiogeddon_scene_on_event_handlers,
    .on_exit_handlers = radiogeddon_scene_on_exit_handlers,
    .scene_num = RadioGeddonSceneNum,
};
