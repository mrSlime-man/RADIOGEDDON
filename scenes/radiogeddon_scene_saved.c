#include "../radiogeddon.h"
#include <dialogs/dialogs.h>

/*
 * Browse /ext/subghz for a saved .sub recording. The chosen path is stored in
 * app->file_path and we advance to the detail scene. Cancelling returns to the
 * previous scene.
 */
void radiogeddon_scene_saved_on_enter(void* context) {
    RadioGeddon* app = context;

    FuriString* start_path = furi_string_alloc_set(RG_SUBGHZ_FOLDER);

    DialogsFileBrowserOptions options;
    dialog_file_browser_set_basic_options(&options, RG_SUBGHZ_EXTENSION, NULL);
    options.base_path = RG_SUBGHZ_FOLDER;
    options.hide_ext = true;
    options.skip_assets = true;

    bool chosen = dialog_file_browser_show(app->dialogs, app->file_path, start_path, &options);
    furi_string_free(start_path);

    if(chosen) {
        scene_manager_next_scene(app->scene_manager, RadioGeddonSceneSavedInfo);
    } else {
        scene_manager_previous_scene(app->scene_manager);
    }
}

bool radiogeddon_scene_saved_on_event(void* context, SceneManagerEvent event) {
    UNUSED(context);
    UNUSED(event);
    return false;
}

void radiogeddon_scene_saved_on_exit(void* context) {
    UNUSED(context);
}
