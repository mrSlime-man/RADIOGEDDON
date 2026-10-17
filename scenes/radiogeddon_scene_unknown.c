#include "radiogeddon_scene.h"

/*
 * Unknown Protocol Analysis (Smart Protocol Discovery in the Full edition):
 * streams the whole RAW capture through the signal
 * engine (helpers/rg_analyzer.h) and shows measured timing as [OBSERVED] and
 * the inferred encoding, bit patterns, frame comparison and field map as
 * [HYPOTHESIS]. No decode is claimed and no key is recovered.
 */
RG_UI_TEXT void radiogeddon_scene_unknown_on_enter(void* context) {
    RadioGeddonApp* app = context;

    // The engine reads the file up to three times; show progress meanwhile.
    radiogeddon_scene_show_progress(app, "Analyzing...");

    furi_string_reset(app->temp_str);
    furi_string_cat_printf(
        app->temp_str,
        "%s: %s\n----------------\n",
        RG_EDITION_FULL ? "Protocol discovery" : "Unknown analysis",
        furi_string_get_cstr(app->loaded.name));
    // Full edition: the analysis is a module, loaded for this run only.
    RadioGeddonUnknownFn run = radiogeddon_scene_unknown_begin(app);
    if(run) {
        int frames = run(app->storage, furi_string_get_cstr(app->file_path), app->temp_str);
#if RG_FEATURE_JOURNAL
        if(frames >= 0) {
            // A real analysis finished: the journal and the next steps know.
            radiogeddon_research_record(
                app, RgJrnDiscovery, 1, furi_string_get_cstr(app->loaded.name));
            radiogeddon_research_step(
                app,
                furi_string_get_cstr(app->file_path),
                RadioGeddonStepAnalyzed,
                (uint32_t)frames);
        }
#else
        UNUSED(frames);
#endif
    } else {
        furi_string_cat_printf(app->temp_str, "%s.\n%s\n", app->message_header, app->message_text);
    }
    radiogeddon_scene_unknown_end(app);
    radiogeddon_scene_progress_end(app);

    text_box_reset(app->text_box);
    text_box_set_font(app->text_box, TextBoxFontText);
    text_box_set_text(app->text_box, furi_string_get_cstr(app->temp_str));
    radiogeddon_scene_show_report(
        app,
        RG_EDITION_FULL ? "Protocol discovery" : "Unknown protocol",
        furi_string_get_cstr(app->temp_str));
}

RG_UI_TEXT bool radiogeddon_scene_unknown_on_event(void* context, SceneManagerEvent event) {
    UNUSED(context);
    UNUSED(event);
    return false;
}

RG_UI_TEXT void radiogeddon_scene_unknown_on_exit(void* context) {
    RadioGeddonApp* app = context;
    radiogeddon_ui_cancel(app);
    text_box_reset(app->text_box);
}
