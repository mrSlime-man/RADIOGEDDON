/**
 * @file radiogeddon_host.h
 * @brief Full edition: what a screen module may call back into the app.
 *
 * A module links against the firmware's API only. The Sessions module's
 * scenes still need a few of the app's own helpers (messages, progress, the
 * Multi-Compare list, loading another module); the app hands it this table
 * when loading it. In a module build (RADIOGEDDON_MODULE defined),
 * radiogeddon_host_map.h maps the helpers' names onto the table, so the
 * scene sources read the same as the app's.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct RadioGeddonApp RadioGeddonApp;

typedef struct {
    void (*show_message)(RadioGeddonApp* app, const char* header, const char* text);
    void (*show_confirm)(
        RadioGeddonApp* app,
        const char* header,
        const char* text,
        const char* yes_label,
        uint32_t event_yes,
        uint32_t event_no);
    void (*show_busy)(RadioGeddonApp* app, const char* text);
    void (*show_progress)(RadioGeddonApp* app, const char* text);
    void (*progress)(void* context, uint32_t done, uint32_t total);
    void (*progress_end)(RadioGeddonApp* app);
    bool (*module_load)(RadioGeddonApp* app, const char* file, size_t extra_heap);
    void (*module_unload)(RadioGeddonApp* app);
    bool (*multi_add)(RadioGeddonApp* app, const char* path);
    void (*multi_clear)(RadioGeddonApp* app);
    size_t (*multi_memory)(size_t count);
} RadioGeddonHost;
