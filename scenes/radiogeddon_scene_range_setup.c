#include "radiogeddon_scene.h"

#if RG_FEATURE_RANGE_SCAN

// Full edition: set up a range scan (start, end, step, dwell, threshold,
// pause on hit, modulation), see how many points and how long a sweep takes,
// start it, or save / load / delete it as a named profile.

typedef enum {
    RangeItemStart,
    RangeItemEnd,
    RangeItemStep,
    RangeItemDwell,
    RangeItemThreshold,
    RangeItemHold,
    RangeItemModulation,
    RangeItemPlan,
    RangeItemScan,
#if RG_FEATURE_WATERFALL
    RangeItemWaterfall,
#endif
    RangeItemSave,
    RangeItemLoad,
    RangeItemDelete,
} RangeItem;

typedef enum {
    RangeEventStart = 800,
    RangeEventEnd,
    RangeEventScan,
    RangeEventWaterfall,
    RangeEventSave,
    RangeEventLoad,
    RangeEventDelete,
} RangeEvent;

static const uint32_t range_steps_hz[] = {
    1000,
    5000,
    10000,
    12500,
    25000,
    50000,
    100000,
    250000,
    500000,
    1000000,
    2000000,
    5000000,
    10000000,
};
static const uint16_t range_dwell_ms[] = {1, 2, 5, 10, 20, 50, 100};
static const uint8_t range_threshold_db[] = {6, 8, 10, 15, 20, 30};

#define RANGE_COUNT(a) (sizeof(a) / sizeof((a)[0]))

/* Kept free beyond the engine, the screen model and the radio session. */
#define RANGE_HEAP_MARGIN (12u * 1024u)

static VariableItem* range_plan_item;

#if RG_FEATURE_WATERFALL
uint32_t radiogeddon_scene_range_setup_waterfall_item(void) {
    return RangeItemWaterfall;
}
#endif

static void radiogeddon_range_step_text(uint32_t hz, char* out, size_t size) {
    if(hz >= 1000000 && hz % 1000000 == 0) {
        snprintf(out, size, "%lu MHz", (unsigned long)(hz / 1000000));
    } else if(hz % 1000 == 0) {
        snprintf(out, size, "%lu kHz", (unsigned long)(hz / 1000));
    } else {
        snprintf(
            out, size, "%lu.%lu kHz", (unsigned long)(hz / 1000), (unsigned long)(hz % 1000 / 100));
    }
}

static void radiogeddon_scene_range_setup_update_plan(RadioGeddonApp* app) {
    if(!range_plan_item) return;
    RgRangeResult r = radiogeddon_scene_plan_range(app);
    char text[24];
    if(r == RgRangeOk || r == RgRangeErrorTooMany) {
        uint32_t ms = rg_range_sweep_ms(app->range.points, app->settings.range_dwell_ms);
        if(r == RgRangeErrorTooMany) {
            snprintf(
                text,
                sizeof(text),
                "%lu>%u!",
                (unsigned long)app->range.points,
                (unsigned)RG_RANGE_MAX_POINTS);
        } else if(ms >= 10000) {
            snprintf(
                text,
                sizeof(text),
                "%lu %lus",
                (unsigned long)app->range.points,
                (unsigned long)(ms / 1000));
        } else {
            snprintf(
                text,
                sizeof(text),
                "%lu %lu.%lus",
                (unsigned long)app->range.points,
                (unsigned long)(ms / 1000),
                (unsigned long)(ms % 1000 / 100));
        }
    } else {
        snprintf(text, sizeof(text), "%s", rg_range_result_text(r));
    }
    variable_item_set_current_value_text(range_plan_item, text);
}

static void radiogeddon_scene_range_step_changed(VariableItem* item) {
    RadioGeddonApp* app = variable_item_get_context(item);
    uint8_t index = variable_item_get_current_value_index(item);
    app->settings.range_step_hz = range_steps_hz[index];
    char text[16];
    radiogeddon_range_step_text(range_steps_hz[index], text, sizeof(text));
    variable_item_set_current_value_text(item, text);
    radiogeddon_scene_range_setup_update_plan(app);
}

static void radiogeddon_scene_range_dwell_changed(VariableItem* item) {
    RadioGeddonApp* app = variable_item_get_context(item);
    uint8_t index = variable_item_get_current_value_index(item);
    app->settings.range_dwell_ms = range_dwell_ms[index];
    char text[12];
    snprintf(text, sizeof(text), "%u ms", (unsigned)range_dwell_ms[index]);
    variable_item_set_current_value_text(item, text);
    radiogeddon_scene_range_setup_update_plan(app);
}

static void radiogeddon_scene_range_threshold_changed(VariableItem* item) {
    RadioGeddonApp* app = variable_item_get_context(item);
    uint8_t index = variable_item_get_current_value_index(item);
    app->settings.scan_threshold_db = range_threshold_db[index];
    char text[12];
    snprintf(text, sizeof(text), "+%u dB", (unsigned)range_threshold_db[index]);
    variable_item_set_current_value_text(item, text);
}

static void radiogeddon_scene_range_hold_changed(VariableItem* item) {
    RadioGeddonApp* app = variable_item_get_context(item);
    uint8_t index = variable_item_get_current_value_index(item);
    app->settings.range_hold_on_hit = (index == 1);
    variable_item_set_current_value_text(item, index ? "On" : "Off");
}

static void radiogeddon_scene_range_preset_changed(VariableItem* item) {
    RadioGeddonApp* app = variable_item_get_context(item);
    uint8_t index = variable_item_get_current_value_index(item);
    app->preset_index = index;
    variable_item_set_current_value_text(item, radiogeddon_presets[index].label);
}

static void radiogeddon_scene_range_enter_cb(void* context, uint32_t index) {
    RadioGeddonApp* app = context;
    static const uint32_t events[] = {
        [RangeItemStart] = RangeEventStart,
        [RangeItemEnd] = RangeEventEnd,
        [RangeItemScan] = RangeEventScan,
#if RG_FEATURE_WATERFALL
        [RangeItemWaterfall] = RangeEventWaterfall,
#endif
        [RangeItemSave] = RangeEventSave,
        [RangeItemLoad] = RangeEventLoad,
        [RangeItemDelete] = RangeEventDelete,
    };
    if(index < RANGE_COUNT(events) && events[index]) {
        view_dispatcher_send_custom_event(app->view_dispatcher, events[index]);
    }
}

static uint8_t radiogeddon_range_index_u32(const uint32_t* values, size_t n, uint32_t v) {
    uint8_t best = 0;
    for(size_t i = 0; i < n; i++) {
        if(values[i] <= v) best = (uint8_t)i;
    }
    return best;
}

static uint8_t radiogeddon_range_index_u16(const uint16_t* values, size_t n, uint16_t v) {
    uint8_t best = 0;
    for(size_t i = 0; i < n; i++) {
        if(values[i] <= v) best = (uint8_t)i;
    }
    return best;
}

static uint8_t radiogeddon_range_index_u8(const uint8_t* values, size_t n, uint8_t v) {
    uint8_t best = 0;
    for(size_t i = 0; i < n; i++) {
        if(values[i] <= v) best = (uint8_t)i;
    }
    return best;
}

void radiogeddon_scene_range_setup_on_enter(void* context) {
    RadioGeddonApp* app = context;
    // Back from the scan screen: its engine is gone already (freed on exit).
    radiogeddon_scene_probe_bands(app);

    VariableItemList* list = app->var_item_list;
    variable_item_list_reset(list);
    range_plan_item = NULL;
    char text[RG_FREQ_TEXT_SIZE + 4];

    VariableItem* item = variable_item_list_add(list, "Start MHz", 1, NULL, app);
    rg_freq_text(app->settings.range_start_hz, text, sizeof(text));
    variable_item_set_current_value_text(item, text);

    item = variable_item_list_add(list, "End MHz", 1, NULL, app);
    rg_freq_text(app->settings.range_end_hz, text, sizeof(text));
    variable_item_set_current_value_text(item, text);

    item = variable_item_list_add(
        list, "Step", RANGE_COUNT(range_steps_hz), radiogeddon_scene_range_step_changed, app);
    uint8_t si = radiogeddon_range_index_u32(
        range_steps_hz, RANGE_COUNT(range_steps_hz), app->settings.range_step_hz);
    variable_item_set_current_value_index(item, si);
    // A step from a profile that is not in the list is kept and shown as is.
    radiogeddon_range_step_text(app->settings.range_step_hz, text, sizeof(text));
    variable_item_set_current_value_text(item, text);

    item = variable_item_list_add(
        list, "Dwell", RANGE_COUNT(range_dwell_ms), radiogeddon_scene_range_dwell_changed, app);
    variable_item_set_current_value_index(
        item,
        radiogeddon_range_index_u16(
            range_dwell_ms, RANGE_COUNT(range_dwell_ms), app->settings.range_dwell_ms));
    radiogeddon_scene_range_dwell_changed(item);

    item = variable_item_list_add(
        list,
        "Threshold",
        RANGE_COUNT(range_threshold_db),
        radiogeddon_scene_range_threshold_changed,
        app);
    variable_item_set_current_value_index(
        item,
        radiogeddon_range_index_u8(
            range_threshold_db, RANGE_COUNT(range_threshold_db), app->settings.scan_threshold_db));
    radiogeddon_scene_range_threshold_changed(item);

    item =
        variable_item_list_add(list, "Pause on hit", 2, radiogeddon_scene_range_hold_changed, app);
    variable_item_set_current_value_index(item, app->settings.range_hold_on_hit ? 1 : 0);
    variable_item_set_current_value_text(item, app->settings.range_hold_on_hit ? "On" : "Off");

    item = variable_item_list_add(
        list, "Modulation", radiogeddon_presets_count, radiogeddon_scene_range_preset_changed, app);
    variable_item_set_current_value_index(item, app->preset_index);
    variable_item_set_current_value_text(item, radiogeddon_presets[app->preset_index].label);

    // Points and estimated sweep time (or why the range cannot be scanned).
    range_plan_item = variable_item_list_add(list, "Points/sweep", 1, NULL, app);

    item = variable_item_list_add(list, "Start scan", 1, NULL, app);
    variable_item_set_current_value_text(item, ">");
#if RG_FEATURE_WATERFALL
    item = variable_item_list_add(list, "Start waterfall", 1, NULL, app);
    variable_item_set_current_value_text(item, ">");
#endif
    item = variable_item_list_add(list, "Save profile", 1, NULL, app);
    variable_item_set_current_value_text(item, ">");
    item = variable_item_list_add(list, "Load profile", 1, NULL, app);
    variable_item_set_current_value_text(item, ">");
    item = variable_item_list_add(list, "Delete profile", 1, NULL, app);
    variable_item_set_current_value_text(item, ">");

    radiogeddon_scene_range_setup_update_plan(app);
    variable_item_list_set_enter_callback(list, radiogeddon_scene_range_enter_cb, app);
    variable_item_list_set_selected_item(
        list, scene_manager_get_scene_state(app->scene_manager, RadioGeddonSceneRangeSetup));
    view_dispatcher_switch_to_view(app->view_dispatcher, RadioGeddonViewVarItemList);
}

/* Check the radio, the plan and the memory before the scan or the waterfall. */
static bool radiogeddon_scene_range_setup_check(RadioGeddonApp* app, bool waterfall) {
    if(!radiogeddon_subghz_is_device_present(app->subghz)) {
        radiogeddon_scene_show_message(
            app, "No radio", "Sub-GHz device not\nfound or not responding.");
        return false;
    }
    RgRangeResult r = radiogeddon_scene_plan_range(app);
    if(r == RgRangeErrorTooMany) {
        radiogeddon_scene_show_message(
            app,
            "Too many points",
            "At most 256 points.\nUse a larger step or a\nnarrower range.");
        return false;
    }
    if(r == RgRangeErrorNoPoints) {
        radiogeddon_scene_show_message(
            app,
            "No tunable points",
            "The range lies in a gap\nthe radio cannot tune.\nSee Settings > Bands.");
        return false;
    }
    if(r != RgRangeOk) {
        radiogeddon_scene_show_message(app, "Bad range", "Start must not be\nabove end.");
        return false;
    }
    // The engine and the screen model must fit with room to spare.
    size_t need = radiogeddon_rangescan_memory(app->range.points) + 2048u + RANGE_HEAP_MARGIN;
#if RG_FEATURE_WATERFALL
    if(waterfall) {
        need = radiogeddon_scene_waterfall_fixed_bytes(app->range.points) +
               radiogeddon_scene_waterfall_min_bytes(app->range.points) + RANGE_HEAP_MARGIN;
    }
#else
    UNUSED(waterfall);
#endif
    if(memmgr_heap_get_max_free_block() < need) {
        radiogeddon_scene_show_message(
            app,
            "Not enough memory",
            "Free memory is too low\nfor this many points.\nUse fewer points.");
        return false;
    }
    return true;
}

bool radiogeddon_scene_range_setup_on_event(void* context, SceneManagerEvent event) {
    RadioGeddonApp* app = context;
    if(event.type != SceneManagerEventTypeCustom) return false;
    RangeItem item;
    switch(event.event) {
    case RangeEventStart:
    case RangeEventEnd:
        item = event.event == RangeEventStart ? RangeItemStart : RangeItemEnd;
        scene_manager_set_scene_state(app->scene_manager, RadioGeddonSceneRangeSetup, item);
        app->freq_target = event.event == RangeEventStart ? RadioGeddonFreqTargetRangeStart :
                                                            RadioGeddonFreqTargetRangeEnd;
        scene_manager_set_scene_state(app->scene_manager, RadioGeddonSceneFrequency, 0);
        scene_manager_next_scene(app->scene_manager, RadioGeddonSceneFrequency);
        return true;
    case RangeEventScan:
        scene_manager_set_scene_state(
            app->scene_manager, RadioGeddonSceneRangeSetup, RangeItemScan);
        if(radiogeddon_scene_range_setup_check(app, false)) {
            app->range_cursor = 0;
            scene_manager_next_scene(app->scene_manager, RadioGeddonSceneRangeScan);
        }
        return true;
#if RG_FEATURE_WATERFALL
    case RangeEventWaterfall:
        scene_manager_set_scene_state(
            app->scene_manager, RadioGeddonSceneRangeSetup, RangeItemWaterfall);
        if(radiogeddon_scene_range_setup_check(app, true)) {
            app->wf_cursor = 0;
            scene_manager_next_scene(app->scene_manager, RadioGeddonSceneWaterfall);
        }
        return true;
#endif
    case RangeEventSave:
        scene_manager_set_scene_state(
            app->scene_manager, RadioGeddonSceneRangeSetup, RangeItemSave);
        if(radiogeddon_scene_plan_range(app) == RgRangeErrorOrder) {
            radiogeddon_scene_show_message(app, "Bad range", "Start must not be\nabove end.");
        } else {
            scene_manager_next_scene(app->scene_manager, RadioGeddonSceneProfileName);
        }
        return true;
    case RangeEventLoad:
    case RangeEventDelete:
        item = event.event == RangeEventLoad ? RangeItemLoad : RangeItemDelete;
        scene_manager_set_scene_state(app->scene_manager, RadioGeddonSceneRangeSetup, item);
        // The profile list serves both; its scene state says which.
        scene_manager_set_scene_state(
            app->scene_manager, RadioGeddonSceneProfiles, event.event == RangeEventDelete);
        scene_manager_next_scene(app->scene_manager, RadioGeddonSceneProfiles);
        return true;
    default:
        return false;
    }
}

void radiogeddon_scene_range_setup_on_exit(void* context) {
    RadioGeddonApp* app = context;
    range_plan_item = NULL;
    radiogeddon_app_save_settings(app);
    variable_item_list_reset(app->var_item_list);
}

#endif
