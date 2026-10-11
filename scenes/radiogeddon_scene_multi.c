#include "radiogeddon_scene.h"
#include "../helpers/rg_multi.h"

#if RG_FEATURE_MULTI_COMPARE

// Full edition: the list of recordings for Multi-Capture Compare. Paths only
// (up to RG_MULTI_MAX_CAPTURES); nothing is read until Compare.

typedef enum {
    MultiIndexAdd = 0,
    MultiIndexCompare,
    MultiIndexClear,
    MultiIndexFile = 100, // + list position: remove it from the list
} MultiIndex;

static void radiogeddon_scene_multi_cb(void* context, uint32_t index) {
    RadioGeddonApp* app = context;
    view_dispatcher_send_custom_event(app->view_dispatcher, index);
}

size_t radiogeddon_scene_multi_memory(size_t count) {
    if(count > RG_MULTI_MAX_CAPTURES) count = RG_MULTI_MAX_CAPTURES;
    // Summaries, the result and one analysis at a time (rg_multi.h).
    return count * sizeof(RgMultiCapture) + sizeof(RgMultiResult) + sizeof(RgAnalyzer);
}

bool radiogeddon_multi_add(RadioGeddonApp* app, const char* path) {
    for(size_t i = 0; i < app->multi_count; i++)
        if(furi_string_cmp_str(app->multi_paths[i], path) == 0) return true; // already in
    if(app->multi_count >= RG_MULTI_MAX_CAPTURES) return false;
    if(!app->multi_paths[app->multi_count])
        app->multi_paths[app->multi_count] = furi_string_alloc();
    furi_string_set_str(app->multi_paths[app->multi_count], path);
    app->multi_count++;
    return true;
}

void radiogeddon_multi_clear(RadioGeddonApp* app) {
    for(size_t i = 0; i < RG_MULTI_MAX_CAPTURES; i++) {
        if(app->multi_paths[i]) furi_string_free(app->multi_paths[i]);
        app->multi_paths[i] = NULL;
    }
    app->multi_count = 0;
}

static void radiogeddon_scene_multi_show(RadioGeddonApp* app) {
    Submenu* submenu = app->submenu;
    submenu_reset(submenu);
    snprintf(
        app->multi_header,
        sizeof(app->multi_header),
        "Compare %u of %u max",
        (unsigned)app->multi_count,
        (unsigned)RG_MULTI_MAX_CAPTURES);
    submenu_set_header(submenu, app->multi_header);
    if(app->multi_count < RG_MULTI_MAX_CAPTURES) {
        submenu_add_item(
            submenu, "Add recording...", MultiIndexAdd, radiogeddon_scene_multi_cb, app);
    }
    if(app->multi_count >= 2) {
        submenu_add_item(
            submenu, "Compare now", MultiIndexCompare, radiogeddon_scene_multi_cb, app);
    }
    for(size_t i = 0; i < app->multi_count; i++) {
        // The item label must stay valid while shown: the path's own name.
        const char* path = furi_string_get_cstr(app->multi_paths[i]);
        const char* slash = strrchr(path, '/');
        submenu_add_item(
            submenu, slash ? slash + 1 : path, MultiIndexFile + i, radiogeddon_scene_multi_cb, app);
    }
    if(app->multi_count) {
        submenu_add_item(submenu, "Clear list", MultiIndexClear, radiogeddon_scene_multi_cb, app);
    }
    submenu_set_selected_item(
        submenu, scene_manager_get_scene_state(app->scene_manager, RadioGeddonSceneMulti));
    view_dispatcher_switch_to_view(app->view_dispatcher, RadioGeddonViewSubmenu);
}

void radiogeddon_scene_multi_on_enter(void* context) {
    RadioGeddonApp* app = context;
    radiogeddon_scene_multi_show(app);
}

static void radiogeddon_scene_multi_browse(RadioGeddonApp* app) {
    DialogsFileBrowserOptions options;
    dialog_file_browser_set_basic_options(&options, RADIOGEDDON_SUB_EXTENSION, NULL);
    options.base_path = RADIOGEDDON_SIGNALS_FOLDER;
    FuriString* sel = furi_string_alloc();
    FuriString* start = furi_string_alloc_set(
        app->multi_count ? furi_string_get_cstr(app->multi_paths[app->multi_count - 1]) :
                           RADIOGEDDON_SIGNALS_FOLDER);
    if(dialog_file_browser_show(app->dialogs, sel, start, &options)) {
        radiogeddon_multi_add(app, furi_string_get_cstr(sel));
    }
    furi_string_free(sel);
    furi_string_free(start);
}

bool radiogeddon_scene_multi_on_event(void* context, SceneManagerEvent event) {
    RadioGeddonApp* app = context;
    if(event.type != SceneManagerEventTypeCustom) return false;
    if(event.event == MultiIndexAdd) {
        radiogeddon_scene_multi_browse(app);
        scene_manager_set_scene_state(app->scene_manager, RadioGeddonSceneMulti, MultiIndexAdd);
        radiogeddon_scene_multi_show(app);
    } else if(event.event == MultiIndexCompare) {
        scene_manager_set_scene_state(
            app->scene_manager, RadioGeddonSceneMulti, MultiIndexCompare);
        scene_manager_next_scene(app->scene_manager, RadioGeddonSceneMultiResult);
    } else if(event.event == MultiIndexClear) {
        radiogeddon_multi_clear(app);
        scene_manager_set_scene_state(app->scene_manager, RadioGeddonSceneMulti, MultiIndexAdd);
        radiogeddon_scene_multi_show(app);
    } else if(
        event.event >= MultiIndexFile &&
        event.event < (uint32_t)MultiIndexFile + app->multi_count) {
        // Remove one file from the list (the recording itself is untouched).
        size_t i = event.event - MultiIndexFile;
        FuriString* gone = app->multi_paths[i];
        for(size_t k = i; k + 1 < app->multi_count; k++)
            app->multi_paths[k] = app->multi_paths[k + 1];
        app->multi_paths[app->multi_count - 1] = gone;
        app->multi_count--;
        scene_manager_set_scene_state(app->scene_manager, RadioGeddonSceneMulti, MultiIndexAdd);
        radiogeddon_scene_multi_show(app);
    } else {
        return false;
    }
    return true;
}

void radiogeddon_scene_multi_on_exit(void* context) {
    RadioGeddonApp* app = context;
    submenu_reset(app->submenu);
}

#endif
