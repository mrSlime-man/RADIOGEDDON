#include "radiogeddon_scene.h"

void radiogeddon_scene_analyze_on_enter(void* context) {
    RadioGeddonApp* app = context;

    furi_string_reset(app->temp_str);
    radiogeddon_analysis_describe(&app->loaded, app->temp_str);
    furi_string_cat_str(app->temp_str, "----------------\n");
    radiogeddon_analysis_analyze(
        app->storage, furi_string_get_cstr(app->file_path), &app->loaded, app->temp_str);
#if RG_FEATURE_JOURNAL
    radiogeddon_research_record(app, RgJrnAnalysis, 1, furi_string_get_cstr(app->loaded.name));
#endif

    text_box_reset(app->text_box);
    text_box_set_font(app->text_box, TextBoxFontText);
    text_box_set_text(app->text_box, furi_string_get_cstr(app->temp_str));
    radiogeddon_scene_show_report(app, "Signal analysis", furi_string_get_cstr(app->temp_str));
}

bool radiogeddon_scene_analyze_on_event(void* context, SceneManagerEvent event) {
    UNUSED(context);
    UNUSED(event);
    return false;
}

void radiogeddon_scene_analyze_on_exit(void* context) {
    RadioGeddonApp* app = context;
    radiogeddon_ui_cancel(app);
    text_box_reset(app->text_box);
}
