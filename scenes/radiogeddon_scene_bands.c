#include "radiogeddon_scene.h"

#if RG_FEATURE_BAND_INFO

#include <furi_hal_region.h>

// Full edition: the receive bands the radio in use accepts, measured by
// asking its driver, and the transmit bands the firmware's region lists.
// Read-only: receiving is possible anywhere in the receive bands; whether a
// transmission is allowed stays the firmware's decision.

static void radiogeddon_scene_bands_line(FuriString* out, uint32_t lo, uint32_t hi) {
    char a[RG_FREQ_TEXT_SIZE], b[RG_FREQ_TEXT_SIZE];
    rg_freq_text(lo, a, sizeof(a));
    rg_freq_text(hi, b, sizeof(b));
    furi_string_cat_printf(out, " %s - %s\n", a, b);
}

void radiogeddon_scene_bands_on_enter(void* context) {
    RadioGeddonApp* app = context;
    radiogeddon_scene_probe_bands(app);

    FuriString* text = app->temp_str;
    furi_string_reset(text);
    furi_string_cat_printf(
        text, "Radio: %s\nReceive bands (MHz):\n", radiogeddon_subghz_device_name(app->subghz));
    if(app->bands.count == 0) {
        furi_string_cat_str(text, " none (no radio?)\n");
    }
    for(size_t i = 0; i < app->bands.count; i++) {
        radiogeddon_scene_bands_line(text, app->bands.band[i].lo_hz, app->bands.band[i].hi_hz);
    }

    furi_string_cat_printf(text, "\nFirmware region: %s\n", radiogeddon_subghz_region_name());
    const FuriHalRegion* region = furi_hal_region_is_provisioned() ? furi_hal_region_get() : NULL;
    if(region && region->bands_count > 0) {
        furi_string_cat_str(text, "Region TX bands (MHz):\n");
        for(uint16_t i = 0; i < region->bands_count && i < 16; i++) {
            radiogeddon_scene_bands_line(text, region->bands[i].start, region->bands[i].end);
        }
    }
    furi_string_cat_str(
        text,
        "\nGaps between receive\nbands are skipped by\nscans. TX is decided by\nthe firmware's own rules.");

    Widget* widget = app->widget;
    widget_reset(widget);
    widget_add_string_element(widget, 64, 2, AlignCenter, AlignTop, FontPrimary, "Radio bands");
    widget_add_text_scroll_element(widget, 0, 14, 128, 50, furi_string_get_cstr(text));
    view_dispatcher_switch_to_view(app->view_dispatcher, RadioGeddonViewWidget);
}

bool radiogeddon_scene_bands_on_event(void* context, SceneManagerEvent event) {
    UNUSED(context);
    UNUSED(event);
    return false;
}

void radiogeddon_scene_bands_on_exit(void* context) {
    RadioGeddonApp* app = context;
    widget_reset(app->widget);
}

#endif
