#include "radiogeddon_scene.h"

/*
 * Unknown Protocol Analysis: streams the whole RAW capture through the signal
 * engine (helpers/rg_analyzer.h) and shows measured timing as [OBSERVED] and
 * the inferred encoding, bit patterns, frame comparison and field map as
 * [HYPOTHESIS]. No decode is claimed and no key is recovered.
 */
void radiogeddon_scene_unknown_on_enter(void* context) {
    RadioGeddonApp* app = context;

    // The engine reads the file up to three times; show progress meanwhile.
    radiogeddon_scene_show_progress(app, "Analyzing...");

    furi_string_reset(app->temp_str);
    furi_string_cat_printf(
        app->temp_str,
        "Unknown analysis: %s\n----------------\n",
        furi_string_get_cstr(app->loaded.name));
    radiogeddon_analysis_unknown(
        app->storage, furi_string_get_cstr(app->file_path), app->temp_str);
    radiogeddon_scene_progress_end(app);

    text_box_reset(app->text_box);
    text_box_set_font(app->text_box, TextBoxFontText);
    text_box_set_text(app->text_box, furi_string_get_cstr(app->temp_str));
    view_dispatcher_switch_to_view(app->view_dispatcher, RadioGeddonViewTextBox);
}

bool radiogeddon_scene_unknown_on_event(void* context, SceneManagerEvent event) {
    UNUSED(context);
    UNUSED(event);
    return false;
}

void radiogeddon_scene_unknown_on_exit(void* context) {
    RadioGeddonApp* app = context;
    text_box_reset(app->text_box);
}
