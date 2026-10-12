#include "radiogeddon_scene.h"
#include "../views/radiogeddon_journal_text.h"

#if RG_FEATURE_JOURNAL

/*
 * Full edition: the Research Journal. Every figure on it is a count of real
 * events the app recorded (helpers/rg_journal.h); nothing is computed from
 * the SD card here. It is in the lab module, with its text pages. Clearing
 * the history keeps the counters (and the XP); resetting
 * clears everything; recordings are never touched.
 *
 * Scene state: the menu (0), a text page (1) or a confirmation (2, 3).
 */

typedef enum {
    JournalItemJournal,
    JournalItemAchievements,
    JournalItemCustomize,
    JournalItemClear,
    JournalItemReset,
} JournalItem;

typedef enum {
    JournalModeMenu,
    JournalModeText,
    JournalModeConfirmClear,
    JournalModeConfirmReset,
} JournalMode;

typedef enum {
    JournalEventYes = 1400,
    JournalEventNo,
} JournalEvent;

#define JOURNAL_TEXT_SIZE 1600u

static char* journal_text;

static void journal_cb(void* context, uint32_t index) {
    RadioGeddonApp* app = context;
    view_dispatcher_send_custom_event(app->view_dispatcher, index);
}

static void journal_show_menu(RadioGeddonApp* app, uint32_t selected) {
    scene_manager_set_scene_state(app->scene_manager, RadioGeddonSceneJournal, JournalModeMenu);
    Submenu* submenu = app->submenu;
    submenu_reset(submenu);
    const RgJournal* j = &app->research.journal;
    bool xp_on = app->settings.research_xp;
    char header[32];
    if(xp_on) {
        uint32_t xp = rg_journal_xp(j);
        snprintf(
            header,
            sizeof(header),
            "Journal: Lv %u, %lu XP",
            (unsigned)rg_journal_level(xp),
            (unsigned long)xp);
    } else {
        strlcpy(header, "Research Journal", sizeof(header));
    }
    submenu_set_header(submenu, header);
    submenu_add_item(submenu, "History & statistics", JournalItemJournal, journal_cb, app);
    // Research XP off: no achievements to show.
    if(xp_on)
        submenu_add_item(
            submenu,
            app->research.fresh_achievements ? "Achievements (new!)" : "Achievements",
            JournalItemAchievements,
            journal_cb,
            app);
#if RG_FEATURE_DOLPHIN_LAB
    submenu_add_item(submenu, "Customize dolphin", JournalItemCustomize, journal_cb, app);
#endif
    submenu_add_item(submenu, "Clear history", JournalItemClear, journal_cb, app);
    submenu_add_item(submenu, "Reset all progress", JournalItemReset, journal_cb, app);
    submenu_set_selected_item(submenu, selected);
    view_dispatcher_switch_to_view(app->view_dispatcher, RadioGeddonViewSubmenu);
}

/* A text page. */
static void journal_show_page(RadioGeddonApp* app, bool achievements) {
    if(!journal_text) journal_text = malloc(JOURNAL_TEXT_SIZE);
    RadioGeddonResearch* r = &app->research;
    if(achievements) {
        radiogeddon_journal_achievements_text(
            &r->journal, r->fresh_achievements, journal_text, JOURNAL_TEXT_SIZE);
        r->fresh_achievements = 0; // seen
    } else {
        radiogeddon_journal_text(
            &r->journal, app->settings.research_xp, journal_text, JOURNAL_TEXT_SIZE);
    }
    scene_manager_set_scene_state(app->scene_manager, RadioGeddonSceneJournal, JournalModeText);
    text_box_reset(app->text_box);
    text_box_set_font(app->text_box, TextBoxFontText);
    text_box_set_text(app->text_box, journal_text);
    view_dispatcher_switch_to_view(app->view_dispatcher, RadioGeddonViewTextBox);
}

void radiogeddon_scene_journal_on_enter(void* context) {
    RadioGeddonApp* app = context;
    journal_show_menu(app, 0);
}

bool radiogeddon_scene_journal_on_event(void* context, SceneManagerEvent event) {
    RadioGeddonApp* app = context;
    uint32_t mode = scene_manager_get_scene_state(app->scene_manager, RadioGeddonSceneJournal);
    if(event.type == SceneManagerEventTypeBack) {
        if(mode != JournalModeMenu) {
            journal_show_menu(app, mode == JournalModeText ? 0 : JournalItemClear);
            return true;
        }
        return false;
    }
    if(event.type != SceneManagerEventTypeCustom) return false;
    switch(event.event) {
    case JournalItemJournal:
        journal_show_page(app, false);
        break;
    case JournalItemAchievements:
        journal_show_page(app, true);
        break;
#if RG_FEATURE_DOLPHIN_LAB
    case JournalItemCustomize:
        scene_manager_next_scene(app->scene_manager, RadioGeddonSceneCustomize);
        break;
#endif
    case JournalItemClear:
        scene_manager_set_scene_state(
            app->scene_manager, RadioGeddonSceneJournal, JournalModeConfirmClear);
        radiogeddon_scene_show_confirm(
            app,
            "Clear history?",
            "The list of recent events\ngoes; counts, XP and\nrecordings stay.",
            "Clear",
            JournalEventYes,
            JournalEventNo);
        break;
    case JournalItemReset:
        scene_manager_set_scene_state(
            app->scene_manager, RadioGeddonSceneJournal, JournalModeConfirmReset);
        radiogeddon_scene_show_confirm(
            app,
            "Reset progress?",
            "Journal, XP and badges\nstart over. Recordings\nare not touched.",
            "Reset",
            JournalEventYes,
            JournalEventNo);
        break;
    case JournalEventYes:
        if(!radiogeddon_research_clear(app, mode == JournalModeConfirmReset)) {
            radiogeddon_scene_show_message(app, "Not saved", "Check the SD card.");
        } else {
            notification_message(app->notifications, &sequence_success);
            journal_show_menu(app, 0);
        }
        break;
    case JournalEventNo:
        journal_show_menu(
            app, mode == JournalModeConfirmReset ? JournalItemReset : JournalItemClear);
        break;
    default:
        break;
    }
    return true;
}

void radiogeddon_scene_journal_on_exit(void* context) {
    RadioGeddonApp* app = context;
    submenu_reset(app->submenu);
    text_box_reset(app->text_box);
    widget_reset(app->widget);
    free(journal_text);
    journal_text = NULL;
}

#endif
