#include "radiogeddon_scene.h"
#include "../radiogeddon_version.h"

#include <toolbox/version.h>

/*
 * About: version, radio, memory figures and the runtime diagnostics
 * (radiogeddon_memdiag.h). "Save report" writes the version, firmware and
 * diagnostics part to apps_data/radiogeddon/diagnostics/ as a text file to
 * attach to a bug report: counters and memory figures only, never a file
 * name, a frequency or a sample of a capture.
 */

#define ABOUT_DIAG_FOLDER RADIOGEDDON_APP_FOLDER "/diagnostics"

typedef enum {
    AboutEventSave = 1500,
} AboutEvent;

/* The report's facts: what a bug report needs, nothing of the captures. */
static RG_UI_TEXT void about_facts(RadioGeddonApp* app, FuriString* out) {
    const Version* v = version_get();
    furi_string_cat_printf(
        out,
        "Version: " RADIOGEDDON_VERSION "\nEdition: " RG_EDITION_NAME "\n"
        "Firmware: %s\n (%s, %s)\nTarget: f%u\nRadio: %s CC1101\n(%s)\nRegion: %s\n"
        "Battery saver: %s\n\n",
        version_get_version(v),
        version_get_githash(v),
        version_get_gitbranch(v),
        (unsigned)version_get_target(v),
        radiogeddon_subghz_get_radio(app->subghz) == RadioGeddonRadioExternal ? "external" :
                                                                                "internal",
        radiogeddon_subghz_device_name(app->subghz),
        radiogeddon_subghz_region_name(),
        app->settings.battery_saver ? "on" : "off");
    uint32_t cost = radiogeddon_subghz_session_cost(app->subghz);
    radiogeddon_memdiag_report(out, cost ? cost : radiogeddon_scene_radio_cost(app));
    radiogeddon_memdiag_report_diag(out);
}

static RG_UI_TEXT void about_save_cb(GuiButtonType type, InputType input, void* context) {
    RadioGeddonApp* app = context;
    if(type == GuiButtonTypeCenter && input == InputTypeShort)
        view_dispatcher_send_custom_event(app->view_dispatcher, AboutEventSave);
}

/* Write the report; false (with the reason in the popup) if the card refused. */
static RG_UI_TEXT bool about_save(RadioGeddonApp* app, FuriString* path) {
    storage_common_mkdir(app->storage, ABOUT_DIAG_FOLDER);
    FuriString* name = furi_string_alloc();
    radiogeddon_storage_default_name(name);
    furi_string_replace_at(name, 0, 2, "RG_DIAG");
    bool ok = radiogeddon_storage_make_unique_path_in(
        app->storage, path, ABOUT_DIAG_FOLDER, furi_string_get_cstr(name), ".txt");
    furi_string_free(name);
    if(!ok) return false;
    furi_string_reset(app->temp_str);
    furi_string_cat_str(app->temp_str, "RadioGeddon diagnostics report\n\n");
    about_facts(app, app->temp_str);
    File* file = storage_file_alloc(app->storage);
    size_t len = furi_string_size(app->temp_str);
    ok = storage_file_open(file, furi_string_get_cstr(path), FSAM_WRITE, FSOM_CREATE_NEW) &&
         storage_file_write(file, furi_string_get_cstr(app->temp_str), len) == len;
    ok = storage_file_close(file) && ok;
    storage_file_free(file);
    if(!ok) storage_common_remove(app->storage, furi_string_get_cstr(path));
    return ok;
}

RG_UI_TEXT void radiogeddon_scene_about_on_enter(void* context) {
    RadioGeddonApp* app = context;
    Widget* widget = app->widget;
    widget_reset(widget);

    widget_add_string_element(
        widget, 64, 2, AlignCenter, AlignTop, FontPrimary, "RadioGeddon " RG_EDITION_NAME);

    furi_string_reset(app->temp_str);
    about_facts(app, app->temp_str);
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
        "- Authorized replay\n"
#if RG_FEATURE_DOLPHIN_LAB
        "- Dolphin Lab interface\n  and lab room\n"
#endif
        "\n"
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

    widget_add_text_scroll_element(widget, 0, 16, 128, 36, furi_string_get_cstr(app->temp_str));
    widget_add_button_element(widget, GuiButtonTypeCenter, "Save report", about_save_cb, app);

    view_dispatcher_switch_to_view(app->view_dispatcher, RadioGeddonViewWidget);
}

RG_UI_TEXT bool radiogeddon_scene_about_on_event(void* context, SceneManagerEvent event) {
    RadioGeddonApp* app = context;
    if(event.type != SceneManagerEventTypeCustom || event.event != AboutEventSave) return false;
    FuriString* path = furi_string_alloc();
    bool ok = about_save(app, path);
    const char* slash = strrchr(furi_string_get_cstr(path), '/');
    // The message scene shows static strings: keep the name in the app.
    snprintf(app->tx_text, sizeof(app->tx_text), "%s", ok && slash ? slash + 1 : "");
    furi_string_free(path);
    radiogeddon_scene_show_message(
        app,
        ok ? "Report saved" : "Report not saved",
        ok ? app->tx_text : "The SD card refused\nthe file.");
    return true;
}

RG_UI_TEXT void radiogeddon_scene_about_on_exit(void* context) {
    RadioGeddonApp* app = context;
    widget_reset(app->widget);
}
