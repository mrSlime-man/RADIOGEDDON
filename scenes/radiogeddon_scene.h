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

/**
 * Ask before a destructive action: @p header, @p text (copied), and Cancel /
 * @p yes_label buttons that post @p event_no / @p event_yes. Back cancels
 * through the scene's own Back handling.
 */
void radiogeddon_scene_show_confirm(
    RadioGeddonApp* app,
    const char* header,
    const char* text,
    const char* yes_label,
    uint32_t event_yes,
    uint32_t event_no);

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

/** The same check before running the decoders over a saved file. */
bool radiogeddon_scene_decoders_memory_ok(RadioGeddonApp* app);

/** That check without the message: whether the decoders fit now. */
bool radiogeddon_scene_decoders_fit(RadioGeddonApp* app);

/** After a receive session: keep its measured cost for the next check. */
void radiogeddon_scene_radio_memory_learn(RadioGeddonApp* app);

#if RG_EDITION_FULL
/** The favorites, read from the card the first time they are needed. */
RadioGeddonFavorites* radiogeddon_scene_favorites(RadioGeddonApp* app);

/** Measure the bands the radio in use accepts into app->bands. */
void radiogeddon_scene_probe_bands(RadioGeddonApp* app);

/** Plan the range in the settings over app->bands into app->range. */
RgRangeResult radiogeddon_scene_plan_range(RadioGeddonApp* app);
#endif

#if RG_FEATURE_WATERFALL
/** Smallest Waterfall history for @p points points (one screen of rows). */
size_t radiogeddon_scene_waterfall_min_bytes(uint32_t points);
/** The Waterfall's heap besides its history (engine and screen). */
size_t radiogeddon_scene_waterfall_fixed_bytes(uint32_t points);
#endif

/**
 * Fill @p out with the frequencies a Scanner or Hopper source gives (the
 * built-in list under @p mask, or the favorites in the Full edition), keeping
 * only those the radio in use can tune. Returns how many, at most @p max.
 */
size_t radiogeddon_scene_build_list(
    RadioGeddonApp* app,
    uint8_t source,
    uint32_t mask,
    uint32_t* out,
    size_t max);
