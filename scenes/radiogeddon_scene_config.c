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
} ConfigItem;

#define ConfigCustomEditScanList 600
#define ConfigCustomEditHopList  601

static void radiogeddon_config_freq_text(uint32_t hz, char* out, size_t out_size) {
    snprintf(
        out,
        out_size,
        "%lu.%02lu",
        (unsigned long)(hz / 1000000),
        (unsigned long)((hz % 1000000) / 10000));
}

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
    uint32_t hz = radiogeddon_frequencies[index];
    char text[12];
    radiogeddon_config_freq_text(hz, text, sizeof(text));
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

static void radiogeddon_scene_config_enter_cb(void* context, uint32_t index) {
    RadioGeddonApp* app = context;
    if(index == ConfigItemEditScanList) {
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
    VariableItem* item = variable_item_list_add(
        list,
        "Frequency MHz",
        radiogeddon_frequencies_count,
        radiogeddon_scene_config_freq_changed,
        app);
    uint8_t freq_index = 0;
    for(size_t i = 0; i < radiogeddon_frequencies_count; i++) {
        if(radiogeddon_frequencies[i] == app->frequency) {
            freq_index = (uint8_t)i;
            break;
        }
    }
    variable_item_set_current_value_index(item, freq_index);
    radiogeddon_config_freq_text(radiogeddon_frequencies[freq_index], text, sizeof(text));
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

    variable_item_list_set_enter_callback(list, radiogeddon_scene_config_enter_cb, app);
    variable_item_list_set_selected_item(
        list, scene_manager_get_scene_state(app->scene_manager, RadioGeddonSceneConfig));

    view_dispatcher_switch_to_view(app->view_dispatcher, RadioGeddonViewVarItemList);
}

bool radiogeddon_scene_config_on_event(void* context, SceneManagerEvent event) {
    RadioGeddonApp* app = context;
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
    // Apply to the radio wrapper and persist across launches.
    radiogeddon_subghz_set_frequency(app->subghz, app->frequency);
    radiogeddon_subghz_set_preset(app->subghz, app->preset_index);
    radiogeddon_app_save_settings(app);
    variable_item_list_reset(app->var_item_list);
}
