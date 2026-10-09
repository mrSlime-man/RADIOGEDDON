#pragma once

#include <gui/scene_manager.h>

/* Scene enum, generated from the scene config. */
typedef enum {
#define ADD_SCENE(prefix, name, id) RadioGeddonScene##id,
#include "radiogeddon_scene_config.h"
#undef ADD_SCENE
    RadioGeddonSceneNum,
} RadioGeddonScene;

extern const SceneManagerHandlers radiogeddon_scene_handlers;

/* Per-scene handler prototypes, generated from the scene config. */
#define ADD_SCENE(prefix, name, id)                                                \
    void prefix##_scene_##name##_on_enter(void* context);                          \
    bool prefix##_scene_##name##_on_event(void* context, SceneManagerEvent event); \
    void prefix##_scene_##name##_on_exit(void* context);
#include "radiogeddon_scene_config.h"
#undef ADD_SCENE
