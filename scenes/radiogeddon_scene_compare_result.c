#include "radiogeddon_scene.h"

void radiogeddon_scene_compare_result_on_enter(void* context) {
    RadioGeddonApp* app = context;

    furi_string_reset(app->temp_str);
    furi_string_cat_printf(
        app->temp_str,
        "A: %s\nB: %s\n----------------\n",
        furi_string_get_cstr(app->loaded.name),
        furi_string_get_cstr(app->loaded_b.name));
    radiogeddon_analysis_compare(&app->loaded, &app->loaded_b, app->temp_str);

    text_box_reset(app->text_box);
    text_box_set_font(app->text_box, TextBoxFontText);
    text_box_set_text(app->text_box, furi_string_get_cstr(app->temp_str));
    view_dispatcher_switch_to_view(app->view_dispatcher, RadioGeddonViewTextBox);
}

bool radiogeddon_scene_compare_result_on_event(void* context, SceneManagerEvent event) {
    UNUSED(context);
    UNUSED(event);
    return false;
}

void radiogeddon_scene_compare_result_on_exit(void* context) {
    RadioGeddonApp* app = context;
    text_box_reset(app->text_box);
}
