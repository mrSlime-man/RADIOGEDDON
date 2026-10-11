#include "radiogeddon_ui.h"

#if RG_FEATURE_DOLPHIN_LAB

#include "../radiogeddon.h"
#include "../scenes/radiogeddon_scene.h"
#include "rg_freq.h"

/* How long a reveal shows before the report, and its wipe. */
#define UI_REVEAL_MS      700u
#define UI_REVEAL_WIPE_MS 400u
/* The dashboard's entry wipe. */
#define UI_HOME_WIPE_MS   300u

/* Short cues for UI feedback (rg_ui_feedback picks one, rate limited). */
static const NotificationSequence ui_seq_tick = {
    &message_vibro_on,
    &message_delay_10,
    &message_vibro_off,
    NULL,
};
static const NotificationSequence ui_seq_click = {
    &message_note_e6,
    &message_delay_10,
    &message_sound_off,
    NULL,
};
static const NotificationSequence ui_seq_blip = {
    &message_note_a6,
    &message_delay_25,
    &message_sound_off,
    NULL,
};
static const NotificationSequence ui_seq_record = {
    &message_vibro_on,
    &message_note_c6,
    &message_delay_50,
    &message_note_e6,
    &message_delay_50,
    &message_sound_off,
    &message_vibro_off,
    NULL,
};
static const NotificationSequence ui_seq_chime = {
    &message_note_e6,
    &message_delay_50,
    &message_note_a6,
    &message_delay_100,
    &message_sound_off,
    NULL,
};
static const NotificationSequence* const ui_cues[RgUiCueCount] = {
    [RgUiCueTick] = &ui_seq_tick,
    [RgUiCueClick] = &ui_seq_click,
    [RgUiCueBlip] = &ui_seq_blip,
    [RgUiCueRecord] = &ui_seq_record,
    [RgUiCueChime] = &ui_seq_chime,
};

/* Milliseconds (the Flipper's kernel tick is 1 ms). */
static uint32_t ui_now(void) {
    return furi_get_tick();
}

static void ui_play(RadioGeddonApp* app, RgUiFbEvent event) {
    RgUiCue cue = rg_ui_feedback(
        &app->ui.feedback, (RgUiFeedbackLevel)app->settings.ui_feedback, event, ui_now());
    if(cue != RgUiCueNone && cue < RgUiCueCount && ui_cues[cue])
        notification_message(app->notifications, ui_cues[cue]);
}

static void ui_reveal_finish(RadioGeddonApp* app) {
    if(!app->ui.reveal) return;
    app->ui.reveal = false;
    view_dispatcher_switch_to_view(app->view_dispatcher, app->ui.reveal_view);
}

static void ui_lab_cb(RadioGeddonLabEvent event, uint32_t index, void* context) {
    RadioGeddonApp* app = context;
    RadioGeddonUi* ui = &app->ui;
    switch(event) {
    case RadioGeddonLabEventKey:
        rg_mascot_event(&ui->mascot, RgMascotEventUserInput, 0, ui_now());
        // Any key skips a reveal: input never waits for an animation.
        ui_reveal_finish(app);
        break;
    case RadioGeddonLabEventSelect:
        ui_play(app, RgUiFbMenu);
        view_dispatcher_send_custom_event(app->view_dispatcher, index);
        break;
    case RadioGeddonLabEventShortcut:
        ui_play(app, RgUiFbMenu);
        view_dispatcher_send_custom_event(
            app->view_dispatcher, RADIOGEDDON_UI_SHORTCUT_BASE + index);
        break;
    case RadioGeddonLabEventDismiss:
        if(ui->reveal) {
            ui_reveal_finish(app);
        } else if(ui->card_dismiss) {
            ui->card_timed = false;
            ui->card_dismiss = false;
            view_dispatcher_send_custom_event(app->view_dispatcher, ui->card_event);
        }
        break;
    }
}

/* The look and animation level from the settings; with memory short the
 * animation stops (a still pose costs no redraws). */
static void ui_apply_look(RadioGeddonApp* app, bool low) {
    RgMascotLook look;
    memcpy(&look, app->settings.look, sizeof(look));
    rg_mascot_set_look(
        &app->ui.mascot, &look, low ? (uint8_t)RgAnimOff : app->settings.ui_animation);
}

static void ui_lab_alloc(RadioGeddonApp* app) {
    if(app->ui.lab) return;
    app->ui.lab = radiogeddon_lab_view_alloc();
    radiogeddon_lab_view_set_callback(app->ui.lab, ui_lab_cb, app);
    radiogeddon_lab_view_set_style(app->ui.lab, (RgUiStyle)app->ui.style);
    view_dispatcher_add_view(
        app->view_dispatcher, RadioGeddonViewLab, radiogeddon_lab_view_get_view(app->ui.lab));
}

static void ui_lab_free(RadioGeddonApp* app) {
    if(!app->ui.lab) return;
    view_dispatcher_remove_view(app->view_dispatcher, RadioGeddonViewLab);
    radiogeddon_lab_view_free(app->ui.lab);
    app->ui.lab = NULL;
}

void radiogeddon_ui_alloc(RadioGeddonApp* app) {
    RadioGeddonUi* ui = &app->ui;
    memset(ui, 0, sizeof(*ui));
    uint32_t now = ui_now();
    rg_mascot_init(&ui->mascot, now, now ^ app->fw_tag);
    ui_apply_look(app, false);
    rg_ui_feedback_init(&ui->feedback);
    // Short of memory at start: Classic for this run, nothing decorative.
    ui->style = (uint8_t)rg_ui_style_effective(app->settings.ui_style, radiogeddon_memdiag_free());
    if(ui->style != RgUiStyleClassic) ui_lab_alloc(app);
}

void radiogeddon_ui_free(RadioGeddonApp* app) {
    ui_lab_free(app);
}

void radiogeddon_ui_apply_style(RadioGeddonApp* app) {
    RadioGeddonUi* ui = &app->ui;
    ui->style = (uint8_t)rg_ui_style_effective(app->settings.ui_style, radiogeddon_memdiag_free());
    ui->reveal = false;
    ui->card_timed = false;
    ui_apply_look(app, ui->mascot.low_memory);
    if(ui->style == RgUiStyleClassic) {
        ui_lab_free(app);
    } else {
        ui_lab_alloc(app);
        radiogeddon_lab_view_set_style(ui->lab, (RgUiStyle)ui->style);
    }
}

bool radiogeddon_ui_active(RadioGeddonApp* app) {
    return app->ui.lab != NULL;
}

void radiogeddon_ui_event(RadioGeddonApp* app, RgMascotEvent event, uint32_t arg) {
    rg_mascot_event(&app->ui.mascot, event, arg, ui_now());
    switch(event) {
    case RgMascotEventSignalActivity:
        ui_play(app, RgUiFbSignal);
        break;
    case RgMascotEventAnalysisCompleted:
        ui_play(app, RgUiFbAnalysisDone);
        break;
    case RgMascotEventSaveCompleted:
        ui_play(app, RgUiFbRecordSaved);
        break;
    case RgMascotEventOperationFailed:
        ui_play(app, RgUiFbError);
        break;
    default:
        break;
    }
}

void radiogeddon_ui_set_scanning(RadioGeddonApp* app, bool scanning) {
    app->ui.scanning = scanning;
}

void radiogeddon_ui_tick(RadioGeddonApp* app) {
    RadioGeddonUi* ui = &app->ui;
    uint32_t now = ui_now();
    // The real state, read: the radio's session, the recorder, the heap.
    bool receiving = radiogeddon_subghz_is_rx_running(app->subghz);
    bool recording = radiogeddon_subghz_is_recording(app->subghz);
    // Memory is "low" when a receive session would be refused now; while
    // the radio runs, its own session is what uses the memory.
    bool low = ui->mascot.low_memory;
    if(!receiving && !recording && !ui->scanning) low = !radiogeddon_scene_decoders_fit(app);
    if(recording && !ui->was_recording) ui_play(app, RgUiFbRecordStart);
    ui->was_recording = recording;
    if(low != (ui->mascot.low_memory != 0)) ui_apply_look(app, low);
    rg_mascot_sync(&ui->mascot, ui->scanning, receiving, recording, low, now);

    if(!ui->lab) return;
    if(ui->card_timed && (int32_t)(now - ui->card_until) >= 0) {
        ui->card_timed = false;
        ui->card_dismiss = false;
        view_dispatcher_send_custom_event(app->view_dispatcher, ui->card_event);
    }
    if(ui->reveal && (int32_t)(now - ui->reveal_until) >= 0) ui_reveal_finish(app);
    // Nothing is computed for a view that is not on screen.
    if(radiogeddon_lab_view_is_visible(ui->lab)) {
        RgMascotFrame f;
        rg_mascot_frame(&ui->mascot, now, &f);
        radiogeddon_lab_view_update(ui->lab, &f, now);
    }
}

bool radiogeddon_ui_badge(RadioGeddonApp* app, RgMascotFrame* frame, bool* cyber) {
    if(!app->ui.lab) return false;
    rg_mascot_frame(&app->ui.mascot, ui_now(), frame);
    *cyber = app->ui.style == RgUiStyleCyberLab;
    return true;
}

static void ui_show_lab(RadioGeddonApp* app) {
    uint32_t now = ui_now();
    RgMascotFrame f;
    rg_mascot_frame(&app->ui.mascot, now, &f);
    radiogeddon_lab_view_update(app->ui.lab, &f, now);
    view_dispatcher_switch_to_view(app->view_dispatcher, RadioGeddonViewLab);
}

bool radiogeddon_ui_card(
    RadioGeddonApp* app,
    RadioGeddonLabCard card,
    const char* title,
    const char* text,
    uint32_t timeout_ms,
    uint32_t done_event,
    bool dismiss) {
    RadioGeddonUi* ui = &app->ui;
    if(!ui->lab) return false;
    ui->reveal = false;
    ui->card_timed = timeout_ms > 0;
    ui->card_until = ui_now() + timeout_ms;
    ui->card_event = done_event;
    ui->card_dismiss = dismiss;
    radiogeddon_lab_view_show_card(ui->lab, card, title, text);
    ui_show_lab(app);
    return true;
}

void radiogeddon_ui_card_progress(RadioGeddonApp* app, uint8_t percent) {
    if(!app->ui.lab) return;
    // The app thread is busy with the work: the frame advances here.
    uint32_t now = ui_now();
    rg_mascot_event(&app->ui.mascot, RgMascotEventAnalysisProgress, percent, now);
    RgMascotFrame f;
    rg_mascot_frame(&app->ui.mascot, now, &f);
    radiogeddon_lab_view_update(app->ui.lab, &f, now);
    radiogeddon_lab_view_set_progress(app->ui.lab, percent);
}

void radiogeddon_ui_reveal(RadioGeddonApp* app, const char* title, const char* text, uint32_t view) {
    RadioGeddonUi* ui = &app->ui;
    radiogeddon_ui_event(app, RgMascotEventAnalysisCompleted, 0);
    if(!ui->lab || !app->settings.ui_transitions) {
        view_dispatcher_switch_to_view(app->view_dispatcher, view);
        return;
    }
    uint32_t now = ui_now();
    ui->card_timed = false;
    ui->card_dismiss = false;
    radiogeddon_lab_view_show_card(ui->lab, RadioGeddonLabCardReveal, title, text);
    radiogeddon_lab_view_transition(ui->lab, RgTransitionWipe, UI_REVEAL_WIPE_MS, now);
    ui->reveal = true;
    ui->reveal_view = view;
    ui->reveal_until = now + UI_REVEAL_MS;
    ui_show_lab(app);
}

void radiogeddon_ui_cancel(RadioGeddonApp* app) {
    app->ui.reveal = false;
    app->ui.card_timed = false;
    app->ui.card_dismiss = false;
}

void radiogeddon_ui_show_home(
    RadioGeddonApp* app,
    const RadioGeddonLabItem* items,
    uint8_t count,
    uint8_t selected,
    bool transition) {
    RadioGeddonUi* ui = &app->ui;
    if(!ui->lab) return;
    ui->card_timed = false;
    ui->reveal = false;
    // At the main menu no tool runs: no sweep, no analysis (a safety net
    // should a screen have closed without saying so).
    ui->scanning = false;
    if(ui->mascot.analyzing)
        rg_mascot_event(&ui->mascot, RgMascotEventAnalysisCompleted, 0, ui_now());
    bool cyber = ui->style == RgUiStyleCyberLab;
    bool external = radiogeddon_subghz_get_radio(app->subghz) == RadioGeddonRadioExternal;

    // Status bar from cached state; the active session is one small file.
    char freq[RG_FREQ_TEXT_SIZE];
    rg_freq_text(app->frequency, freq, sizeof(freq));
    char left[24];
    snprintf(
        left,
        sizeof(left),
        "%s %s%s",
        freq,
        radiogeddon_presets[app->preset_index].label,
        cyber ? (external ? " EXT" : " INT") : (external ? " EXT" : ""));
    char right[20] = "";
    bool warn = false;
    if(!radiogeddon_scene_decoders_fit(app)) {
        strlcpy(right, "MEM LOW", sizeof(right));
        warn = true;
    } else {
#if RG_FEATURE_SESSIONS
        char session[RG_SESSION_NAME_MAX];
        radiogeddon_session_active(app->storage, session, sizeof(session));
        if(session[0]) {
            // The status bar trims what does not fit.
            strlcpy(right, "S:", sizeof(right));
            strlcat(right, session, sizeof(right));
        }
#endif
#if RG_FEATURE_JOURNAL
        // A badge earned and not yet seen in the Research Journal.
        if(!right[0] && app->research.fresh_achievements && app->settings.research_xp)
            strlcpy(right, "New badge!", sizeof(right));
#endif
        if(!right[0])
            strlcpy(right, cyber ? RG_EDITION_NAME " EDITION" : RG_EDITION_NAME, sizeof(right));
    }
#if RG_FEATURE_JOURNAL
    // A decoration shows only while its level is reached.
    uint8_t level = rg_journal_unlock_level(&app->research.journal, app->settings.research_xp);
    radiogeddon_lab_view_set_decoration(
        ui->lab,
        rg_decoration_unlocked((RgDecoration)app->settings.decoration, level) ?
            app->settings.decoration :
            0);
#endif
    radiogeddon_lab_view_set_status(ui->lab, left, right, warn);
    char readout[24];
    snprintf(
        readout, sizeof(readout), "FREE %luK", (unsigned long)(radiogeddon_memdiag_free() / 1024u));
    radiogeddon_lab_view_set_readout(ui->lab, readout);
    radiogeddon_lab_view_set_style(ui->lab, (RgUiStyle)ui->style);
    radiogeddon_lab_view_show_home(ui->lab, items, count, selected);
    if(transition && !ui->home_shown && app->settings.ui_transitions)
        radiogeddon_lab_view_transition(ui->lab, RgTransitionWipe, UI_HOME_WIPE_MS, ui_now());
    ui->home_shown = true;
    ui_show_lab(app);
}

#endif
