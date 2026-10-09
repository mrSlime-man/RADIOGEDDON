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

/**
 * Show a "please wait" popup before blocking work in a scene's on_enter (file
 * analysis). The GUI redraws it while the app thread waits on the SD card.
 */
void radiogeddon_scene_show_busy(RadioGeddonApp* app, const char* text);

/**
 * Busy popup with a percentage under @p text, for work that reports progress:
 * pass radiogeddon_scene_progress() (context: the app) to the operation.
 * Whole-file analyses report to it until radiogeddon_scene_progress_end().
 */
void radiogeddon_scene_show_progress(RadioGeddonApp* app, const char* text);
void radiogeddon_scene_progress(void* context, uint32_t done, uint32_t total);
void radiogeddon_scene_progress_end(RadioGeddonApp* app);

/**
 * Show an error or notice (static strings) on its own screen with the error
 * tone; it closes after a few seconds or on Back, returning to the caller.
 */
void radiogeddon_scene_show_message(RadioGeddonApp* app, const char* header, const char* text);

/** Free the Database index and list view, if they exist. */
void radiogeddon_scene_db_release(RadioGeddonApp* app);

/** Heap a receive session took when last measured on this firmware (0: never). */
uint32_t radiogeddon_scene_radio_cost(RadioGeddonApp* app);

/**
 * Before starting a receive session: true if its measured cost (plus a margin)
 * fits in the free heap, or if it was never measured. Otherwise shows "Not
 * enough memory" with the figures on the popup and returns false.
 */
bool radiogeddon_scene_radio_memory_ok(RadioGeddonApp* app);

/** After a receive session: keep its measured cost for the next check. */
void radiogeddon_scene_radio_memory_learn(RadioGeddonApp* app);
