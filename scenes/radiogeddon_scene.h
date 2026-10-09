/**
 * @file radiogeddon_scene.h
 * @brief Scene registry generated from radiogeddon_scene_config.h (X-macros).
 */
#pragma once

#include <gui/scene_manager.h>
#include "../radiogeddon.h"

// Scene identifier enum
#define ADD_SCENE(prefix, name, id) RadioGeddonScene##id,
typedef enum {
#include "radiogeddon_scene_config.h"
    RadioGeddonSceneNum,
} RadioGeddonSceneId;
#undef ADD_SCENE

extern const SceneManagerHandlers radiogeddon_scene_handlers;

// Per-scene handler prototypes
#define ADD_SCENE(prefix, name, id)                                            \
    void prefix##_scene_##name##_on_enter(void* context);                      \
    bool prefix##_scene_##name##_on_event(void* context, SceneManagerEvent e); \
    void prefix##_scene_##name##_on_exit(void* context);
#include "radiogeddon_scene_config.h"
#undef ADD_SCENE
