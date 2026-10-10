#include "radiogeddon_scene.h"
#include "../radiogeddon_version.h"

void radiogeddon_scene_about_on_enter(void* context) {
    RadioGeddonApp* app = context;
    Widget* widget = app->widget;
    widget_reset(widget);

    widget_add_string_element(
        widget, 64, 2, AlignCenter, AlignTop, FontPrimary, "RadioGeddon " RG_EDITION_NAME);

    furi_string_reset(app->temp_str);
    furi_string_cat_printf(
        app->temp_str,
        "Version: " RADIOGEDDON_VERSION "\n"
        "Edition: " RG_EDITION_NAME "\n"
        "Radio: %s CC1101\n(%s)\n"
        "Region: %s\n\n",
        radiogeddon_subghz_get_radio(app->subghz) == RadioGeddonRadioExternal ? "external" :
                                                                                "internal",
        radiogeddon_subghz_device_name(app->subghz),
        radiogeddon_subghz_region_name());
    // Measured this run if a session ran, else as kept from an earlier run.
    uint32_t cost = radiogeddon_subghz_session_cost(app->subghz);
    radiogeddon_memdiag_report(app->temp_str, cost ? cost : radiogeddon_scene_radio_cost(app));
    furi_string_cat_printf(
        app->temp_str,
        "\nStandalone Sub-GHz\nanalysis toolkit.\n\n"
        "Modules:\n"
        "- Scanner (live RSSI)\n"
#if RG_FEATURE_RANGE_SCAN
        "- Range scanner + profiles\n"
#endif
#if RG_FEATURE_FAVORITES
        "- Favorite frequencies\n"
#endif
        "- Frequency hopper\n"
        "- Receive & decode\n"
        "- RAW recorder\n"
        "- Signal analyzer\n"
        "- Unknown protocol\n  analysis\n"
#if RG_FEATURE_CHECKSUM_HINTS
        "- Checksum structure\n  hypotheses\n"
#endif
        "- Pulse timeline\n"
        "- Crypto characteristics\n"
        "- Comparator\n"
        "- SD-card database\n"
        "- Authorized replay\n\n"
        "Labels: [CONFIRMED] =\ndecoder matched.\n[OBSERVED] = measured\nfrom the timing.\n"
        "[HEURISTIC] = guess from\nsignal statistics.\n"
        "[HYPOTHESIS] = engine\ninference, unverified.\n\n"
#if RG_FEATURE_REGION_TX_GATE
        "No key recovery is\nperformed. TX only where\nthe firmware's region\nallows it.\n"
#else
        "No key recovery is\nperformed. TX follows the\nfirmware's own rules.\n"
#endif
        "Use only on devices you\nare authorized to test.\n\n"
        "Signals stored under:\n/ext/apps_data/\n  radiogeddon/signals\n\n"
        "github.com/mrSlime-man/\n  RADIOGEDDON\n");

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
