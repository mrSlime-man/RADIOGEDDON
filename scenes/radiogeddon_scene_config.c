#include "radiogeddon_scene.h"

// Scan-list presets. "Custom" is any other combination, edited per frequency.
typedef struct {
    const char* label;
    uint32_t lo_hz;
    uint32_t hi_hz;
} ConfigBand;

static const ConfigBand config_bands[] = {
    {"All", 0, 0xFFFFFFFFu},
    {"300-348", 300000000, 348000000},
    {"387-464", 387000000, 464000000},
    {"779-928", 779000000, 928000000},
};
#define CONFIG_BAND_COUNT  (sizeof(config_bands) / sizeof(config_bands[0]))
#define CONFIG_BAND_CUSTOM CONFIG_BAND_COUNT

static const uint16_t config_dwell_ms[] = {5, 10, 20, 50, 100};
static const uint16_t config_hop_dwell_ms[] = {100, 200, 300, 500, 1000};
static const uint16_t config_hop_hold_ms[] = {1000, 2000, 3000, 5000, 10000};
static const uint8_t config_threshold_db[] = {6, 8, 10, 15, 20, 30};

#define COUNT_OF_ARRAY(a) (sizeof(a) / sizeof((a)[0]))

typedef enum {
    ConfigItemFrequency,
    ConfigItemModulation,
    ConfigItemThreshold,
    ConfigItemScanList,
    ConfigItemEditScanList,
    ConfigItemDwell,
    ConfigItemHold,
    ConfigItemHopList,
    ConfigItemEditHopList,
    ConfigItemHopDwell,
    ConfigItemHopHold,
    ConfigItemHopRecord,
    ConfigItemRadio,
    ConfigItemExtPower,
    // Full edition only (last, so the indices above are the same in both).
    ConfigItemScanSource,
    ConfigItemHopSource,
    ConfigItemFreqStep,
    ConfigItemBands,
} ConfigItem;

#define ConfigCustomEditScanList 600
#define ConfigCustomEditHopList  601
#define ConfigCustomExtMissing   602
#define ConfigCustomFrequency    603
#define ConfigCustomRebuild      604
#define ConfigCustomBands        605

#if RG_FEATURE_FREQ_STEP
/* Frequency steps for Left/Right on "Frequency MHz"; 0 steps through the list. */
static const uint32_t config_freq_steps_hz[] =
    {0, 1000, 5000, 10000, 12500, 25000, 100000, 1000000};
static const char* const config_freq_step_text[] =
    {"List", "1 kHz", "5 kHz", "10 kHz", "12.5 kHz", "25 kHz", "100 kHz", "1 MHz"};
/* In step mode the item has three values and sits on the middle one: Left
 * and Right move it off the middle, the change is applied, and it goes back. */
#define CONFIG_STEP_MIDDLE 1
#endif

#if RG_FEATURE_FAVORITES
static const char* const config_source_text[RadioGeddonSourceCount] = {"List", "Favorites"};
#endif

static uint32_t radiogeddon_config_band_mask(size_t band) {
    return rg_scan_band_mask(
        radiogeddon_frequencies,
        radiogeddon_frequencies_count,
        config_bands[band].lo_hz,
        config_bands[band].hi_hz);
}

static size_t radiogeddon_config_current_band(uint32_t mask) {
    for(size_t i = 0; i < CONFIG_BAND_COUNT; i++) {
        if(radiogeddon_config_band_mask(i) == mask) return i;
    }
    return CONFIG_BAND_CUSTOM;
}

static void radiogeddon_scene_config_freq_changed(VariableItem* item) {
    RadioGeddonApp* app = variable_item_get_context(item);
    uint8_t index = variable_item_get_current_value_index(item);
    char text[RG_FREQ_TEXT_SIZE];
#if RG_FEATURE_FREQ_STEP
    if(app->settings.freq_step_hz) {
        // Step within the bands the radio accepts, jumping over the gaps.
        int32_t delta = (int32_t)app->settings.freq_step_hz;
        if(index < CONFIG_STEP_MIDDLE) delta = -delta;
        app->frequency = rg_range_step(&app->bands, app->frequency, delta);
        variable_item_set_current_value_index(item, CONFIG_STEP_MIDDLE);
        rg_freq_text(app->frequency, text, sizeof(text));
        variable_item_set_current_value_text(item, text);
        return;
    }
#endif
    uint32_t hz = radiogeddon_frequencies[index];
    rg_freq_text(hz, text, sizeof(text));
    variable_item_set_current_value_text(item, text);
    app->frequency = hz;
}

static void radiogeddon_scene_config_preset_changed(VariableItem* item) {
    RadioGeddonApp* app = variable_item_get_context(item);
    uint8_t index = variable_item_get_current_value_index(item);
    variable_item_set_current_value_text(item, radiogeddon_presets[index].label);
    app->preset_index = index;
}

static void radiogeddon_scene_config_band_changed(VariableItem* item) {
    RadioGeddonApp* app = variable_item_get_context(item);
    uint8_t index = variable_item_get_current_value_index(item);
    if(index < CONFIG_BAND_COUNT) {
        uint32_t mask = radiogeddon_config_band_mask(index);
        if(mask) app->settings.scan_mask = mask;
        variable_item_set_current_value_text(item, config_bands[index].label);
    } else {
        // Custom keeps the current list; edit it with "Edit scan list".
        variable_item_set_current_value_text(item, "Custom");
    }
}

// Hop list: "Common" (the default four bands), then the same choices as the
// scan list, then "Custom".
#define CONFIG_HOP_CHOICES (CONFIG_BAND_COUNT + 2)

static uint32_t radiogeddon_config_hop_choice_mask(size_t choice) {
    if(choice == 0) return radiogeddon_settings_default_hop_mask();
    return radiogeddon_config_band_mask(choice - 1);
}

static const char* radiogeddon_config_hop_choice_label(size_t choice) {
    if(choice == 0) return "Common";
    if(choice <= CONFIG_BAND_COUNT) return config_bands[choice - 1].label;
    return "Custom";
}

static size_t radiogeddon_config_current_hop_choice(uint32_t mask) {
    for(size_t i = 0; i < CONFIG_HOP_CHOICES - 1; i++) {
        if(radiogeddon_config_hop_choice_mask(i) == mask) return i;
    }
    return CONFIG_HOP_CHOICES - 1;
}

static void radiogeddon_scene_config_hop_list_changed(VariableItem* item) {
    RadioGeddonApp* app = variable_item_get_context(item);
    uint8_t index = variable_item_get_current_value_index(item);
    if(index < CONFIG_HOP_CHOICES - 1) {
        uint32_t mask = radiogeddon_config_hop_choice_mask(index);
        if(mask) app->settings.hop_mask = mask;
    }
    variable_item_set_current_value_text(item, radiogeddon_config_hop_choice_label(index));
}

static void radiogeddon_scene_config_hop_dwell_changed(VariableItem* item) {
    RadioGeddonApp* app = variable_item_get_context(item);
    uint8_t index = variable_item_get_current_value_index(item);
    app->settings.hop_dwell_ms = config_hop_dwell_ms[index];
    char text[12];
    snprintf(text, sizeof(text), "%u ms", (unsigned)config_hop_dwell_ms[index]);
    variable_item_set_current_value_text(item, text);
}

static void radiogeddon_scene_config_hop_hold_changed(VariableItem* item) {
    RadioGeddonApp* app = variable_item_get_context(item);
    uint8_t index = variable_item_get_current_value_index(item);
    app->settings.hop_hold_ms = config_hop_hold_ms[index];
    char text[12];
    snprintf(text, sizeof(text), "%u s", (unsigned)(config_hop_hold_ms[index] / 1000));
    variable_item_set_current_value_text(item, text);
}

static void radiogeddon_scene_config_hop_record_changed(VariableItem* item) {
    RadioGeddonApp* app = variable_item_get_context(item);
    uint8_t index = variable_item_get_current_value_index(item);
    app->settings.hop_auto_record = (index == 1);
    variable_item_set_current_value_text(item, index ? "On" : "Off");
}

static void radiogeddon_scene_config_dwell_changed(VariableItem* item) {
    RadioGeddonApp* app = variable_item_get_context(item);
    uint8_t index = variable_item_get_current_value_index(item);
    app->settings.scan_dwell_ms = config_dwell_ms[index];
    char text[12];
    snprintf(text, sizeof(text), "%u ms", (unsigned)config_dwell_ms[index]);
    variable_item_set_current_value_text(item, text);
}

static void radiogeddon_scene_config_threshold_changed(VariableItem* item) {
    RadioGeddonApp* app = variable_item_get_context(item);
    uint8_t index = variable_item_get_current_value_index(item);
    app->settings.scan_threshold_db = config_threshold_db[index];
    char text[12];
    snprintf(text, sizeof(text), "+%u dB", (unsigned)config_threshold_db[index]);
    variable_item_set_current_value_text(item, text);
}

static void radiogeddon_scene_config_hold_changed(VariableItem* item) {
    RadioGeddonApp* app = variable_item_get_context(item);
    uint8_t index = variable_item_get_current_value_index(item);
    app->settings.scan_hold_on_hit = (index == 1);
    variable_item_set_current_value_text(item, index ? "On" : "Off");
}

/* Switch to the radio the settings ask for. False if an external module was
 * wanted but did not answer (the internal radio is then in use). */
static bool radiogeddon_scene_config_apply_radio(RadioGeddonApp* app, bool want_external) {
    // Start from the internal radio so a changed 5 V setting takes effect.
    radiogeddon_subghz_set_radio(app->subghz, RadioGeddonRadioInternal, false);
    bool external = false;
    if(want_external) {
        external = radiogeddon_subghz_set_radio(
                       app->subghz, RadioGeddonRadioExternal, app->settings.ext_power) ==
                   RadioGeddonRadioExternal;
    }
    app->settings.radio_external = external;
    return external == want_external;
}

static void radiogeddon_scene_config_radio_text(VariableItem* item, bool external) {
    variable_item_set_current_value_index(item, external ? 1 : 0);
    variable_item_set_current_value_text(item, external ? "External" : "Internal");
}

static void radiogeddon_scene_config_radio_changed(VariableItem* item) {
    RadioGeddonApp* app = variable_item_get_context(item);
    bool want = variable_item_get_current_value_index(item) == 1;
    bool ok = radiogeddon_scene_config_apply_radio(app, want);
    radiogeddon_scene_config_radio_text(item, app->settings.radio_external);
    if(!ok) view_dispatcher_send_custom_event(app->view_dispatcher, ConfigCustomExtMissing);
}

static void radiogeddon_scene_config_ext_power_changed(VariableItem* item) {
    RadioGeddonApp* app = variable_item_get_context(item);
    uint8_t index = variable_item_get_current_value_index(item);
    app->settings.ext_power = (index == 1);
    variable_item_set_current_value_text(item, index ? "On" : "Off");
    if(app->settings.radio_external && !radiogeddon_scene_config_apply_radio(app, true)) {
        // Without 5 V the module no longer answers: back on the internal
        // radio. The message screen says so and the list is rebuilt after it.
        view_dispatcher_send_custom_event(app->view_dispatcher, ConfigCustomExtMissing);
    }
}

#if RG_EDITION_FULL
static void radiogeddon_scene_config_scan_source_changed(VariableItem* item) {
    RadioGeddonApp* app = variable_item_get_context(item);
    uint8_t index = variable_item_get_current_value_index(item);
    app->settings.scan_source = index;
    variable_item_set_current_value_text(item, config_source_text[index]);
}

static void radiogeddon_scene_config_hop_source_changed(VariableItem* item) {
    RadioGeddonApp* app = variable_item_get_context(item);
    uint8_t index = variable_item_get_current_value_index(item);
    app->settings.hop_source = index;
    variable_item_set_current_value_text(item, config_source_text[index]);
}

static void radiogeddon_scene_config_freq_step_changed(VariableItem* item) {
    RadioGeddonApp* app = variable_item_get_context(item);
    uint8_t index = variable_item_get_current_value_index(item);
    bool was_list = app->settings.freq_step_hz == 0;
    app->settings.freq_step_hz = config_freq_steps_hz[index];
    variable_item_set_current_value_text(item, config_freq_step_text[index]);
    // Between list and step mode the Frequency row changes its value count.
    if(was_list != (app->settings.freq_step_hz == 0)) {
        view_dispatcher_send_custom_event(app->view_dispatcher, ConfigCustomRebuild);
    }
}
#endif

static void radiogeddon_scene_config_enter_cb(void* context, uint32_t index) {
    RadioGeddonApp* app = context;
#if RG_FEATURE_BAND_INFO
    if(index == ConfigItemBands) {
        view_dispatcher_send_custom_event(app->view_dispatcher, ConfigCustomBands);
        return;
    }
#endif
    if(index == ConfigItemFrequency) {
        view_dispatcher_send_custom_event(app->view_dispatcher, ConfigCustomFrequency);
    } else if(index == ConfigItemEditScanList) {
        view_dispatcher_send_custom_event(app->view_dispatcher, ConfigCustomEditScanList);
    } else if(index == ConfigItemEditHopList) {
        view_dispatcher_send_custom_event(app->view_dispatcher, ConfigCustomEditHopList);
    }
}

static uint8_t radiogeddon_config_nearest_u16(const uint16_t* values, size_t n, uint16_t v) {
    uint8_t best = 0;
    for(size_t i = 0; i < n; i++) {
        if(values[i] <= v) best = (uint8_t)i;
    }
    return best;
}

static uint8_t radiogeddon_config_nearest_u8(const uint8_t* values, size_t n, uint8_t v) {
    uint8_t best = 0;
    for(size_t i = 0; i < n; i++) {
        if(values[i] <= v) best = (uint8_t)i;
    }
    return best;
}

void radiogeddon_scene_config_on_enter(void* context) {
    RadioGeddonApp* app = context;
    VariableItemList* list = app->var_item_list;
    variable_item_list_reset(list);
    char text[16];

    // Frequency
    bool step_mode = false;
#if RG_FEATURE_FREQ_STEP
    radiogeddon_scene_probe_bands(app);
    step_mode = app->settings.freq_step_hz != 0;
#endif
    VariableItem* item = variable_item_list_add(
        list,
        "Frequency MHz",
        step_mode ? 3 : radiogeddon_frequencies_count,
        radiogeddon_scene_config_freq_changed,
        app);
    if(step_mode) {
#if RG_FEATURE_FREQ_STEP
        variable_item_set_current_value_index(item, CONFIG_STEP_MIDDLE);
#endif
    } else {
        // A custom frequency (OK opens the keyboard) is shown as it is; left
        // and right then step from the nearest one in the list.
        size_t freq_index = rg_freq_nearest(
            radiogeddon_frequencies, radiogeddon_frequencies_count, app->frequency);
        variable_item_set_current_value_index(item, (uint8_t)freq_index);
    }
    rg_freq_text(app->frequency, text, sizeof(text));
    variable_item_set_current_value_text(item, text);

    // Preset
    item = variable_item_list_add(
        list,
        "Modulation",
        radiogeddon_presets_count,
        radiogeddon_scene_config_preset_changed,
        app);
    variable_item_set_current_value_index(item, app->preset_index);
    variable_item_set_current_value_text(item, radiogeddon_presets[app->preset_index].label);

    // Detection threshold (scanner and hopper)
    item = variable_item_list_add(
        list,
        "Threshold",
        COUNT_OF_ARRAY(config_threshold_db),
        radiogeddon_scene_config_threshold_changed,
        app);
    uint8_t ti = radiogeddon_config_nearest_u8(
        config_threshold_db, COUNT_OF_ARRAY(config_threshold_db), app->settings.scan_threshold_db);
    variable_item_set_current_value_index(item, ti);
    radiogeddon_scene_config_threshold_changed(item);

    // Scanner: scan list preset
    item = variable_item_list_add(
        list, "Scan list", CONFIG_BAND_COUNT + 1, radiogeddon_scene_config_band_changed, app);
    size_t band = radiogeddon_config_current_band(app->settings.scan_mask);
    variable_item_set_current_value_index(item, band);
    variable_item_set_current_value_text(
        item, band < CONFIG_BAND_COUNT ? config_bands[band].label : "Custom");

    // Scanner: per-frequency editor (press OK)
    item = variable_item_list_add(list, "Edit scan list", 1, NULL, app);
    uint32_t enabled = 0;
    for(size_t i = 0; i < radiogeddon_frequencies_count && i < 32; i++) {
        if(app->settings.scan_mask & (1u << i)) enabled++;
    }
    snprintf(text, sizeof(text), "%lu on >", (unsigned long)enabled);
    variable_item_set_current_value_text(item, text);

    // Scanner: dwell
    item = variable_item_list_add(
        list,
        "Scan dwell",
        COUNT_OF_ARRAY(config_dwell_ms),
        radiogeddon_scene_config_dwell_changed,
        app);
    uint8_t di = radiogeddon_config_nearest_u16(
        config_dwell_ms, COUNT_OF_ARRAY(config_dwell_ms), app->settings.scan_dwell_ms);
    variable_item_set_current_value_index(item, di);
    radiogeddon_scene_config_dwell_changed(item);

    // Scanner: hold on hit
    item =
        variable_item_list_add(list, "Hold on hit", 2, radiogeddon_scene_config_hold_changed, app);
    variable_item_set_current_value_index(item, app->settings.scan_hold_on_hit ? 1 : 0);
    variable_item_set_current_value_text(item, app->settings.scan_hold_on_hit ? "On" : "Off");

    // Hopper: hop list preset
    item = variable_item_list_add(
        list, "Hop list", CONFIG_HOP_CHOICES, radiogeddon_scene_config_hop_list_changed, app);
    size_t hop_choice = radiogeddon_config_current_hop_choice(app->settings.hop_mask);
    variable_item_set_current_value_index(item, hop_choice);
    variable_item_set_current_value_text(item, radiogeddon_config_hop_choice_label(hop_choice));

    // Hopper: per-frequency editor (press OK)
    item = variable_item_list_add(list, "Edit hop list", 1, NULL, app);
    enabled = 0;
    for(size_t i = 0; i < radiogeddon_frequencies_count && i < 32; i++) {
        if(app->settings.hop_mask & (1u << i)) enabled++;
    }
    snprintf(text, sizeof(text), "%lu on >", (unsigned long)enabled);
    variable_item_set_current_value_text(item, text);

    // Hopper: dwell
    item = variable_item_list_add(
        list,
        "Hop dwell",
        COUNT_OF_ARRAY(config_hop_dwell_ms),
        radiogeddon_scene_config_hop_dwell_changed,
        app);
    variable_item_set_current_value_index(
        item,
        radiogeddon_config_nearest_u16(
            config_hop_dwell_ms, COUNT_OF_ARRAY(config_hop_dwell_ms), app->settings.hop_dwell_ms));
    radiogeddon_scene_config_hop_dwell_changed(item);

    // Hopper: activity hold
    item = variable_item_list_add(
        list,
        "Activity hold",
        COUNT_OF_ARRAY(config_hop_hold_ms),
        radiogeddon_scene_config_hop_hold_changed,
        app);
    variable_item_set_current_value_index(
        item,
        radiogeddon_config_nearest_u16(
            config_hop_hold_ms, COUNT_OF_ARRAY(config_hop_hold_ms), app->settings.hop_hold_ms));
    radiogeddon_scene_config_hop_hold_changed(item);

    // Hopper: automatic RAW recording of activity
    item = variable_item_list_add(
        list, "Hop auto-rec", 2, radiogeddon_scene_config_hop_record_changed, app);
    variable_item_set_current_value_index(item, app->settings.hop_auto_record ? 1 : 0);
    variable_item_set_current_value_text(item, app->settings.hop_auto_record ? "On" : "Off");

    // Radio: internal, or an external CC1101 module (only if one answers)
    item = variable_item_list_add(list, "Radio", 2, radiogeddon_scene_config_radio_changed, app);
    radiogeddon_scene_config_radio_text(
        item, radiogeddon_subghz_get_radio(app->subghz) == RadioGeddonRadioExternal);

    item = variable_item_list_add(
        list, "Ext radio 5V", 2, radiogeddon_scene_config_ext_power_changed, app);
    variable_item_set_current_value_index(item, app->settings.ext_power ? 1 : 0);
    variable_item_set_current_value_text(item, app->settings.ext_power ? "On" : "Off");

#if RG_EDITION_FULL
    // Scanner and Hopper: built-in list or the favorites.
    item = variable_item_list_add(
        list,
        "Scan source",
        RadioGeddonSourceCount,
        radiogeddon_scene_config_scan_source_changed,
        app);
    variable_item_set_current_value_index(item, app->settings.scan_source);
    variable_item_set_current_value_text(item, config_source_text[app->settings.scan_source]);
    item = variable_item_list_add(
        list,
        "Hop source",
        RadioGeddonSourceCount,
        radiogeddon_scene_config_hop_source_changed,
        app);
    variable_item_set_current_value_index(item, app->settings.hop_source);
    variable_item_set_current_value_text(item, config_source_text[app->settings.hop_source]);

    // Left/Right step on "Frequency MHz".
    item = variable_item_list_add(
        list,
        "Freq step",
        COUNT_OF_ARRAY(config_freq_steps_hz),
        radiogeddon_scene_config_freq_step_changed,
        app);
    uint8_t fi = 0;
    for(size_t i = 0; i < COUNT_OF_ARRAY(config_freq_steps_hz); i++) {
        if(config_freq_steps_hz[i] == app->settings.freq_step_hz) fi = (uint8_t)i;
    }
    variable_item_set_current_value_index(item, fi);
    variable_item_set_current_value_text(item, config_freq_step_text[fi]);

    item = variable_item_list_add(list, "Radio bands", 1, NULL, app);
    variable_item_set_current_value_text(item, ">");
#endif

    variable_item_list_set_enter_callback(list, radiogeddon_scene_config_enter_cb, app);
    variable_item_list_set_selected_item(
        list, scene_manager_get_scene_state(app->scene_manager, RadioGeddonSceneConfig));

    view_dispatcher_switch_to_view(app->view_dispatcher, RadioGeddonViewVarItemList);
}

bool radiogeddon_scene_config_on_event(void* context, SceneManagerEvent event) {
    RadioGeddonApp* app = context;
    if(event.type == SceneManagerEventTypeCustom && event.event == ConfigCustomExtMissing) {
        scene_manager_set_scene_state(app->scene_manager, RadioGeddonSceneConfig, ConfigItemRadio);
        radiogeddon_scene_show_message(
            app,
            "No external radio",
            "No CC1101 module answered\non the GPIO pins. Using\nthe internal radio.");
        return true;
    }
#if RG_EDITION_FULL
    if(event.type == SceneManagerEventTypeCustom && event.event == ConfigCustomRebuild) {
        scene_manager_set_scene_state(
            app->scene_manager, RadioGeddonSceneConfig, ConfigItemFreqStep);
        radiogeddon_scene_config_on_enter(app);
        return true;
    }
    if(event.type == SceneManagerEventTypeCustom && event.event == ConfigCustomBands) {
        scene_manager_set_scene_state(app->scene_manager, RadioGeddonSceneConfig, ConfigItemBands);
        scene_manager_next_scene(app->scene_manager, RadioGeddonSceneBands);
        return true;
    }
#endif
    if(event.type == SceneManagerEventTypeCustom && event.event == ConfigCustomFrequency) {
        app->freq_target = RadioGeddonFreqTargetReceive;
        scene_manager_set_scene_state(
            app->scene_manager, RadioGeddonSceneConfig, ConfigItemFrequency);
        scene_manager_set_scene_state(app->scene_manager, RadioGeddonSceneFrequency, 0);
        scene_manager_next_scene(app->scene_manager, RadioGeddonSceneFrequency);
        return true;
    }
    if(event.type == SceneManagerEventTypeCustom &&
       (event.event == ConfigCustomEditScanList || event.event == ConfigCustomEditHopList)) {
        bool hop = (event.event == ConfigCustomEditHopList);
        scene_manager_set_scene_state(
            app->scene_manager,
            RadioGeddonSceneConfig,
            hop ? ConfigItemEditHopList : ConfigItemEditScanList);
        // The list editor serves both lists; its scene state says which.
        scene_manager_set_scene_state(app->scene_manager, RadioGeddonSceneScanList, hop ? 1 : 0);
        scene_manager_next_scene(app->scene_manager, RadioGeddonSceneScanList);
        return true;
    }
    return false;
}

void radiogeddon_scene_config_on_exit(void* context) {
    RadioGeddonApp* app = context;
    // A custom frequency may not suit a radio chosen after it was typed.
    if(!radiogeddon_subghz_is_frequency_allowed(app->subghz, app->frequency)) {
        app->frequency = RADIOGEDDON_FREQUENCY_DEFAULT;
    }
    // Apply to the radio wrapper and persist across launches.
    radiogeddon_subghz_set_frequency(app->subghz, app->frequency);
    radiogeddon_subghz_set_preset(app->subghz, app->preset_index);
    radiogeddon_app_save_settings(app);
    variable_item_list_reset(app->var_item_list);
}
