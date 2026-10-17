#include "radiogeddon_scene.h"

#if RG_FEATURE_ASSISTANT

/*
 * Full edition: a Dolphin Assistant tip (helpers/rg_assist.h), opened from
 * the main menu. It says what it suggests and on which recording, and Go
 * opens that tool on the recording in context without asking for the file
 * again; Dismiss hides the tip until another recording is in context. A tip
 * never transmits and never says what a signal is or whose it is.
 */

typedef enum {
    AssistantEventGo = 1500,
    AssistantEventDismiss,
} AssistantEvent;

static RG_UI_TEXT void assistant_button(GuiButtonType result, InputType type, void* context) {
    RadioGeddonApp* app = context;
    if(type != InputTypeShort) return;
    view_dispatcher_send_custom_event(
        app->view_dispatcher,
        result == GuiButtonTypeRight ? AssistantEventGo : AssistantEventDismiss);
}

RG_UI_TEXT void radiogeddon_scene_assistant_on_enter(void* context) {
    RadioGeddonApp* app = context;
    RgTip tip = (RgTip)app->research.tip;
    Widget* widget = app->widget;
    widget_reset(widget);
    widget_add_string_element(
        widget, 64, 2, AlignCenter, AlignTop, FontPrimary, "Dolphin Assistant");
    furi_string_set(app->temp_str, rg_tip_text(tip));
    if(app->research.ctx_name[0]) {
        furi_string_cat_printf(app->temp_str, "\n%s", app->research.ctx_name);
    }
    widget_add_text_box_element(
        widget,
        0,
        14,
        128,
        36,
        AlignCenter,
        AlignCenter,
        furi_string_get_cstr(app->temp_str),
        false);
    widget_add_button_element(widget, GuiButtonTypeLeft, "Dismiss", assistant_button, app);
    widget_add_button_element(widget, GuiButtonTypeRight, "Go", assistant_button, app);
    view_dispatcher_switch_to_view(app->view_dispatcher, RadioGeddonViewWidget);
}

/* The recording in context into app->loaded / file_path. */
static RG_UI_TEXT bool assistant_load(RadioGeddonApp* app) {
    if(!radiogeddon_research_context_path(app, app->file_path)) return false;
    radiogeddon_loaded_signal_reset(&app->loaded);
    radiogeddon_loaded_signal_init(&app->loaded);
    bool ok =
        radiogeddon_storage_load(app->storage, furi_string_get_cstr(app->file_path), &app->loaded);
    app->file_damaged = !ok;
    app->have_loaded_signal = ok;
    return ok;
}

/* The tool replaces this card: Back from it returns to the main menu. */
static RG_UI_TEXT void assistant_open(SceneManager* sm, uint32_t scene) {
    scene_manager_search_and_switch_to_another_scene(sm, scene);
}

static RG_UI_TEXT void assistant_go(RadioGeddonApp* app, RgTipAction action) {
    SceneManager* sm = app->scene_manager;
    switch(action) {
    case RgTipActionSettings:
        scene_manager_set_scene_state(sm, RadioGeddonSceneConfig, 0);
        assistant_open(sm, RadioGeddonSceneConfig);
        return;
    case RgTipActionReceive:
        assistant_open(sm, RadioGeddonSceneReceiver);
        return;
    case RgTipActionSessionReport:
#if RG_FEATURE_SESSIONS
        scene_manager_set_scene_state(sm, RadioGeddonSceneSessions, 0);
        assistant_open(sm, RadioGeddonSceneSessions);
#endif
        return;
    default:
        break;
    }
    if(!assistant_load(app)) {
        radiogeddon_scene_show_message(
            app, "Not found", "The recording is gone\nor cannot be read.");
        return;
    }
    switch(action) {
    case RgTipActionDiscovery:
        assistant_open(sm, RadioGeddonSceneUnknown);
        break;
#if RG_FEATURE_FINGERPRINT
    case RgTipActionSimilar:
        scene_manager_set_scene_state(sm, RadioGeddonSceneSimilar, 0);
        assistant_open(sm, RadioGeddonSceneSimilar);
        break;
#endif
#if RG_FEATURE_PULSE_STUDIO
    case RgTipActionStudio:
        assistant_open(sm, RadioGeddonSceneStudio);
        break;
#endif
#if RG_FEATURE_MULTI_COMPARE
    case RgTipActionMultiCompare: {
        radiogeddon_multi_clear(app);
        radiogeddon_multi_add(app, furi_string_get_cstr(app->file_path));
        FuriString* path = furi_string_alloc();
        for(size_t i = 0; i < RADIOGEDDON_RESEARCH_SIMILAR; i++) {
            if(!app->research.similar[i][0]) continue;
            furi_string_printf(
                path, "%s/%s", RADIOGEDDON_SIGNALS_FOLDER, app->research.similar[i]);
            radiogeddon_multi_add(app, furi_string_get_cstr(path));
        }
        furi_string_free(path);
        scene_manager_set_scene_state(sm, RadioGeddonSceneMulti, 0);
        assistant_open(sm, RadioGeddonSceneMulti);
        break;
    }
#endif
#if RG_FEATURE_SESSIONS
    case RgTipActionSessionAdd: {
        char session[RG_SESSION_NAME_MAX];
        if(radiogeddon_session_capture_saved(
               app->storage, app->research.ctx_name, session, sizeof(session))) {
            radiogeddon_research_session_added(app);
            radiogeddon_research_step(
                app, furi_string_get_cstr(app->file_path), RadioGeddonStepInSession, 0);
            notification_message(app->notifications, &sequence_success);
            scene_manager_previous_scene(sm);
        } else {
            radiogeddon_scene_show_message(
                app, "Not added", "No active session, or\nthe SD card refused.");
        }
        break;
    }
#endif
    default:
        scene_manager_previous_scene(sm);
        break;
    }
}

RG_UI_TEXT bool radiogeddon_scene_assistant_on_event(void* context, SceneManagerEvent event) {
    RadioGeddonApp* app = context;
    if(event.type != SceneManagerEventTypeCustom) return false;
    if(event.event == AssistantEventDismiss) {
        radiogeddon_research_dismiss_tip(app);
        scene_manager_previous_scene(app->scene_manager);
        return true;
    }
    if(event.event == AssistantEventGo) {
        assistant_go(app, rg_tip_action((RgTip)app->research.tip));
        return true;
    }
    return false;
}

RG_UI_TEXT void radiogeddon_scene_assistant_on_exit(void* context) {
    RadioGeddonApp* app = context;
    widget_reset(app->widget);
}

#endif
