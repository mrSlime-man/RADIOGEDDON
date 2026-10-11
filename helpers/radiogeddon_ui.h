/**
 * @file radiogeddon_ui.h
 * @brief Dolphin Lab (Full edition): the bridge between the app and its
 * interface styles.
 *
 * Scenes report what really happens (radiogeddon_ui_event) and ask for
 * cards instead of popups; the app's 100 ms tick (radiogeddon_ui_tick)
 * reads the radio's real state (receiving, recording, memory), keeps the
 * mascot in step and redraws the lab view only while it is on screen and
 * only when its picture changed. No timer of its own and nothing that
 * touches the radio: the UI only reads.
 *
 * In Classic (chosen, or forced when memory is short at start) the lab view
 * does not exist, every card call returns false and the caller shows its
 * Beta 7 popup. The Catalog edition compiles all of this to no-ops.
 */
#pragma once

#include "../radiogeddon_edition.h"
#include <stdbool.h>
#include <stdint.h>
#include "rg_mascot.h"
#include "rg_ui.h"

typedef struct RadioGeddonApp RadioGeddonApp;

/* Custom events the dashboard posts to the start scene: the item's index,
 * plus this for a long OK. */
#define RADIOGEDDON_UI_SHORTCUT_BASE 50u

#if RG_FEATURE_DOLPHIN_LAB

#include "../views/radiogeddon_lab_view.h"

typedef struct {
    RadioGeddonLabView* lab; // NULL in Classic
    uint8_t style; // RgUiStyle in effect
    bool scanning; // a Scanner / Range Scanner / Waterfall sweep runs
    bool was_recording;
    bool home_shown; // the dashboard's entry transition plays once a run
    RgMascot mascot;
    RgUiFeedback feedback;
    // A card that closes by itself: when, and the event it then posts.
    uint32_t card_until;
    uint32_t card_event;
    bool card_timed;
    bool card_dismiss; // OK closes it early
    // An analysis reveal: the view it hands over to.
    bool reveal;
    uint32_t reveal_view;
    uint32_t reveal_until;
} RadioGeddonUi;

/** Set up from the settings (app start). */
void radiogeddon_ui_alloc(RadioGeddonApp* app);
void radiogeddon_ui_free(RadioGeddonApp* app);
/** The style setting changed: create or drop the lab view to match. */
void radiogeddon_ui_apply_style(RadioGeddonApp* app);

/** True when cards and the dashboard are in use (not Classic). */
bool radiogeddon_ui_active(RadioGeddonApp* app);

void radiogeddon_ui_event(RadioGeddonApp* app, RgMascotEvent event, uint32_t arg);
/** A sweep is collecting data (true) or stopped / paused (false). */
void radiogeddon_ui_set_scanning(RadioGeddonApp* app, bool scanning);
/** The app's 100 ms tick. */
void radiogeddon_ui_tick(RadioGeddonApp* app);

/**
 * The radio screens' header badge: false in Classic (no badge), else the
 * frame to draw and whether it is the Cyber Lab badge.
 */
bool radiogeddon_ui_badge(RadioGeddonApp* app, RgMascotFrame* frame, bool* cyber);

/**
 * Show a card; false in Classic (show the popup instead). With
 * @p timeout_ms, @p done_event is posted after that long, or at once on OK
 * when @p dismiss.
 */
bool radiogeddon_ui_card(
    RadioGeddonApp* app,
    RadioGeddonLabCard card,
    const char* title,
    const char* text,
    uint32_t timeout_ms,
    uint32_t done_event,
    bool dismiss);
/** Progress of the card shown (percent). */
void radiogeddon_ui_card_progress(RadioGeddonApp* app, uint8_t percent);

/**
 * Show a finished analysis: a short reveal of @p text's first lines, then
 * @p view (a ViewDispatcher id). Without the lab styles or transitions, the
 * view at once. Any key skips the reveal.
 */
void radiogeddon_ui_reveal(RadioGeddonApp* app, const char* title, const char* text, uint32_t view);
/** The scene is closing: forget a reveal or a card timer still running,
 * so no late event reaches the next scene. */
void radiogeddon_ui_cancel(RadioGeddonApp* app);

/** Show the dashboard with @p items (static), @p selected highlighted. */
void radiogeddon_ui_show_home(
    RadioGeddonApp* app,
    const RadioGeddonLabItem* items,
    uint8_t count,
    uint8_t selected,
    bool transition);

#else

static inline bool radiogeddon_ui_active(RadioGeddonApp* app) {
    (void)app;
    return false;
}
static inline void radiogeddon_ui_event(RadioGeddonApp* app, RgMascotEvent event, uint32_t arg) {
    (void)app;
    (void)event;
    (void)arg;
}
static inline void radiogeddon_ui_set_scanning(RadioGeddonApp* app, bool scanning) {
    (void)app;
    (void)scanning;
}
static inline void radiogeddon_ui_cancel(RadioGeddonApp* app) {
    (void)app;
}

#endif
