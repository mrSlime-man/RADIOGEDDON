/**
 * Offline UI tests for Dolphin Lab (make -C test ui): the dashboard in both
 * lab styles, every mascot state, the cards that replace popups (every
 * message the app shows, progress, saves, reveals) and the transitions,
 * drawn into the host 128x64 canvas with the firmware's own fonts
 * (test/ui/canvas_host.c) and driven with key events.
 *
 * Checked on every screen: nothing drawn off the screen, no text past an
 * edge, every message's text shown whole; the menu's navigation, events and
 * window; the view asking for redraws only when something changed. Each
 * state is written under build/ui/previews as a SYNTHETIC development
 * preview (drawing code fed with synthetic data on the host), never a
 * device screenshot and never for the Apps Catalog.
 */
#include "ui/canvas_host.h"
#include "../views/radiogeddon_lab_view.h"
#include "../views/radiogeddon_mascot_draw.h"
#include "../views/radiogeddon_room_view.h"
#include "../views/radiogeddon_scanner_view.h"
#include "../views/radiogeddon_receiver_view.h"
#include "../views/radiogeddon_spectrum_view.h"
#include "../views/radiogeddon_waterfall_view.h"
#include "../views/radiogeddon_studio_view.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static int g_failures = 0;
static int g_checks = 0;

#define CHECK(cond, msg)                                             \
    do {                                                             \
        g_checks++;                                                  \
        if(!(cond)) {                                                \
            g_failures++;                                            \
            printf("  FAIL: %s (%s:%d)\n", msg, __FILE__, __LINE__); \
        }                                                            \
    } while(0)

#ifndef PREVIEW_DIR
#define PREVIEW_DIR "build/ui/previews"
#endif

static int g_previews = 0;

/* Draw, check the screen's edges, keep a preview when @p name is set. */
static bool render(View* view, const char* name) {
    Canvas* c = canvas_host();
    canvas_host_reset(c);
    view_host_draw(view, c);
    bool ok = canvas_host_clipped(c) == 0 && canvas_host_text_overflows(c) == 0;
    if(name) {
        char msg[200];
        snprintf(msg, sizeof(msg), "%s: no pixel off the screen", name);
        CHECK(canvas_host_clipped(c) == 0, msg);
        snprintf(
            msg, sizeof(msg), "%s: no text past the edge (%s)", name, canvas_host_last_overflow(c));
        CHECK(canvas_host_text_overflows(c) == 0, msg);
        char path[160];
        snprintf(path, sizeof(path), PREVIEW_DIR "/synthetic_%s.pbm", name);
        if(canvas_host_write_pbm(c, path)) g_previews++;
    }
    return ok;
}

static const RadioGeddonLabItem g_items[] = {
    {"Scan", RgSpriteIconScan},
    {"Receive", RgSpriteIconReceive},
    {"Analyze", RgSpriteIconAnalyze},
    {"Database", RgSpriteIconDatabase},
    {"Sessions", RgSpriteIconSessions},
    {"Dolphin Lab", RgSpriteIconLab},
    {"Settings", RgSpriteIconSettings},
    {"About", RgSpriteIconAbout},
};
#define ITEMS (uint8_t)(sizeof(g_items) / sizeof(g_items[0]))

static int g_events[4];
static uint32_t g_last_index;
static void lab_cb(RadioGeddonLabEvent event, uint32_t index, void* context) {
    (void)context;
    if((unsigned)event < 4) g_events[event]++;
    g_last_index = index;
}

static void frame_of(RgMascotState s, uint32_t t, RgMascotFrame* f) {
    rg_mascot_pose(s, t, 5, s == RgMascotStateAnalyzing ? 40 : RG_MASCOT_PROGRESS_NONE, false, f);
}

/* ---- dashboard -------------------------------------------------------------- */

static void test_home(void) {
    printf("test_home\n");
    RadioGeddonLabView* v = radiogeddon_lab_view_alloc();
    View* view = radiogeddon_lab_view_get_view(v);
    radiogeddon_lab_view_set_callback(v, lab_cb, NULL);
    radiogeddon_lab_view_set_style(v, RgUiStyleDolphinLab);
    radiogeddon_lab_view_show_home(v, g_items, ITEMS, 0);
    radiogeddon_lab_view_set_status(v, "433.92 AM650", "Full", false);

    CHECK(!radiogeddon_lab_view_is_visible(v), "not visible before entering");
    view_host_enter(view);
    CHECK(radiogeddon_lab_view_is_visible(v), "visible once entered");

    // Every state of the mascot on the dashboard, at several moments.
    static const struct {
        RgMascotState s;
        const char* name;
    } states[] = {
        {RgMascotStateIdle, "home_idle"},
        {RgMascotStateScanning, "home_scanning"},
        {RgMascotStateDetected, "home_signal_detected"},
        {RgMascotStateReceiving, "home_receiving"},
        {RgMascotStateRecording, "home_recording"},
        {RgMascotStateAnalyzing, "home_analyzing"},
        {RgMascotStateSaved, "home_save_success"},
        {RgMascotStateError, "home_error"},
        {RgMascotStateLowMemory, "home_low_memory"},
        {RgMascotStateBored, "home_bored"},
        {RgMascotStateAsleep, "home_asleep"},
    };
    bool all_inside = true;
    for(size_t i = 0; i < sizeof(states) / sizeof(states[0]); i++) {
        for(uint32_t t = 0; t < 12000; t += 100) {
            RgMascotFrame f;
            frame_of(states[i].s, t, &f);
            radiogeddon_lab_view_update(v, &f, 1000000 + t);
            if(!render(view, NULL)) all_inside = false;
        }
        RgMascotFrame f;
        frame_of(states[i].s, states[i].s == RgMascotStateAsleep ? 1100 : 0, &f);
        radiogeddon_lab_view_update(v, &f, 2000000);
        render(view, states[i].name);
        CHECK(canvas_host_count(canvas_host(), 0, 0, 48, 53) > 120, "the lab scene is drawn");
        CHECK(canvas_host_count(canvas_host(), 50, 0, 78, 52) > 100, "the menu is drawn");
    }
    CHECK(all_inside, "every frame of every state inside the screen");

    // Navigation: wrap, page, events.
    memset(g_events, 0, sizeof(g_events));
    view_host_input(view, InputKeyUp, InputTypeShort);
    CHECK(radiogeddon_lab_view_get_selected(v) == ITEMS - 1, "Up from the top wraps");
    render(view, "home_scrolled");
    view_host_input(view, InputKeyDown, InputTypeShort);
    CHECK(radiogeddon_lab_view_get_selected(v) == 0, "Down from the bottom wraps");
    view_host_input(view, InputKeyRight, InputTypeShort);
    CHECK(radiogeddon_lab_view_get_selected(v) == 5, "Right: a page down");
    view_host_input(view, InputKeyRight, InputTypeShort);
    CHECK(radiogeddon_lab_view_get_selected(v) == ITEMS - 1, "stops at the last");
    view_host_input(view, InputKeyLeft, InputTypeShort);
    CHECK(radiogeddon_lab_view_get_selected(v) == 2, "Left: a page up");
    view_host_input(view, InputKeyDown, InputTypeRepeat);
    CHECK(radiogeddon_lab_view_get_selected(v) == 3, "held Down repeats");
    CHECK(view_host_input(view, InputKeyOk, InputTypeShort), "OK consumed");
    CHECK(g_events[RadioGeddonLabEventSelect] == 1 && g_last_index == 3, "OK selects item 3");
    CHECK(view_host_input(view, InputKeyOk, InputTypeLong), "long OK consumed");
    CHECK(g_events[RadioGeddonLabEventShortcut] == 1 && g_last_index == 3, "long OK: shortcut");
    CHECK(!view_host_input(view, InputKeyBack, InputTypeShort), "Back left to the scene");
    CHECK(g_events[RadioGeddonLabEventKey] >= 7, "keys reported (the mascot wakes)");
    CHECK(!view_host_input(view, InputKeyOk, InputTypePress), "raw press ignored");

    // Redraws only when something changed.
    RgMascotFrame f;
    frame_of(RgMascotStateIdle, 0, &f);
    radiogeddon_lab_view_update(v, &f, 3000000);
    view_host_updates(view);
    radiogeddon_lab_view_update(v, &f, 3000100);
    CHECK(view_host_updates(view) == 0, "same frame: no redraw");
    frame_of(RgMascotStateSaved, 0, &f);
    radiogeddon_lab_view_update(v, &f, 3000200);
    CHECK(view_host_updates(view) == 1, "new frame: one redraw");
    // Idle animation over a minute asks for few redraws.
    uint32_t redraws = 0;
    for(uint32_t t = 0; t < 60000; t += 100) {
        frame_of(RgMascotStateIdle, t, &f);
        radiogeddon_lab_view_update(v, &f, 4000000 + t);
        redraws += view_host_updates(view);
    }
    printf("  idle: %u redraws in 60 s of 100 ms ticks\n", (unsigned)redraws);
    CHECK(redraws < 200, "idle redraws at well under the tick rate");

    // A long status and an empty menu stay inside the screen.
    radiogeddon_lab_view_set_status(v, "915.00 FM476 EXT", "Session: a_very_long_name", true);
    render(view, "home_long_status");
    radiogeddon_lab_view_show_home(v, g_items, 0, 9);
    CHECK(render(view, NULL), "no items: still draws");
    view_host_input(view, InputKeyDown, InputTypeShort);
    radiogeddon_lab_view_show_home(v, g_items, ITEMS, 200);
    CHECK(radiogeddon_lab_view_get_selected(v) == 0, "stale selection reset");

    view_host_exit(view);
    CHECK(!radiogeddon_lab_view_is_visible(v), "not visible once left");
    radiogeddon_lab_view_free(v);
}

static void test_home_cyber(void) {
    printf("test_home_cyber\n");
    RadioGeddonLabView* v = radiogeddon_lab_view_alloc();
    View* view = radiogeddon_lab_view_get_view(v);
    radiogeddon_lab_view_set_style(v, RgUiStyleCyberLab);
    radiogeddon_lab_view_show_home(v, g_items, ITEMS, 1);
    radiogeddon_lab_view_set_status(v, "433.92 AM650 INT", "41 KB", false);
    radiogeddon_lab_view_set_readout(v, "FULL v1.0.0-b8");
    bool inside = true;
    uint32_t redraws = 0;
    for(uint32_t t = 0; t < 8000; t += 100) {
        RgMascotFrame f;
        frame_of(RgMascotStateIdle, t, &f);
        radiogeddon_lab_view_update(v, &f, 10000 + t);
        redraws += view_host_updates(view);
        if(!render(view, NULL)) inside = false;
    }
    CHECK(inside, "every Cyber Lab frame inside");
    CHECK(redraws >= 30 && redraws <= 45, "the radar turns at about 4 steps a second");
    render(view, "cyber_lab_home");
    for(int i = 0; i < 5; i++)
        view_host_input(view, InputKeyDown, InputTypeShort);
    CHECK(radiogeddon_lab_view_get_selected(v) == 6, "navigates");
    render(view, "cyber_lab_scrolled");
    radiogeddon_lab_view_free(v);
}

/* ---- cards ------------------------------------------------------------------ */

static uint16_t host_width(void* context, const char* text, size_t len) {
    char buf[96];
    if(len >= sizeof(buf)) len = sizeof(buf) - 1;
    memcpy(buf, text, len);
    buf[len] = '\0';
    return canvas_string_width(context, buf);
}

static const struct {
    const char* header;
    const char* text;
} g_messages[] = {
#include MESSAGES_INC
    // Built at run time: the longest forms they take.
    {"Not enough memory", "Radio needs ~38 KB,\n12 KB free. Restart\nthe Flipper and retry."},
    {"Session created", "Signals 433.92 AM650 12 files"},
};

static void test_cards(void) {
    printf("test_cards\n");
    RadioGeddonLabView* v = radiogeddon_lab_view_alloc();
    View* view = radiogeddon_lab_view_get_view(v);
    radiogeddon_lab_view_set_callback(v, lab_cb, NULL);
    RgMascotFrame f;

    // Every message the app shows: whole and inside, in both lab styles.
    for(int style = RgUiStyleDolphinLab; style <= RgUiStyleCyberLab; style++) {
        radiogeddon_lab_view_set_style(v, (RgUiStyle)style);
        frame_of(RgMascotStateError, 0, &f);
        radiogeddon_lab_view_update(v, &f, 0);
        bool inside = true, whole = true;
        for(size_t i = 0; i < sizeof(g_messages) / sizeof(g_messages[0]); i++) {
            radiogeddon_lab_view_show_card(
                v, RadioGeddonLabCardMessage, g_messages[i].header, g_messages[i].text);
            if(!render(view, NULL)) {
                inside = false;
                printf("  overflow: %s\n", g_messages[i].header);
            }
            // The text is all there: wrapped across the screen it fits.
            Canvas* c = canvas_host();
            canvas_set_font(c, FontSecondary);
            RgUiLine lines[8];
            if(rg_ui_wrap(g_messages[i].text, 126, host_width, c, lines, 8) > 5) whole = false;
        }
        CHECK(inside, "every message card inside the screen");
        CHECK(whole, "every message fits whole");
    }

    radiogeddon_lab_view_set_style(v, RgUiStyleDolphinLab);
    frame_of(RgMascotStateError, 0, &f);
    radiogeddon_lab_view_update(v, &f, 0);
    radiogeddon_lab_view_show_card(
        v, RadioGeddonLabCardMessage, "Cannot open", "Not a readable\n.sub file.");
    render(view, "card_error");
    radiogeddon_lab_view_show_card(
        v,
        RadioGeddonLabCardMessage,
        "No external radio",
        "No CC1101 module answered\non the GPIO pins. Using\nthe internal radio.");
    render(view, "card_error_long");
    frame_of(RgMascotStateLowMemory, 0, &f);
    radiogeddon_lab_view_update(v, &f, 0);
    radiogeddon_lab_view_show_card(
        v,
        RadioGeddonLabCardMessage,
        "Not enough memory",
        "Radio needs ~38 KB,\n12 KB free. Restart\nthe Flipper and retry.");
    render(view, "card_low_memory");

    memset(g_events, 0, sizeof(g_events));
    CHECK(view_host_input(view, InputKeyOk, InputTypeShort), "OK closes a message");
    CHECK(g_events[RadioGeddonLabEventDismiss] == 1, "dismiss event");
    CHECK(!view_host_input(view, InputKeyBack, InputTypeShort), "Back left to the scene");

    // Save results, with a long file name and a session.
    frame_of(RgMascotStateSaved, 0, &f);
    radiogeddon_lab_view_update(v, &f, 0);
    radiogeddon_lab_view_show_card(
        v,
        RadioGeddonLabCardSaved,
        "Saved",
        "a_rather_long_recording_name_2026_10_11.sub\n+ session garage\n123456 samples, 12.3 s\n"
        "42 lost (SD too slow)");
    render(view, "card_save_success");
    radiogeddon_lab_view_show_card(v, RadioGeddonLabCardSaved, "Saved", "RAW_433.sub");
    render(view, "card_save_short");
    frame_of(RgMascotStateError, 0, &f);
    radiogeddon_lab_view_update(v, &f, 0);
    radiogeddon_lab_view_show_card(
        v, RadioGeddonLabCardSaved, "Save failed", "Could not write\nto the SD card.");
    render(view, "card_save_failed");

    // Progress: only a real figure is drawn.
    frame_of(RgMascotStateAnalyzing, 0, &f);
    radiogeddon_lab_view_update(v, &f, 0);
    radiogeddon_lab_view_show_card(v, RadioGeddonLabCardBusy, "Opening...", NULL);
    render(view, "card_busy");
    uint32_t busy_ink = canvas_host_count(canvas_host(), 56, 24, 70, 22);
    radiogeddon_lab_view_show_card(v, RadioGeddonLabCardProgress, "Writing report...", NULL);
    render(view, "card_progress_unknown");
    CHECK(
        canvas_host_count(canvas_host(), 56, 24, 70, 22) == busy_ink,
        "unknown progress: no figure, no bar");
    radiogeddon_lab_view_set_progress(v, 0);
    render(view, "card_progress_0");
    radiogeddon_lab_view_set_progress(v, 42);
    render(view, "card_analyzing");
    uint32_t ink42 = canvas_host_count(canvas_host(), 58, 39, 62, 3);
    radiogeddon_lab_view_set_progress(v, 100);
    render(view, "card_progress_100");
    CHECK(canvas_host_count(canvas_host(), 58, 39, 62, 3) > ink42, "the bar follows the figure");
    radiogeddon_lab_view_set_style(v, RgUiStyleCyberLab);
    render(view, "cyber_card_progress");
    radiogeddon_lab_view_show_card(
        v, RadioGeddonLabCardMessage, "Bad range", "Start must not be\nabove end.");
    render(view, "cyber_card_error");

    radiogeddon_lab_view_free(v);
}

/* ---- transitions ------------------------------------------------------------ */

static void test_transitions(void) {
    printf("test_transitions\n");
    RadioGeddonLabView* v = radiogeddon_lab_view_alloc();
    View* view = radiogeddon_lab_view_get_view(v);
    radiogeddon_lab_view_set_style(v, RgUiStyleDolphinLab);
    radiogeddon_lab_view_show_home(v, g_items, ITEMS, 0);
    radiogeddon_lab_view_set_status(v, "433.92 AM650", "Full", false);
    RgMascotFrame f;
    frame_of(RgMascotStateIdle, 0, &f);
    radiogeddon_lab_view_update(v, &f, 0);
    render(view, NULL);
    uint32_t full = canvas_host_count(canvas_host(), 0, 0, 128, 64);

    radiogeddon_lab_view_transition(v, RgTransitionWipe, 300, 1000);
    CHECK(radiogeddon_lab_view_transition_active(v), "running");
    render(view, NULL);
    CHECK(canvas_host_count(canvas_host(), 0, 0, 128, 64) < 80, "starts (nearly) hidden");
    radiogeddon_lab_view_update(v, NULL, 1150);
    render(view, "transition_wipe");
    uint32_t mid = canvas_host_count(canvas_host(), 0, 0, 128, 64);
    CHECK(mid > 80 && mid < full, "half revealed");
    radiogeddon_lab_view_update(v, NULL, 1300);
    CHECK(!radiogeddon_lab_view_transition_active(v), "done after 300 ms");
    render(view, NULL);
    CHECK(canvas_host_count(canvas_host(), 0, 0, 128, 64) == full, "all revealed");

    radiogeddon_lab_view_transition(v, RgTransitionDither, 300, 2000);
    radiogeddon_lab_view_update(v, NULL, 2150);
    render(view, "transition_dither");
    uint32_t d = canvas_host_count(canvas_host(), 0, 0, 128, 64);
    CHECK(d > full / 4 && d < full, "dither half way");

    // A key ends it at once and is handled as usual.
    radiogeddon_lab_view_transition(v, RgTransitionWipe, 300, 3000);
    view_host_input(view, InputKeyDown, InputTypeShort);
    CHECK(!radiogeddon_lab_view_transition_active(v), "a key ends the transition");
    CHECK(radiogeddon_lab_view_get_selected(v) == 1, "and still moves the menu");

    // Cost: draw calls are what the device spends; time the host's as a
    // rough comparison of the kinds (not a device measurement).
    static const RgTransitionKind kinds[] = {
        RgTransitionNone, RgTransitionWipe, RgTransitionDither};
    static const char* const names[] = {"none", "wipe", "dither"};
    for(size_t k = 0; k < 3; k++) {
        clock_t c0 = clock();
        for(int i = 0; i < 200; i++) {
            radiogeddon_lab_view_transition(v, kinds[k], 300, 5000);
            radiogeddon_lab_view_update(v, NULL, 5150);
            render(view, NULL);
        }
        double us = (double)(clock() - c0) * 1e6 / CLOCKS_PER_SEC / 200.0;
        printf("  host benchmark: home frame with %s transition: %.0f us\n", names[k], us);
    }
    radiogeddon_lab_view_free(v);
}

/* ---- badges ----------------------------------------------------------------- */

static void test_badges(void) {
    printf("test_badges\n");
    Canvas* c = canvas_host();
    bool inside = true;
    for(int s = 0; s < RgMascotStateCount; s++)
        for(uint32_t t = 0; t < 4000; t += 100)
            for(int cyber = 0; cyber < 2; cyber++) {
                RgMascotFrame f;
                rg_mascot_pose((RgMascotState)s, t, 1, RG_MASCOT_PROGRESS_NONE, t > 2000, &f);
                canvas_host_reset(c);
                rg_draw_badge(c, 128 - RG_BADGE_W, 0, cyber, &f);
                if(canvas_host_clipped(c)) inside = false;
                if(canvas_host_count(c, 0, 0, 128 - RG_BADGE_W, 64) ||
                   canvas_host_count(c, 0, RG_BADGE_H, 128, 64 - RG_BADGE_H))
                    inside = false;
            }
    CHECK(inside, "badges stay in their box");

    // The recording lamp is drawn only while recording.
    RgMascotFrame f;
    rg_mascot_pose(RgMascotStateReceiving, 0, 1, 0, false, &f);
    canvas_host_reset(c);
    rg_draw_badge(c, 0, 0, false, &f);
    uint32_t rx = canvas_host_count(c, 11, 0, 3, 5);
    rg_mascot_pose(RgMascotStateRecording, 0, 1, 0, false, &f);
    canvas_host_reset(c);
    rg_draw_badge(c, 0, 0, false, &f);
    CHECK(canvas_host_count(c, 11, 1, 3, 3) >= 5 && f.fx_on, "recording: lamp lit");
    (void)rx;
}

/* ---- all poses at every place ---------------------------------------------- */

static void test_mascot_everywhere(void) {
    printf("test_mascot_everywhere\n");
    Canvas* c = canvas_host();
    bool inside = true;
    for(int s = 0; s < RgMascotStateCount; s++)
        for(uint32_t t = 0; t < 30000; t += 50) {
            RgMascotFrame f;
            rg_mascot_pose((RgMascotState)s, t, 77, (uint8_t)(t % 101), (t / 700) & 1, &f);
            canvas_host_reset(c);
            rg_draw_mascot(c, 0, RG_MASCOT_TOP, &f, 64, true);
            rg_draw_prop(c, 33, 42, f.prop, f.prop_phase, f.progress);
            if(canvas_host_clipped(c) || canvas_host_text_overflows(c)) inside = false;
        }
    CHECK(inside, "every pose with its props and effects fits from the screen's top");
}

/* ---- radio screens with their badge ----------------------------------------- */

/* The views ask their engines for snapshots; synthetic ones stand in here
 * (no radio, no thread). */
static RadioGeddonScannerSnapshot g_scan;
void radiogeddon_scanner_snapshot(RadioGeddonScanner* instance, RadioGeddonScannerSnapshot* out) {
    (void)instance;
    *out = g_scan;
}

static RadioGeddonRangeSnapshot g_range;
void radiogeddon_rangescan_snapshot(RadioGeddonRangeScan* instance, RadioGeddonRangeSnapshot* out) {
    (void)instance;
    *out = g_range;
}

static void wf_waiting(
    void* engine,
    const RadioGeddonWaterfallRequest* request,
    RadioGeddonWaterfallFrame* out) {
    (void)engine;
    (void)request;
    memset(out, 0, sizeof(*out));
    out->columns = 64;
    out->points = 64;
    out->estimate_ms = 1300;
    out->cursor_dbm = RG_WF_NO_DATA;
    out->cursor_peak = RG_WF_NO_DATA;
    out->cursor_hz = 433920000;
}

typedef void (*BadgeSetter)(void* view, const RgMascotFrame* frame, bool cyber);

static const struct {
    RgMascotState s;
    const char* name;
} g_radio_states[] = {
    {RgMascotStateScanning, "scanning"},
    {RgMascotStateDetected, "signal_detected"},
    {RgMascotStateReceiving, "receiving"},
    {RgMascotStateRecording, "recording"},
};

/* Every badge frame of the radio states on @p view, previews of each. */
static void badge_states(View* view, const char* prefix, BadgeSetter set, void* v) {
    bool inside = true;
    for(size_t i = 0; i < sizeof(g_radio_states) / sizeof(g_radio_states[0]); i++) {
        for(uint32_t t = 0; t < 3000; t += 100)
            for(int cyber = 0; cyber < 2; cyber++) {
                RgMascotFrame f;
                rg_mascot_pose(g_radio_states[i].s, t, 1, RG_MASCOT_PROGRESS_NONE, false, &f);
                set(v, &f, cyber);
                if(!render(view, NULL)) inside = false;
            }
        RgMascotFrame f;
        rg_mascot_pose(g_radio_states[i].s, 0, 1, RG_MASCOT_PROGRESS_NONE, false, &f);
        set(v, &f, false);
        char name[64];
        snprintf(name, sizeof(name), "%s_%s", prefix, g_radio_states[i].name);
        render(view, name);
    }
    char msg[80];
    snprintf(msg, sizeof(msg), "%s: every badge frame inside the screen", prefix);
    CHECK(inside, msg);
}

static void set_scanner(void* v, const RgMascotFrame* f, bool c) {
    radiogeddon_scanner_view_set_badge(v, f, c);
}
static void set_receiver(void* v, const RgMascotFrame* f, bool c) {
    radiogeddon_receiver_view_set_badge(v, f, c);
}
static void set_spectrum(void* v, const RgMascotFrame* f, bool c) {
    radiogeddon_spectrum_view_set_badge(v, f, c);
}
static void set_waterfall(void* v, const RgMascotFrame* f, bool c) {
    radiogeddon_waterfall_view_set_badge(v, f, c);
}

static void test_radio_screens(void) {
    printf("test_radio_screens\n");
    // Scanner: the sweep's cursor and the badge.
    memset(&g_scan, 0, sizeof(g_scan));
    g_scan.count = 6;
    g_scan.hold_index = -1;
    g_scan.global_floor = -98.0f;
    g_scan.threshold_db = 10;
    for(size_t i = 0; i < g_scan.count; i++) {
        g_scan.frequencies[i] = 315000000 + (uint32_t)i * 100000000u;
        g_scan.channels[i].samples = 50;
        g_scan.channels[i].last = -95.0f + (float)i * 9;
        g_scan.channels[i].peak = -80.0f + (float)i * 5;
        g_scan.channels[i].floor = -98.0f;
        g_scan.channels[i].hits = (uint16_t)(i * 7);
        g_scan.channels[i].active = i == 4;
    }
    RadioGeddonScannerView* sv = radiogeddon_scanner_view_alloc();
    View* sview = radiogeddon_scanner_view_get_view(sv);
    bool moves = true;
    uint32_t prev_col = 0xFFFF;
    for(int32_t k = 0; k < (int32_t)g_scan.count; k++) {
        g_scan.measuring = k;
        radiogeddon_scanner_view_update(sv, NULL);
        render(sview, NULL);
        // The cursor: the mark on the line under the header.
        uint32_t col = 0;
        for(int x = 0; x < 128; x++)
            if(canvas_host_pixel(canvas_host(), x, 10)) {
                col = (uint32_t)x;
                break;
            }
        if(col == prev_col) moves = false;
        prev_col = col;
    }
    CHECK(moves, "scan cursor follows the channel being measured");
    g_scan.measuring = -1;
    radiogeddon_scanner_view_update(sv, NULL);
    render(sview, "scanner_paused_no_cursor");
    CHECK(
        canvas_host_count(canvas_host(), 0, 10, 128, 1) == 0,
        "no cursor when nothing is measured");
    g_scan.measuring = 2;
    radiogeddon_scanner_view_update(sv, NULL);
    badge_states(sview, "scanner", set_scanner, sv);
    radiogeddon_scanner_view_set_badge(sv, NULL, false);
    render(sview, "scanner_classic");
    CHECK(
        canvas_host_count(canvas_host(), 56, 1, RG_BADGE_W, RG_BADGE_H) == 0, "Classic: no badge");
    // Adaptive modes: the mode, the selected channel's share and the
    // longest wait, from the engine's real counters (synthetic here).
    for(uint8_t mode = RgAdaptBalanced; mode < RgAdaptModeCount; mode++) {
        g_scan.mode = mode;
        for(size_t i = 0; i < g_scan.count; i++)
            g_scan.share[i] = (uint8_t)(i == 4 ? 31 : 3);
        g_scan.worst_gap_ms = 4200;
        g_scan.measuring = 4;
        radiogeddon_scanner_view_update(sv, NULL);
        char name[40];
        snprintf(name, sizeof(name), "scanner_adaptive_%u", (unsigned)mode);
        render(sview, name);
    }
    g_scan.mode = RgAdaptStandard;
    radiogeddon_scanner_view_free(sv);

    // Receiver: listening, and the recorder's line with its lamp.
    RadioGeddonReceiverView* rv = radiogeddon_receiver_view_alloc();
    View* rview = radiogeddon_receiver_view_get_view(rv);
    radiogeddon_receiver_view_set_config(rv, 433920000, "AM650");
    radiogeddon_receiver_view_set_rssi(rv, -62.0f);
    radiogeddon_receiver_view_set_threshold(rv, -127.0f);
    radiogeddon_receiver_view_set_history(rv, 2, "Princeton 24bit");
    RadioGeddonRecordStats st;
    memset(&st, 0, sizeof(st));
    st.samples = 12345;
    st.elapsed_ms = 4200;
    radiogeddon_receiver_view_set_recording(rv, true, &st);
    radiogeddon_receiver_view_set_external(rv, true);
    badge_states(rview, "receiver", set_receiver, rv);
    radiogeddon_receiver_view_free(rv);

    // Range scanner: sweep marker, cursor cap, badge.
    memset(&g_range, 0, sizeof(g_range));
    g_range.count = 81;
    g_range.range.start_hz = 433000000;
    g_range.range.step_hz = 25000;
    g_range.range.points = 81;
    g_range.floor = -98;
    g_range.hold_index = -1;
    g_range.threshold_db = 10;
    g_range.position = 30;
    g_range.sweep_ms = 1200;
    for(uint32_t i = 0; i < g_range.count; i++) {
        g_range.point[i].flags = RgSpectrumMeasured;
        g_range.point[i].last = (int8_t)(-100 + (int32_t)((i * 37) % 30));
        g_range.point[i].peak = (int8_t)(g_range.point[i].last + 5);
    }
    RadioGeddonSpectrumView* pv = radiogeddon_spectrum_view_alloc();
    View* pview = radiogeddon_spectrum_view_get_view(pv);
    radiogeddon_spectrum_view_update(pv, NULL);
    radiogeddon_spectrum_view_set_cursor(pv, 40);
    CHECK(radiogeddon_spectrum_view_sweeping(pv), "sweeping");
    badge_states(pview, "range", set_spectrum, pv);
    g_range.paused = true;
    radiogeddon_spectrum_view_update(pv, NULL);
    CHECK(!radiogeddon_spectrum_view_sweeping(pv), "paused: not collecting");
    render(pview, "range_paused");
    radiogeddon_spectrum_view_free(pv);

    // Waterfall: the entry reveal hides, never invents; a key ends it.
    RadioGeddonWaterfallView* wv = radiogeddon_waterfall_view_alloc();
    View* wview = radiogeddon_waterfall_view_get_view(wv);
    radiogeddon_waterfall_view_update(wv, wf_waiting, NULL);
    render(wview, NULL);
    uint32_t full = canvas_host_count(canvas_host(), 0, 11, 128, 53);
    radiogeddon_waterfall_view_set_reveal(wv, 0);
    render(wview, NULL);
    uint32_t hidden = canvas_host_count(canvas_host(), 0, 11, 128, 53);
    radiogeddon_waterfall_view_set_reveal(wv, 128);
    render(wview, "waterfall_reveal");
    uint32_t half = canvas_host_count(canvas_host(), 0, 11, 128, 53);
    CHECK(hidden < 140 && half > hidden && half < full + 128, "reveal uncovers the screen");
    view_host_input(wview, InputKeyLeft, InputTypeShort);
    render(wview, NULL);
    CHECK(canvas_host_count(canvas_host(), 0, 11, 128, 53) == full, "a key ends the reveal");
    badge_states(wview, "waterfall", set_waterfall, wv);
    radiogeddon_waterfall_view_free(wv);
}

/* ---- the Dolphin Lab room ------------------------------------------------------ */

static int g_room_ok[RadioGeddonRoomCount];
static void room_cb(uint8_t station, void* context) {
    (void)context;
    if(station < RadioGeddonRoomCount) g_room_ok[station]++;
}

static void test_room(void) {
    printf("test_room\n");
    RadioGeddonRoomView* v = radiogeddon_room_view_alloc();
    View* view = radiogeddon_room_view_get_view(v);
    radiogeddon_room_view_set_callback(v, room_cb, NULL);
    char lines[RADIOGEDDON_ROOM_LINES][RADIOGEDDON_ROOM_LINE_LEN] = {
        "RadioGeddon 1.0.0-beta.9",
        "Momentum mntm-012",
        "Level 3, 164 XP",
        "Saved 12, analysed 5",
        "Badges 4/12, logs 1",
    };
    radiogeddon_room_view_set_terminal(v, (const char(*)[RADIOGEDDON_ROOM_LINE_LEN])lines, 12);
    radiogeddon_room_view_set_workbench(v, "RAW_garage_door_2026.sub", "Gate study");
    radiogeddon_room_view_set_decoration(v, RgDecoPlant);

    static const char* const names[RadioGeddonRoomCount] = {
        "room_workbench",
        "room_scope_empty",
        "room_radar",
        "room_terminal",
        "room_rack",
        "room_corner"};
    memset(g_room_ok, 0, sizeof(g_room_ok));
    uint32_t now = 1000;
    bool inside = true;
    for(int s = 0; s < RadioGeddonRoomCount; s++) {
        CHECK(radiogeddon_room_view_get_station(v) == s, "station order");
        for(int k = 0; k < 20; k++) {
            RgMascotFrame f;
            rg_mascot_pose(
                RgMascotStateScanning, (uint32_t)k * 100, 1, RG_MASCOT_PROGRESS_NONE, false, &f);
            radiogeddon_room_view_update(v, &f, now += 100);
            if(!render(view, NULL)) inside = false;
        }
        render(view, names[s]);
        CHECK(view_host_input(view, InputKeyOk, InputTypeShort), "OK consumed");
        CHECK(g_room_ok[s] == 1, "OK reports its station");
        view_host_input(view, InputKeyRight, InputTypeShort);
        // Mid-slide.
        radiogeddon_room_view_update(v, NULL, now += 1);
        radiogeddon_room_view_update(v, NULL, now += 100);
        if(!render(view, NULL)) inside = false;
    }
    CHECK(inside, "every room frame (and slide) inside the screen");
    CHECK(radiogeddon_room_view_get_station(v) == 0, "Right wraps to the first");
    view_host_input(view, InputKeyLeft, InputTypeShort);
    CHECK(radiogeddon_room_view_get_station(v) == RadioGeddonRoomCorner, "Left wraps to the last");
    view_host_input(view, InputKeyDown, InputTypeShort);
    view_host_input(view, InputKeyDown, InputTypeShort);
    CHECK(radiogeddon_room_view_get_gallery(v) == 2, "Down: next pose");
    view_host_input(view, InputKeyUp, InputTypeShort);
    CHECK(radiogeddon_room_view_get_gallery(v) == 1, "Up: previous pose");
    for(int g = 0; g < RgMascotStateCount; g++) {
        RgMascotFrame f;
        rg_mascot_pose(radiogeddon_room_view_get_gallery(v), 300, 1, 60, false, &f);
        radiogeddon_room_view_update(v, &f, now += 100);
        if(!render(view, NULL)) inside = false;
        view_host_input(view, InputKeyDown, InputTypeShort);
    }
    CHECK(inside, "every gallery pose inside");
    // Every look (headphones, goggles, antenna, tool, accessory) on every
    // pose stays on the screen; the decorations too.
    RgMascotLook look;
    bool looks_inside = true;
    for(uint8_t ph = 0; ph < 2; ph++)
        for(uint8_t go = 0; go < RgLookGogglesCount; go++)
            for(uint8_t an = 0; an < RgLookAntennaCount; an++)
                for(uint8_t to = 0; to < RgLookToolCount; to++)
                    for(uint8_t ac = 0; ac < RgLookAccessoryCount; ac++) {
                        look = (RgMascotLook){ph, go, an, to, (uint8_t)(ac % 3), ac};
                        RgMascotFrame f;
                        rg_mascot_pose(
                            (RgMascotState)((ph + go + an + to + ac) % RgMascotStateCount),
                            700,
                            1,
                            60,
                            false,
                            &f);
                        rg_mascot_style(&f, &look, RgAnimFull, 700);
                        radiogeddon_room_view_update(v, &f, now += 100);
                        if(!render(view, NULL)) looks_inside = false;
                    }
    CHECK(looks_inside, "every look inside the screen");
    for(uint8_t d = 0; d < RgDecoCount; d++) {
        radiogeddon_room_view_set_decoration(v, d);
        if(!render(view, NULL)) looks_inside = false;
    }
    CHECK(looks_inside, "every decoration inside the screen");
    look = (RgMascotLook){
        1, RgLookGogglesAlways, RgLookAntennaDish, RgLookToolWrench, 1, RgLookAccessoryBowtie};
    {
        RgMascotFrame f;
        rg_mascot_pose(RgMascotStateIdle, 0, 1, 60, false, &f);
        rg_mascot_style(&f, &look, RgAnimNormal, 0);
        radiogeddon_room_view_set_decoration(v, RgDecoTrophy);
        radiogeddon_room_view_update(v, &f, now += 100);
        render(view, "room_corner_custom");
        look = (RgMascotLook){
            0, RgLookGogglesOff, RgLookAntennaWhip, RgLookToolProbe, 2, RgLookAccessoryScarf};
        rg_mascot_style(&f, &look, RgAnimNormal, 0);
        radiogeddon_room_view_set_decoration(v, RgDecoPoster);
        radiogeddon_room_view_update(v, &f, now += 100);
        render(view, "room_corner_custom2");
    }
    CHECK(!view_host_input(view, InputKeyBack, InputTypeShort), "Back leaves the room");

    // The rack: Up/Down choose a tool, wrapping.
    view_host_input(view, InputKeyLeft, InputTypeShort); // corner -> rack
    CHECK(radiogeddon_room_view_get_station(v) == RadioGeddonRoomRack, "rack");
    CHECK(
        radiogeddon_room_view_get_rack(v) == RadioGeddonRackScanner, "rack starts on the Scanner");
    view_host_input(view, InputKeyDown, InputTypeShort);
    CHECK(radiogeddon_room_view_get_rack(v) == RadioGeddonRackAdaptive, "Down: next tool");
    view_host_input(view, InputKeyUp, InputTypeShort);
    view_host_input(view, InputKeyUp, InputTypeShort);
    CHECK(radiogeddon_room_view_get_rack(v) == RadioGeddonRackReceive, "Up wraps");
    for(int k = 0; k < 20; k++)
        radiogeddon_room_view_update(v, NULL, now += 100);
    render(view, "room_rack_receive");

    // The oscilloscope shows a recording's levels as recorded.
    view_host_input(view, InputKeyRight, InputTypeShort); // rack -> corner
    view_host_input(view, InputKeyRight, InputTypeShort); // -> workbench
    view_host_input(view, InputKeyRight, InputTypeShort); // -> scope
    radiogeddon_room_view_update(v, NULL, now += 1000);
    radiogeddon_room_view_update(v, NULL, now += 1000);
    int32_t samples[] = {400, -400, 400, -1200, 1200, -400, 400, -400, 400, -1200, 1200, -2000};
    RadioGeddonRoomTrace trace;
    memset(&trace, 0, sizeof(trace));
    trace.loaded = true;
    uint64_t span = rg_ui_trace(
        samples, sizeof(samples) / sizeof(samples[0]), RADIOGEDDON_ROOM_SCOPE_W, trace.bits);
    trace.span_us = (uint32_t)span;
    strlcpy(trace.name, "RAW_garage_door_2026.sub", sizeof(trace.name));
    radiogeddon_room_view_set_scope(v, &trace);
    render(view, "room_scope");
    CHECK(span == 9600, "span is the recorded time");
    CHECK((trace.bits[0] & 1) == 1, "starts high as recorded");
    CHECK(!(trace.bits[RADIOGEDDON_ROOM_SCOPE_W / 8 - 1] & 0x80), "ends low as recorded");
    trace.loaded = true;
    trace.empty = true;
    radiogeddon_room_view_set_scope(v, &trace);
    render(view, "room_scope_no_samples");
    radiogeddon_room_view_free(v);

    // rg_ui_trace edge cases.
    uint8_t bits[4];
    CHECK(rg_ui_trace(samples, 0, 32, bits) == 0, "no samples");
    int32_t zero[2] = {0, 0};
    CHECK(rg_ui_trace(zero, 2, 32, bits) == 0, "zero durations");
    int32_t one[1] = {-500};
    CHECK(rg_ui_trace(one, 1, 32, bits) == 500 && bits[0] == 0 && bits[3] == 0, "all low");
    int32_t extreme[2] = {INT32_MIN, INT32_MAX};
    CHECK(rg_ui_trace(extreme, 2, 32, bits) > 0, "extreme values do not overflow");
}

/* Pulse Studio on a synthetic RAW file in memory: PWM frames with a
 * preamble, one frame with a glitch, read through the real RAW reader and
 * analyzer, every page drawn. */
typedef struct {
    const char* text;
    size_t len;
    size_t pos;
} MemFile;

static size_t mem_read(void* ctx, uint8_t* buf, size_t len) {
    MemFile* f = ctx;
    size_t n = f->len - f->pos < len ? f->len - f->pos : len;
    memcpy(buf, f->text + f->pos, n);
    f->pos += n;
    return n;
}

static bool mem_seek(void* ctx, uint32_t offset) {
    MemFile* f = ctx;
    if(offset > f->len) return false;
    f->pos = offset;
    return true;
}

static int g_studio_want = -1;
static void studio_want(uint8_t frame, void* context) {
    (void)context;
    g_studio_want = frame;
}

static void test_studio_view(void) {
    printf("test_studio_view\n");
    static char text[16384];
    size_t len =
        (size_t)snprintf(text, sizeof(text), "Filetype: Flipper SubGhz RAW File\nVersion: 1\n");
    static const char* const codes[] = {
        "101100111000101011110000",
        "101100111000101011110000",
        "101100111000101011110001",
        "101100111000101011110000",
        "101100111000101011110010",
        "101100111000101011110000"};
    for(size_t c = 0; c < 6; c++) {
        len += (size_t)snprintf(text + len, sizeof(text) - len, "RAW_Data: -12000");
        for(int p = 0; p < 6; p++)
            len += (size_t)snprintf(text + len, sizeof(text) - len, " 400 -400");
        for(const char* b = codes[c]; *b; b++) {
            if(c == 3 && b == codes[c] + 9) // a glitch inside one frame
                len += (size_t)snprintf(text + len, sizeof(text) - len, " 60 -60");
            len += (size_t)snprintf(
                text + len, sizeof(text) - len, *b == '1' ? " 1200 -400" : " 400 -1200");
        }
        len += (size_t)snprintf(text + len, sizeof(text) - len, "\n");
    }
    CHECK(len < sizeof(text) - 1, "synthetic file fits");

    MemFile mf = {text, len, 0};
    RgRawReader* reader = calloc(1, sizeof(RgRawReader));
    rg_raw_reader_init(reader, (RgRawSource){mem_read, mem_seek, &mf});
    RgAnalyzer* a = calloc(1, sizeof(RgAnalyzer));
    rg_analyzer_begin(a);
    int32_t chunk[128];
    do {
        rg_raw_reader_rewind(reader);
        size_t n;
        while((n = rg_raw_reader_read(reader, chunk, 128)) > 0)
            rg_analyzer_feed(a, chunk, n);
    } while(rg_analyzer_next_pass(a));
    CHECK(a->result.frames_kept >= 5, "studio: the analyzer finds the frames");

    RadioGeddonStudioView* sv = radiogeddon_studio_view_alloc();
    View* view = radiogeddon_studio_view_get_view(sv);
    radiogeddon_studio_view_set_callback(sv, studio_want, NULL);
    radiogeddon_studio_view_set_analysis(sv, "RAW_gate_test.sub", &a->result);
    free(a);
    // The view starts on the first frame that decodes (as the scene does).
    for(uint8_t f = 0; f < 8; f++)
        radiogeddon_studio_view_load_frame(sv, reader, f);
    static const char* const pages[] = {
        "studio_wave", "studio_frame", "studio_bits", "studio_hex", "studio_diff", "studio_timing"};
    for(size_t p = 0; p < 6; p++) {
        render(view, pages[p]);
        view_host_input(view, InputKeyOk, InputTypeShort);
    }
    // Back on the wave: frame changes ask for samples; zoom, cursor, marker.
    view_host_input(view, InputKeyDown, InputTypeShort);
    int first_want = g_studio_want;
    CHECK(first_want >= 1, "studio: Down asks for a later frame");
    view_host_input(view, InputKeyUp, InputTypeShort);
    CHECK(g_studio_want == first_want - 1, "studio: Up asks for the one before");
    view_host_input(view, InputKeyDown, InputTypeShort);
    radiogeddon_studio_view_load_frame(sv, reader, (uint8_t)g_studio_want);
    view_host_input(view, InputKeyDown, InputTypeShort);
    view_host_input(view, InputKeyDown, InputTypeShort);
    radiogeddon_studio_view_load_frame(sv, reader, (uint8_t)g_studio_want);
    render(view, "studio_wave_glitch");
    bool inside = true;
    for(int k = 0; k < 4; k++)
        view_host_input(view, InputKeyRight, InputTypeLong);
    for(int k = 0; k < 30; k++) {
        view_host_input(view, InputKeyRight, InputTypeShort);
        if(!render(view, NULL)) inside = false;
    }
    view_host_input(view, InputKeyOk, InputTypeLong);
    for(int k = 0; k < 10; k++)
        view_host_input(view, InputKeyRight, InputTypeShort);
    render(view, "studio_wave_zoom_marker");
    for(int k = 0; k < 8; k++) {
        view_host_input(view, InputKeyLeft, InputTypeLong);
        if(!render(view, NULL)) inside = false;
    }
    // Every frame on every page stays on the screen.
    for(int f = 0; f < 8; f++) {
        view_host_input(view, InputKeyDown, InputTypeShort);
        if(g_studio_want >= 0)
            radiogeddon_studio_view_load_frame(sv, reader, (uint8_t)g_studio_want);
        for(int p = 0; p < 6; p++) {
            if(!render(view, NULL)) inside = false;
            view_host_input(view, InputKeyOk, InputTypeShort);
        }
    }
    CHECK(inside, "studio: every frame and page inside the screen");
    CHECK(!view_host_input(view, InputKeyBack, InputTypeShort), "studio: Back leaves");
    radiogeddon_studio_view_free(sv);
    free(reader);
}

int main(void) {
    test_home();
    test_home_cyber();
    test_cards();
    test_transitions();
    test_badges();
    test_mascot_everywhere();
    test_radio_screens();
    test_room();
    test_studio_view();
    printf("\n%d checks, %d failures, %d synthetic previews\n", g_checks, g_failures, g_previews);
    return g_failures ? 1 : 0;
}
