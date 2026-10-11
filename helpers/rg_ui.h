/**
 * @file rg_ui.h
 * @brief Dolphin Lab: the interface's pure logic (Full edition).
 *
 * UI styles and their fallback, the feedback rate limiter, screen
 * transitions and their dither, the dashboard's menu window, text wrapping
 * for message cards and the firmware family name. Pure C with no SDK
 * headers; host-tested in test/test_mascot.c.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* ---- styles -------------------------------------------------------------- */

/** The "UI style" setting. Stored by number: append, never reorder. */
typedef enum {
    RgUiStyleDolphinLab, // mascot dashboard, cards, reactions, transitions
    RgUiStyleCyberLab, // technical dashboard, minimal mascot
    RgUiStyleClassic, // Beta 7 menus and popups: no mascot, no animation
    RgUiStyleCount,
} RgUiStyle;

/** The "UI feedback" setting. */
typedef enum {
    RgUiFeedbackOff, // only the notifications RadioGeddon always gave
    RgUiFeedbackMinimal, // + a short vibration on a few milestones
    RgUiFeedbackFull, // + short sounds, menu clicks and signal blips
    RgUiFeedbackCount,
} RgUiFeedbackLevel;

/* Below this much free heap at start the session runs in Classic: nothing
 * decorative is allocated. */
#define RG_UI_MIN_FREE_HEAP (8u * 1024u)

const char* rg_ui_style_name(RgUiStyle style);
const char* rg_ui_feedback_name(RgUiFeedbackLevel level);

/** The style to run with: @p saved, or Classic when memory is short. */
RgUiStyle rg_ui_style_effective(uint32_t saved, uint32_t free_heap);

/* ---- feedback -------------------------------------------------------------- */

/** Moments that may give feedback. */
typedef enum {
    RgUiFbMenu, // a dashboard item chosen
    RgUiFbSignal, // activity detected
    RgUiFbRecordStart,
    RgUiFbRecordSaved,
    RgUiFbAnalysisDone,
    RgUiFbError,
    RgUiFbCount,
} RgUiFbEvent;

/** What to play. */
typedef enum {
    RgUiCueNone,
    RgUiCueTick, // a very short vibration
    RgUiCueClick, // a very short, quiet note
    RgUiCueBlip, // a short note
    RgUiCueRecord, // vibration and a rising pair of notes
    RgUiCueChime, // two notes
    RgUiCueCount,
} RgUiCue;

/* Shortest time between two cues of the same moment, and between any two. */
#define RG_UI_FB_SIGNAL_MS 2000u
#define RG_UI_FB_MENU_MS   80u
#define RG_UI_FB_OTHER_MS  500u
#define RG_UI_FB_ANY_MS    60u

typedef struct {
    uint32_t last[RgUiFbCount];
    uint32_t last_any;
    uint8_t played[RgUiFbCount];
    uint8_t played_any;
} RgUiFeedback;

void rg_ui_feedback_init(RgUiFeedback* fb);

/**
 * The cue for @p event at @p now under @p level, or RgUiCueNone (not at
 * this level, already covered by the app's own notification, or too soon
 * after the last one: repeated events never pile up).
 */
RgUiCue rg_ui_feedback(RgUiFeedback* fb, RgUiFeedbackLevel level, RgUiFbEvent event, uint32_t now);

/* ---- transitions ----------------------------------------------------------- */

typedef enum {
    RgTransitionNone,
    RgTransitionWipe, // an oscilloscope line sweeps across, revealing
    RgTransitionDither, // an ordered-dither fade in
    RgTransitionSlide, // the new content slides in from the right
    RgTransitionCount,
} RgTransitionKind;

#define RG_TRANSITION_DONE 255u

typedef struct {
    uint32_t start;
    uint16_t duration_ms;
    uint8_t kind;
    uint8_t active;
} RgTransition;

void rg_transition_start(
    RgTransition* t,
    RgTransitionKind kind,
    uint16_t duration_ms,
    uint32_t now);
/** End at once (a key was pressed: input never waits for an animation). */
void rg_transition_finish(RgTransition* t);
/** How much is revealed at @p now: 0..255; 255 (and inactive) when done. */
uint8_t rg_transition_level(RgTransition* t, uint32_t now);

/** Ordered (4x4 Bayer) dither: true if pixel (x, y) is still hidden at @p level. */
bool rg_dither_hidden(uint32_t x, uint32_t y, uint8_t level);

/* ---- menu ------------------------------------------------------------------ */

/** First visible row so that @p selected shows in a window of @p rows. */
uint8_t rg_ui_menu_top(uint8_t selected, uint8_t top, uint8_t count, uint8_t rows);

/* ---- text ------------------------------------------------------------------ */

/** Width in pixels of the first @p len characters of @p text. */
typedef uint16_t (*RgUiWidthFn)(void* context, const char* text, size_t len);

typedef struct {
    uint16_t start;
    uint16_t len;
} RgUiLine;

/**
 * Break @p text into lines no wider than @p max_w: at its own line breaks,
 * then between words, and inside a word only when the word alone is too
 * wide. Fills at most @p max_lines; returns how many lines the whole text
 * needs (more than @p max_lines when it does not fit).
 */
size_t rg_ui_wrap(
    const char* text,
    uint16_t max_w,
    RgUiWidthFn width,
    void* context,
    RgUiLine* lines,
    size_t max_lines);

/**
 * True if a message with @p header reports something done ("Report saved",
 * "Session created") rather than a refusal or error ("Not saved", "Save
 * failed"): the mascot cheers instead of worrying. The text is the same.
 */
bool rg_ui_message_is_notice(const char* header);

/* ---- oscilloscope trace ---------------------------------------------------- */

/**
 * The level recorded at the middle of each of @p cols columns spread over
 * the @p n RAW samples (positive: carrier on for that many microseconds,
 * negative: off), as bits in @p bits (bit c of byte c / 8 set: on). Nothing
 * is interpolated: each column shows the sample that covers its middle.
 * Returns the time the samples span (us); 0 when there is nothing to show.
 */
uint64_t rg_ui_trace(const int32_t* samples, size_t n, uint32_t cols, uint8_t* bits);

/* ---- firmware -------------------------------------------------------------- */

/** "Official", "Momentum", "Unleashed", "RogueMaster" or "Other", from the
 * firmware's version string (version_get_version). */
const char* rg_ui_fw_family(const char* version);
