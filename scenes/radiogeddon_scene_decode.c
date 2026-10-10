#include "radiogeddon_scene.h"

/*
 * Firmware Decode: runs the firmware's own protocol decoders, the ones Receive
 * uses, over a saved RAW capture and lists what they decode as [CONFIRMED],
 * each with how often and when (helpers/rg_decode.h). The radio is not used:
 * nothing is received or transmitted, and nothing is saved.
 */
void radiogeddon_scene_decode_on_enter(void* context) {
    RadioGeddonApp* app = context;

    // The decoders and keystore are most of a receive session: same check.
    if(!radiogeddon_scene_decoders_memory_ok(app)) return;

    radiogeddon_scene_show_progress(app, "Decoding...");

    furi_string_reset(app->temp_str);
    furi_string_cat_printf(
        app->temp_str,
        "Firmware decode: %s\n----------------\n",
        furi_string_get_cstr(app->loaded.name));
    radiogeddon_analysis_decode(
        app->storage, app->subghz, furi_string_get_cstr(app->file_path), app->temp_str);
    radiogeddon_scene_progress_end(app);

    text_box_reset(app->text_box);
    text_box_set_font(app->text_box, TextBoxFontText);
    text_box_set_text(app->text_box, furi_string_get_cstr(app->temp_str));
    view_dispatcher_switch_to_view(app->view_dispatcher, RadioGeddonViewTextBox);
}

bool radiogeddon_scene_decode_on_event(void* context, SceneManagerEvent event) {
    UNUSED(context);
    UNUSED(event);
    return false;
}

void radiogeddon_scene_decode_on_exit(void* context) {
    RadioGeddonApp* app = context;
    text_box_reset(app->text_box);
}
