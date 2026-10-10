#include "radiogeddon_scene.h"

// Read-only report of the hopper's per-frequency statistics and recent
// activity. The text lives in its own string while the screen is open.

static FuriString* hopper_stats_text = NULL;

void radiogeddon_scene_hopper_stats_on_enter(void* context) {
    RadioGeddonApp* app = context;
    hopper_stats_text = furi_string_alloc();
    if(app->hopper) {
        radiogeddon_hopper_report(app->hopper, hopper_stats_text);
    } else {
        furi_string_set(hopper_stats_text, "No hopper session yet.");
    }
    text_box_reset(app->text_box);
    text_box_set_font(app->text_box, TextBoxFontText);
    text_box_set_text(app->text_box, furi_string_get_cstr(hopper_stats_text));
    view_dispatcher_switch_to_view(app->view_dispatcher, RadioGeddonViewTextBox);
}

bool radiogeddon_scene_hopper_stats_on_event(void* context, SceneManagerEvent event) {
    UNUSED(context);
    UNUSED(event);
    return false;
}

void radiogeddon_scene_hopper_stats_on_exit(void* context) {
    RadioGeddonApp* app = context;
    text_box_reset(app->text_box);
    if(hopper_stats_text) {
        furi_string_free(hopper_stats_text);
        hopper_stats_text = NULL;
    }
}
