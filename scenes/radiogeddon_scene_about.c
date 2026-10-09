#include "../radiogeddon.h"

void radiogeddon_scene_about_on_enter(void* context) {
    RadioGeddon* app = context;

    FuriString* body = furi_string_alloc();
    furi_string_printf(
        body,
        "RadioGeddon v%s\n\n"
        "Standalone Sub-GHz analysis\ntoolkit for Flipper Zero.\n\n"
        "Scan, capture RAW, decode &\nidentify protocols, compare\nsignals, hop frequencies and\nreplay authorized recordings.\n\n"
        "Recordings are standard .sub\nfiles in /ext/subghz, shared\nwith the stock Sub-GHz app.\n\n"
        "Only transmit signals you are\nlegally authorized to send.\n\n"
        "License: MIT\n"
        "github.com/mrslime-man/\nradiogeddon",
        RG_VERSION_STRING);

    widget_reset(app->widget);
    widget_add_text_scroll_element(app->widget, 0, 0, 128, 64, furi_string_get_cstr(body));
    furi_string_free(body);

    view_dispatcher_switch_to_view(app->view_dispatcher, RadioGeddonViewWidget);
}

bool radiogeddon_scene_about_on_event(void* context, SceneManagerEvent event) {
    UNUSED(context);
    UNUSED(event);
    return false;
}

void radiogeddon_scene_about_on_exit(void* context) {
    RadioGeddon* app = context;
    widget_reset(app->widget);
}
