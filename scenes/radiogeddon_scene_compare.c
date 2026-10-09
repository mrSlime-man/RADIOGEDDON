#include "../radiogeddon.h"

/*
 * Compare two RAW recordings. If slot A was pre-seeded (from Saved -> Compare)
 * we only prompt for B; otherwise we prompt for both. The similarity report is
 * shown in a scrollable widget.
 */
static bool radiogeddon_compare_pick(RadioGeddon* app, FuriString* out) {
    FuriString* start_path = furi_string_alloc_set(RG_SUBGHZ_FOLDER);
    DialogsFileBrowserOptions options;
    dialog_file_browser_set_basic_options(&options, RG_SUBGHZ_EXTENSION, NULL);
    options.base_path = RG_SUBGHZ_FOLDER;
    options.hide_ext = true;
    options.skip_assets = true;
    bool chosen = dialog_file_browser_show(app->dialogs, out, start_path, &options);
    furi_string_free(start_path);
    return chosen;
}

void radiogeddon_scene_compare_on_enter(void* context) {
    RadioGeddon* app = context;

    /* Pick file A if not pre-seeded. */
    if(furi_string_empty(app->compare_a)) {
        if(!radiogeddon_compare_pick(app, app->compare_a)) {
            furi_string_reset(app->compare_a);
            scene_manager_previous_scene(app->scene_manager);
            return;
        }
    }

    /* Pick file B. */
    if(!radiogeddon_compare_pick(app, app->compare_b)) {
        furi_string_reset(app->compare_a);
        furi_string_reset(app->compare_b);
        scene_manager_previous_scene(app->scene_manager);
        return;
    }

    FuriString* report = furi_string_alloc();
    int score = 0;
    bool ok = rg_storage_compare(
        furi_string_get_cstr(app->compare_a), furi_string_get_cstr(app->compare_b), report, &score);

    if(ok) {
        if(score >= 85) {
            radiogeddon_notify(app, &sequence_success);
        } else {
            radiogeddon_notify(app, &sequence_single_vibro);
        }
    } else {
        radiogeddon_notify(app, &sequence_error);
    }

    widget_reset(app->widget);
    widget_add_text_scroll_element(app->widget, 0, 0, 128, 64, furi_string_get_cstr(report));
    furi_string_free(report);

    view_dispatcher_switch_to_view(app->view_dispatcher, RadioGeddonViewWidget);
}

bool radiogeddon_scene_compare_on_event(void* context, SceneManagerEvent event) {
    UNUSED(context);
    UNUSED(event);
    return false;
}

void radiogeddon_scene_compare_on_exit(void* context) {
    RadioGeddon* app = context;
    widget_reset(app->widget);
    /* Reset slots so the next comparison starts fresh. */
    furi_string_reset(app->compare_a);
    furi_string_reset(app->compare_b);
}
