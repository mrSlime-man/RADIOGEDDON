#include "radiogeddon_scene.h"

void radiogeddon_scene_crypto_on_enter(void* context) {
    RadioGeddonApp* app = context;

    furi_string_reset(app->temp_str);
    furi_string_cat_printf(
        app->temp_str, "Crypto: %s\n", furi_string_get_cstr(app->loaded.protocol));
    furi_string_cat_str(app->temp_str, "----------------\n");
    radiogeddon_analysis_crypto(&app->loaded, app->temp_str);
    furi_string_cat_str(
        app->temp_str,
        "----------------\n"
        "This tool identifies\ncrypto characteristics\nonly. It does NOT recover\nkeys or defeat rolling\ncodes.\n");

    text_box_reset(app->text_box);
    text_box_set_font(app->text_box, TextBoxFontText);
    text_box_set_text(app->text_box, furi_string_get_cstr(app->temp_str));
    view_dispatcher_switch_to_view(app->view_dispatcher, RadioGeddonViewTextBox);
}

bool radiogeddon_scene_crypto_on_event(void* context, SceneManagerEvent event) {
    UNUSED(context);
    UNUSED(event);
    return false;
}

void radiogeddon_scene_crypto_on_exit(void* context) {
    RadioGeddonApp* app = context;
    text_box_reset(app->text_box);
}
