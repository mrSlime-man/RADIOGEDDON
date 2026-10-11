/**
 * @file radiogeddon_host_map.h
 * @brief Module builds: the app's helpers by name, through the host table.
 *
 * Included last by scenes/radiogeddon_scene.h, after the helpers are
 * declared; does nothing outside a module build. See radiogeddon_host.h.
 */
#pragma once

#include "radiogeddon_host.h"

#ifdef RADIOGEDDON_MODULE
/* Set by the module's set_host() before any scene of it runs. */
extern const RadioGeddonHost* radiogeddon_host;

#define radiogeddon_scene_show_message  (radiogeddon_host->show_message)
#define radiogeddon_scene_show_confirm  (radiogeddon_host->show_confirm)
#define radiogeddon_scene_show_busy     (radiogeddon_host->show_busy)
#define radiogeddon_scene_show_progress (radiogeddon_host->show_progress)
#define radiogeddon_scene_progress      (radiogeddon_host->progress)
#define radiogeddon_scene_progress_end  (radiogeddon_host->progress_end)
#define radiogeddon_scene_module_load   (radiogeddon_host->module_load)
#define radiogeddon_scene_module_unload (radiogeddon_host->module_unload)
#define radiogeddon_multi_add           (radiogeddon_host->multi_add)
#define radiogeddon_multi_clear         (radiogeddon_host->multi_clear)
#define radiogeddon_scene_multi_memory  (radiogeddon_host->multi_memory)
#endif
