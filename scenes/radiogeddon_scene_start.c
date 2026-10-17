#include "radiogeddon_scene.h"

// The Catalog edition lists its tools directly. The Full edition groups the
// scanning tools under Scan and the file tools under Analyze, so the main
// menu stays one screen long.
typedef enum {
    StartIndexScanner,
    StartIndexRange, // unused: under Scan in the Full edition
    StartIndexReceiver,
    StartIndexHopper,
    StartIndexFavorites, // unused: under Scan in the Full edition
    StartIndexDatabase,
    StartIndexSettings,
    StartIndexAbout,
    StartIndexScan, // Full edition
    StartIndexAnalyze, // Full edition
    StartIndexSessions, // Full edition
    StartIndexLab, // Full edition: the Dolphin Lab room (lab styles)
    StartIndexTip, // Full edition: the Dolphin Assistant's tip, when there is one
} StartIndex;

#if RG_FEATURE_DOLPHIN_LAB
// The dashboard (Dolphin Lab and Cyber Lab styles): every Full menu item,
// and the Dolphin Lab room, after the assistant's tip when there is one.
// Its events are positions in start_lab_shown.
static const RadioGeddonLabItem start_lab_items[] = {
    {"Scan", RgSpriteIconScan},
    {"Receive", RgSpriteIconReceive},
    {"Analyze", RgSpriteIconAnalyze},
    {"Database", RgSpriteIconDatabase},
    {"Sessions", RgSpriteIconSessions},
    {"Dolphin Lab", RgSpriteIconLab},
    {"Settings", RgSpriteIconSettings},
    {"About", RgSpriteIconAbout},
};
static const uint8_t start_lab_index[] = {
    StartIndexScan,
    StartIndexReceiver,
    StartIndexAnalyze,
    StartIndexDatabase,
    StartIndexSessions,
    StartIndexLab,
    StartIndexSettings,
    StartIndexAbout,
};
#define START_LAB_COUNT (sizeof(start_lab_items) / sizeof(start_lab_items[0]))
/* What the dashboard shows now: the items above, maybe after a tip. */
static RadioGeddonLabItem start_lab_shown[START_LAB_COUNT + 1];
static uint8_t start_lab_shown_index[START_LAB_COUNT + 1];
static uint8_t start_lab_shown_count;
#endif

#if RG_FEATURE_ASSISTANT
static char start_tip_label[40]; // the Classic menu's row
static char start_tip_short[16]; // the dashboard's row
#endif

/* The assistant's tip for now (none in the Catalog edition). */
static RG_UI_TEXT bool radiogeddon_scene_start_tip(RadioGeddonApp* app) {
#if RG_FEATURE_ASSISTANT
    RgTip tip = radiogeddon_research_tip(app);
    if(tip == RgTipNone) return false;
    snprintf(start_tip_label, sizeof(start_tip_label), "Tip: %s", rg_tip_text(tip));
    snprintf(start_tip_short, sizeof(start_tip_short), "%s", rg_tip_short(tip));
    return true;
#else
    UNUSED(app);
    return false;
#endif
}

static RG_UI_TEXT void radiogeddon_scene_start_submenu_callback(void* context, uint32_t index) {
    RadioGeddonApp* app = context;
    view_dispatcher_send_custom_event(app->view_dispatcher, index);
}

RG_UI_TEXT void radiogeddon_scene_start_on_enter(void* context) {
    RadioGeddonApp* app = context;
    // Back at the main menu: scanner results and the Database index are no
    // longer needed.
    radiogeddon_scene_db_release(app);
    if(app->scanner) {
        radiogeddon_scanner_free(app->scanner);
        app->scanner = NULL;
    }
    if(app->hopper) {
        radiogeddon_hopper_free(app->hopper);
        app->hopper = NULL;
    }
#if RG_FEATURE_SESSIONS
    radiogeddon_scene_sessions_release(app);
#endif
#if RG_FEATURE_FINGERPRINT
    // Find Similar Signals' results (its screens are all closed now).
    free(app->similar);
    app->similar = NULL;
#endif
#if RG_FEATURE_ADAPTIVE_SCAN
    app->scan_mode_run = RgAdaptStandard;
#endif
    bool tip = radiogeddon_scene_start_tip(app);
#if RG_FEATURE_DOLPHIN_LAB
    if(radiogeddon_ui_active(app)) {
        start_lab_shown_count = 0;
#if RG_FEATURE_ASSISTANT
        if(tip) {
            start_lab_shown[0] = (RadioGeddonLabItem){start_tip_short, RgSpriteIconTip};
            start_lab_shown_index[0] = StartIndexTip;
            start_lab_shown_count = 1;
        }
#endif
        for(size_t i = 0; i < START_LAB_COUNT; i++) {
            start_lab_shown[start_lab_shown_count] = start_lab_items[i];
            start_lab_shown_index[start_lab_shown_count++] = start_lab_index[i];
        }
        // Scene state: the position on the dashboard.
        uint32_t pos = scene_manager_get_scene_state(app->scene_manager, RadioGeddonSceneStart);
        radiogeddon_ui_show_home(
            app,
            start_lab_shown,
            start_lab_shown_count,
            (uint8_t)(pos < start_lab_shown_count ? pos : 0),
            true);
        return;
    }
#endif
    Submenu* submenu = app->submenu;
    submenu_reset(submenu);
    submenu_set_header(submenu, RG_EDITION_FULL ? "RadioGeddon Full" : "RadioGeddon");

#if RG_EDITION_FULL
#if RG_FEATURE_ASSISTANT
    if(tip) {
        submenu_add_item(
            submenu, start_tip_label, StartIndexTip, radiogeddon_scene_start_submenu_callback, app);
    }
#endif
    submenu_add_item(
        submenu, "Scan", StartIndexScan, radiogeddon_scene_start_submenu_callback, app);
    submenu_add_item(
        submenu,
        "Receive & Record",
        StartIndexReceiver,
        radiogeddon_scene_start_submenu_callback,
        app);
    submenu_add_item(
        submenu, "Analyze", StartIndexAnalyze, radiogeddon_scene_start_submenu_callback, app);
    submenu_add_item(
        submenu, "Database", StartIndexDatabase, radiogeddon_scene_start_submenu_callback, app);
    submenu_add_item(
        submenu, "Sessions", StartIndexSessions, radiogeddon_scene_start_submenu_callback, app);
#else
    UNUSED(tip);
    submenu_add_item(
        submenu, "Scanner", StartIndexScanner, radiogeddon_scene_start_submenu_callback, app);
    submenu_add_item(
        submenu,
        "Receive & Record",
        StartIndexReceiver,
        radiogeddon_scene_start_submenu_callback,
        app);
    submenu_add_item(
        submenu,
        "Frequency Hopper",
        StartIndexHopper,
        radiogeddon_scene_start_submenu_callback,
        app);
    submenu_add_item(
        submenu, "Database", StartIndexDatabase, radiogeddon_scene_start_submenu_callback, app);
#endif
    submenu_add_item(
        submenu, "Settings", StartIndexSettings, radiogeddon_scene_start_submenu_callback, app);
    submenu_add_item(
        submenu, "About", StartIndexAbout, radiogeddon_scene_start_submenu_callback, app);

    submenu_set_selected_item(
        submenu, scene_manager_get_scene_state(app->scene_manager, RadioGeddonSceneStart));

    view_dispatcher_switch_to_view(app->view_dispatcher, RadioGeddonViewSubmenu);
}

/* Open the screen of the menu item @p index (StartIndex). */
static RG_UI_TEXT bool radiogeddon_scene_start_open(RadioGeddonApp* app, uint32_t index) {
    switch(index) {
    case StartIndexScanner:
        scene_manager_next_scene(app->scene_manager, RadioGeddonSceneScanner);
        break;
    case StartIndexReceiver:
        scene_manager_next_scene(app->scene_manager, RadioGeddonSceneReceiver);
        break;
    case StartIndexHopper:
        scene_manager_next_scene(app->scene_manager, RadioGeddonSceneHopper);
        break;
    case StartIndexDatabase:
        scene_manager_next_scene(app->scene_manager, RadioGeddonSceneSavedList);
        break;
    case StartIndexSettings:
        scene_manager_set_scene_state(app->scene_manager, RadioGeddonSceneConfig, 0);
        scene_manager_next_scene(app->scene_manager, RadioGeddonSceneConfig);
        break;
    case StartIndexAbout:
        scene_manager_next_scene(app->scene_manager, RadioGeddonSceneAbout);
        break;
#if RG_EDITION_FULL
    case StartIndexScan:
        scene_manager_next_scene(app->scene_manager, RadioGeddonSceneScanMenu);
        break;
    case StartIndexAnalyze:
        scene_manager_next_scene(app->scene_manager, RadioGeddonSceneAnalyzeMenu);
        break;
    case StartIndexSessions:
        scene_manager_set_scene_state(app->scene_manager, RadioGeddonSceneSessions, 0);
        scene_manager_next_scene(app->scene_manager, RadioGeddonSceneSessions);
        break;
#endif
#if RG_FEATURE_DOLPHIN_LAB
    case StartIndexLab:
        scene_manager_next_scene(app->scene_manager, RadioGeddonSceneRoom);
        break;
#endif
#if RG_FEATURE_ASSISTANT
    case StartIndexTip:
        scene_manager_next_scene(app->scene_manager, RadioGeddonSceneAssistant);
        break;
#endif
    default:
        break;
    }
    return true;
}

RG_UI_TEXT bool radiogeddon_scene_start_on_event(void* context, SceneManagerEvent event) {
    RadioGeddonApp* app = context;
    bool consumed = false;

#if RG_FEATURE_DOLPHIN_LAB
    if(radiogeddon_ui_active(app) && event.type == SceneManagerEventTypeCustom) {
        // Dashboard positions; long OK is a shortcut straight into the tool.
        uint32_t pos = event.event;
        bool shortcut = pos >= RADIOGEDDON_UI_SHORTCUT_BASE;
        if(shortcut) pos -= RADIOGEDDON_UI_SHORTCUT_BASE;
        if(pos >= start_lab_shown_count) return false;
        uint8_t index = start_lab_shown_index[pos];
#if RG_FEATURE_ASSISTANT
        if(shortcut && index == StartIndexTip) {
            // Long OK on the tip dismisses it.
            radiogeddon_research_dismiss_tip(app);
            scene_manager_set_scene_state(app->scene_manager, RadioGeddonSceneStart, 0);
            radiogeddon_scene_start_on_enter(app);
            return true;
        }
#endif
        if(shortcut && index == StartIndexScan) {
            scene_manager_next_scene(app->scene_manager, RadioGeddonSceneScanner);
        } else if(shortcut && index == StartIndexReceiver) {
            app->receiver_autorecord = true; // Receive and record at once
            scene_manager_next_scene(app->scene_manager, RadioGeddonSceneReceiver);
        } else {
            // The same screen as the Classic menu's item (below).
            scene_manager_set_scene_state(app->scene_manager, RadioGeddonSceneStart, pos);
            return radiogeddon_scene_start_open(app, index);
        }
        // Back here, the dashboard selects the same position.
        scene_manager_set_scene_state(app->scene_manager, RadioGeddonSceneStart, pos);
        return true;
    }
#endif

    // Only menu indices are ours; ignore late events from a scene just closed.
    if(event.type == SceneManagerEventTypeCustom &&
       (event.event <= StartIndexSessions || event.event == StartIndexTip)) {
        scene_manager_set_scene_state(app->scene_manager, RadioGeddonSceneStart, event.event);
        radiogeddon_scene_start_open(app, event.event);
        consumed = true;
    }
    return consumed;
}

RG_UI_TEXT void radiogeddon_scene_start_on_exit(void* context) {
    RadioGeddonApp* app = context;
    submenu_reset(app->submenu);
}
