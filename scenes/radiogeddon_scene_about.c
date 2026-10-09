#include "radiogeddon_scene.h"

#define RADIOGEDDON_VERSION_STR "1.0"

void radiogeddon_scene_about_on_enter(void* context) {
    RadioGeddonApp* app = context;
    Widget* widget = app->widget;
    widget_reset(widget);

    widget_add_string_element(
        widget, 64, 2, AlignCenter, AlignTop, FontPrimary, "RadioGeddon " RADIOGEDDON_VERSION_STR);

    furi_string_reset(app->temp_str);
    furi_string_cat_printf(
        app->temp_str,
        "Device: %s\n\n"
        "Standalone Sub-GHz\nanalysis toolkit.\n\n"
        "Modules:\n"
        "- Scanner (live RSSI)\n"
        "- Frequency hopper\n"
        "- Receive & decode\n"
        "- RAW recorder\n"
        "- Signal analyzer\n"
        "- Crypto characteristics\n"
        "- Comparator\n"
        "- SD-card database\n"
        "- Authorized replay\n\n"
        "Labels: [CONFIRMED] =\ndecoder matched.\n[HEURISTIC] = guess from\nsignal statistics.\n\n"
        "No key recovery is\nperformed. Transmission\nrespects regional limits.\n"
        "Use only on devices you\nare authorized to test.\n\n"
        "Signals stored under:\n/ext/apps_data/\n  radiogeddon/signals\n",
        radiogeddon_subghz_device_name(app->subghz));

    widget_add_text_scroll_element(widget, 0, 16, 128, 48, furi_string_get_cstr(app->temp_str));

    view_dispatcher_switch_to_view(app->view_dispatcher, RadioGeddonViewWidget);
}

bool radiogeddon_scene_about_on_event(void* context, SceneManagerEvent event) {
    UNUSED(context);
    UNUSED(event);
    return false;
}

void radiogeddon_scene_about_on_exit(void* context) {
    RadioGeddonApp* app = context;
    widget_reset(app->widget);
}
