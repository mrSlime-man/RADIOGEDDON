/**
 * Host-side unit tests for Dolphin Lab's pure logic: the mascot's state
 * machine (helpers/rg_mascot.c), the interface helpers (helpers/rg_ui.c) and
 * the generated sprite table (helpers/rg_sprites.c).
 * Run: make -C test
 */
#include "../helpers/rg_mascot.h"
#include "../helpers/rg_ui.h"
#include "../helpers/rg_sprites.h"
#include <stdio.h>
#include <string.h>

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

/* ---- state machine ------------------------------------------------------- */

static void test_idle_and_priorities(void) {
    printf("test_idle_and_priorities\n");
    RgMascot m;
    rg_mascot_init(&m, 1000, 7);
    CHECK(rg_mascot_state(&m, 1000) == RgMascotStateIdle, "starts idle");

    rg_mascot_sync(&m, true, false, false, false, 1100);
    CHECK(rg_mascot_state(&m, 1100) == RgMascotStateScanning, "scanning");
    rg_mascot_event(&m, RgMascotEventSignalActivity, 0, 1200);
    CHECK(rg_mascot_state(&m, 1200) == RgMascotStateDetected, "detected over scanning");
    CHECK(
        rg_mascot_state(&m, 1200 + RG_MASCOT_DETECT_MS) == RgMascotStateScanning,
        "detection expires back to scanning");

    rg_mascot_sync(&m, false, true, false, false, 5000);
    CHECK(rg_mascot_state(&m, 5000) == RgMascotStateReceiving, "receiving");
    rg_mascot_event(&m, RgMascotEventSignalActivity, 0, 5100);
    CHECK(rg_mascot_state(&m, 5100) == RgMascotStateReceiving, "receiving outranks a detection");
    RgMascotFrame f;
    rg_mascot_frame(&m, 5100, &f);
    CHECK(f.fx == RgMascotFxExclaim, "but the detection still flashes on the receiving pose");

    rg_mascot_sync(&m, false, true, true, false, 5200);
    CHECK(rg_mascot_state(&m, 5200) == RgMascotStateRecording, "recording over receiving");
    rg_mascot_frame(&m, 5200, &f);
    CHECK(f.fx == RgMascotFxRec, "REC shows while recording, even with a detection");

    rg_mascot_sync(&m, false, true, true, true, 5300);
    CHECK(rg_mascot_state(&m, 5300) == RgMascotStateLowMemory, "low memory over recording");
    rg_mascot_event(&m, RgMascotEventOperationFailed, 0, 5400);
    CHECK(rg_mascot_state(&m, 5400) == RgMascotStateError, "error over everything");
    CHECK(
        rg_mascot_state(&m, 5400 + RG_MASCOT_ERROR_MS) == RgMascotStateLowMemory,
        "error is a reaction: it ends");

    rg_mascot_sync(&m, false, false, false, false, 9000);
    CHECK(rg_mascot_state(&m, 9000) == RgMascotStateIdle, "all stopped: idle");
    rg_mascot_frame(&m, 9000, &f);
    CHECK(f.fx != RgMascotFxRec, "no REC once recording stopped");
}

static void test_recording_follows_the_recorder(void) {
    printf("test_recording_follows_the_recorder\n");
    RgMascot m;
    rg_mascot_init(&m, 0, 1);
    RgMascotFrame f;
    // A recording that failed to start is never reported as running: the
    // level the app syncs is the recorder's own state.
    rg_mascot_sync(&m, false, true, false, false, 100);
    for(uint32_t t = 100; t < 3000; t += 100) {
        rg_mascot_frame(&m, t, &f);
        CHECK(f.fx != RgMascotFxRec, "receiving without a recorder: no REC");
    }
    rg_mascot_sync(&m, false, true, true, false, 3000);
    bool label_always = true;
    int lamp_on = 0;
    for(uint32_t t = 3000; t < 6000; t += 100) {
        rg_mascot_frame(&m, t, &f);
        if(f.fx != RgMascotFxRec) label_always = false;
        lamp_on += f.fx_on;
    }
    CHECK(label_always, "REC label on every frame while recording");
    CHECK(lamp_on > 5 && lamp_on < 25, "its lamp blinks");
    rg_mascot_sync(&m, false, true, false, false, 6000);
    rg_mascot_frame(&m, 6000, &f);
    CHECK(f.fx != RgMascotFxRec && f.state == RgMascotStateReceiving, "REC gone at once");
}

static void test_repeated_events_do_not_queue(void) {
    printf("test_repeated_events_do_not_queue\n");
    RgMascot m;
    rg_mascot_init(&m, 0, 3);
    for(uint32_t i = 0; i < 100000; i++)
        rg_mascot_event(&m, RgMascotEventSignalActivity, 0, 1000 + i / 100);
    // 100000 detections within one second end like one: 1.5 s after the last.
    uint32_t last = 1000 + 99999 / 100;
    CHECK(rg_mascot_state(&m, last + 10) == RgMascotStateDetected, "still detected");
    CHECK(
        rg_mascot_state(&m, last + RG_MASCOT_DETECT_MS) == RgMascotStateIdle,
        "over 1.5 s after the last one");
    CHECK(m.detections == 100000, "counted");
    m.detections = UINT32_MAX;
    rg_mascot_event(&m, RgMascotEventSignalActivity, 0, 5000);
    CHECK(m.detections == UINT32_MAX, "count saturates");
    CHECK(sizeof(RgMascot) < 96, "fixed size state");
}

static void test_boredom_and_sleep(void) {
    printf("test_boredom_and_sleep\n");
    RgMascot m;
    rg_mascot_init(&m, 0, 9);
    CHECK(rg_mascot_state(&m, RG_MASCOT_BORED_MS - 1) == RgMascotStateIdle, "idle first");
    CHECK(rg_mascot_state(&m, RG_MASCOT_BORED_MS) == RgMascotStateBored, "bored");
    CHECK(rg_mascot_state(&m, RG_MASCOT_ASLEEP_MS) == RgMascotStateAsleep, "asleep");
    RgMascotFrame f;
    rg_mascot_frame(&m, RG_MASCOT_ASLEEP_MS + 100, &f);
    CHECK(f.eyes == RgMascotEyesShut && f.fx == RgMascotFxZzz, "asleep pose");
    rg_mascot_event(&m, RgMascotEventUserInput, 0, RG_MASCOT_ASLEEP_MS + 200);
    CHECK(rg_mascot_state(&m, RG_MASCOT_ASLEEP_MS + 200) == RgMascotStateGreet, "a key wakes it");
    CHECK(
        rg_mascot_state(&m, RG_MASCOT_ASLEEP_MS + 200 + RG_MASCOT_GREET_MS) == RgMascotStateIdle,
        "a short welcome, then idle");
    rg_mascot_event(&m, RgMascotEventUserInput, 0, RG_MASCOT_ASLEEP_MS + 2000);
    CHECK(rg_mascot_state(&m, RG_MASCOT_ASLEEP_MS + 2000) == RgMascotStateIdle, "no welcome while awake");

    // A long scan with nothing on the air is not "no signal": it scans.
    rg_mascot_init(&m, 0, 9);
    for(uint32_t t = 0; t <= 3 * RG_MASCOT_ASLEEP_MS; t += 100)
        rg_mascot_sync(&m, true, false, false, false, t);
    CHECK(
        rg_mascot_state(&m, 3 * RG_MASCOT_ASLEEP_MS) == RgMascotStateScanning,
        "scanning for minutes stays scanning");
    rg_mascot_sync(&m, false, false, false, false, 3 * RG_MASCOT_ASLEEP_MS);
    CHECK(
        rg_mascot_state(&m, 3 * RG_MASCOT_ASLEEP_MS + 1000) == RgMascotStateIdle,
        "the quiet time starts when the scan stops");

    // The clock wraps around.
    rg_mascot_init(&m, UINT32_MAX - 500, 9);
    CHECK(rg_mascot_state(&m, 1000) == RgMascotStateIdle, "wrap: 1.5 s later still idle");
    rg_mascot_event(&m, RgMascotEventSaveCompleted, 0, UINT32_MAX - 100);
    CHECK(rg_mascot_state(&m, 500) == RgMascotStateSaved, "deadline across the wrap");
    CHECK(rg_mascot_state(&m, 2500) == RgMascotStateIdle, "and it expires");
    // A clock going backwards is not a long quiet time.
    rg_mascot_init(&m, 100000, 9);
    CHECK(rg_mascot_state(&m, 50000) == RgMascotStateIdle, "backwards clock: idle");
}

static void test_analysis_progress(void) {
    printf("test_analysis_progress\n");
    RgMascot m;
    rg_mascot_init(&m, 0, 2);
    RgMascotFrame f;
    rg_mascot_event(&m, RgMascotEventAnalysisStarted, 0, 10);
    rg_mascot_frame(&m, 10, &f);
    CHECK(f.state == RgMascotStateAnalyzing, "analyzing");
    CHECK(f.progress == RG_MASCOT_PROGRESS_NONE, "no progress invented");
    rg_mascot_event(&m, RgMascotEventAnalysisProgress, 42, 20);
    rg_mascot_frame(&m, 20, &f);
    CHECK(f.progress == 42, "real progress shown");
    rg_mascot_event(&m, RgMascotEventAnalysisProgress, 250, 30);
    rg_mascot_frame(&m, 30, &f);
    CHECK(f.progress == 100, "clamped");
    rg_mascot_event(&m, RgMascotEventAnalysisCompleted, 0, 40);
    CHECK(rg_mascot_state(&m, 40) == RgMascotStateIdle, "done");
    rg_mascot_frame(&m, 40, &f);
    CHECK(f.progress == RG_MASCOT_PROGRESS_NONE, "progress cleared");
}

static void test_unknown_events_and_determinism(void) {
    printf("test_unknown_events_and_determinism\n");
    RgMascot a, b;
    rg_mascot_init(&a, 0, 11);
    rg_mascot_init(&b, 0, 11);
    RgMascot before = a;
    rg_mascot_event(&a, (RgMascotEvent)RgMascotEventCount, 0, 50);
    rg_mascot_event(&a, (RgMascotEvent)200, 0, 60);
    a.now = before.now;
    CHECK(memcmp(&a, &before, sizeof(a)) == 0, "unknown events change nothing");

    // Same inputs, same frames.
    bool same = true;
    for(uint32_t t = 0; t < 60000; t += 100) {
        if(t == 5000) {
            rg_mascot_event(&a, RgMascotEventSaveCompleted, 0, t);
            rg_mascot_event(&b, RgMascotEventSaveCompleted, 0, t);
        }
        RgMascotFrame fa, fb;
        rg_mascot_frame(&a, t, &fa);
        rg_mascot_frame(&b, t, &fb);
        if(rg_mascot_frame_changed(&fa, &fb)) same = false;
    }
    CHECK(same, "deterministic");
    CHECK(strcmp(rg_mascot_state_name(RgMascotStateScanning), "Scanning") == 0, "names");
    CHECK(strcmp(rg_mascot_state_name(RgMascotStateCount), "?") == 0, "unknown name");
}

/* Every pose of every state uses valid indices, and idle animation is calm:
 * it changes less often than the active states. */
static void test_poses(void) {
    printf("test_poses\n");
    bool valid = true;
    uint32_t changes[RgMascotStateCount] = {0};
    for(int s = 0; s < RgMascotStateCount; s++) {
        RgMascotFrame prev;
        rg_mascot_pose((RgMascotState)s, 0, 5, 50, false, &prev);
        for(uint32_t t = 0; t < 60000; t += 100) {
            RgMascotFrame f;
            rg_mascot_pose((RgMascotState)s, t, 5, 50, (t / 1000) & 1, &f);
            if(f.state != s || f.eyes >= RgMascotEyesCount || f.fin >= RgMascotFinCount ||
               f.prop >= RgMascotPropCount || f.fx >= RgMascotFxCount || f.prop_phase > 15 ||
               f.fx_phase > 3 || f.eye_dx < -1 || f.eye_dx > 1 || f.eye_dy < -1 || f.eye_dy > 1 ||
               f.body_dy > 1 || f.antenna < 3 || f.antenna > 6 || f.phones_lift > 4)
                valid = false;
            if(rg_mascot_frame_changed(&prev, &f)) changes[s]++;
            prev = f;
        }
    }
    CHECK(valid, "all poses in range");
    // 600 frames of 100 ms: idle redraws far less than once per tick.
    CHECK(changes[RgMascotStateIdle] < 200, "idle is calm");
    CHECK(changes[RgMascotStateAsleep] < 200, "asleep is calm");
    CHECK(changes[RgMascotStateScanning] > 300, "scanning animates");
    RgMascotFrame f;
    rg_mascot_pose(RgMascotStateCount, 0, 0, 0, false, &f);
    CHECK(f.state == RgMascotStateIdle, "unknown state draws idle");
    rg_mascot_pose(RgMascotStateLowMemory, 0, 0, 0, false, &f);
    CHECK(f.eyes == RgMascotEyesWorried && f.fx == RgMascotFxChip, "low memory pose");
    rg_mascot_pose(RgMascotStateError, 0, 0, 0, false, &f);
    CHECK(f.fx == RgMascotFxQuestion, "error pose");
    rg_mascot_pose(RgMascotStateSaved, 0, 0, 0, false, &f);
    CHECK(f.eyes == RgMascotEyesHappy && f.fin == RgMascotFinCheer, "saved pose");
}

/* ---- interface helpers ----------------------------------------------------- */

static void test_styles(void) {
    printf("test_styles\n");
    CHECK(rg_ui_style_effective(RgUiStyleDolphinLab, 40000) == RgUiStyleDolphinLab, "kept");
    CHECK(rg_ui_style_effective(RgUiStyleCyberLab, 40000) == RgUiStyleCyberLab, "kept");
    CHECK(
        rg_ui_style_effective(RgUiStyleDolphinLab, RG_UI_MIN_FREE_HEAP - 1) == RgUiStyleClassic,
        "short of memory: Classic");
    CHECK(rg_ui_style_effective(99, 40000) == RgUiStyleClassic, "unknown value: Classic");
    CHECK(strcmp(rg_ui_style_name(RgUiStyleCyberLab), "Cyber Lab") == 0, "name");
    CHECK(strcmp(rg_ui_feedback_name(RgUiFeedbackMinimal), "Minimal") == 0, "name");
}

static void test_feedback(void) {
    printf("test_feedback\n");
    RgUiFeedback fb;
    rg_ui_feedback_init(&fb);
    for(int e = 0; e < RgUiFbCount; e++)
        CHECK(rg_ui_feedback(&fb, RgUiFeedbackOff, (RgUiFbEvent)e, 1000) == RgUiCueNone, "off");
    CHECK(rg_ui_feedback(&fb, RgUiFeedbackMinimal, RgUiFbMenu, 1000) == RgUiCueNone, "no clicks");
    CHECK(rg_ui_feedback(&fb, RgUiFeedbackMinimal, RgUiFbSignal, 1000) == RgUiCueNone, "no blip");
    CHECK(
        rg_ui_feedback(&fb, RgUiFeedbackMinimal, RgUiFbRecordStart, 1000) == RgUiCueTick, "tick");
    CHECK(
        rg_ui_feedback(&fb, RgUiFeedbackFull, RgUiFbRecordSaved, 5000) == RgUiCueNone,
        "a save has its own sound already");
    CHECK(
        rg_ui_feedback(&fb, RgUiFeedbackFull, RgUiFbError, 6000) == RgUiCueNone,
        "so has an error");

    // Signal blips: at most one per RG_UI_FB_SIGNAL_MS however many come.
    rg_ui_feedback_init(&fb);
    int blips = 0;
    for(uint32_t t = 0; t < 10000; t += 10)
        if(rg_ui_feedback(&fb, RgUiFeedbackFull, RgUiFbSignal, t) != RgUiCueNone) blips++;
    CHECK(blips == 5, "five blips in ten seconds of constant activity");
    // Cues of different moments still keep a gap between them.
    rg_ui_feedback_init(&fb);
    CHECK(rg_ui_feedback(&fb, RgUiFeedbackFull, RgUiFbMenu, 100) == RgUiCueClick, "click");
    CHECK(rg_ui_feedback(&fb, RgUiFeedbackFull, RgUiFbAnalysisDone, 120) == RgUiCueNone, "gap");
    CHECK(rg_ui_feedback(&fb, RgUiFeedbackFull, RgUiFbAnalysisDone, 200) == RgUiCueChime, "chime");
    CHECK(rg_ui_feedback(&fb, RgUiFeedbackFull, (RgUiFbEvent)99, 900) == RgUiCueNone, "unknown");
}

static void test_transitions(void) {
    printf("test_transitions\n");
    RgTransition t;
    memset(&t, 0, sizeof(t));
    CHECK(rg_transition_level(&t, 0) == RG_TRANSITION_DONE, "none: done");
    rg_transition_start(&t, RgTransitionWipe, 300, 1000);
    CHECK(rg_transition_level(&t, 1000) == 0, "starts hidden");
    uint8_t mid = rg_transition_level(&t, 1150);
    CHECK(mid > 100 && mid < 160, "half way");
    CHECK(rg_transition_level(&t, 1300) == RG_TRANSITION_DONE && !t.active, "done");
    rg_transition_start(&t, RgTransitionDither, 300, 1000);
    rg_transition_finish(&t);
    CHECK(rg_transition_level(&t, 1001) == RG_TRANSITION_DONE, "a key ends it at once");
    rg_transition_start(&t, (RgTransitionKind)77, 300, 0);
    CHECK(!t.active, "unknown kind does not run");
    rg_transition_start(&t, RgTransitionSlide, 0, 0);
    CHECK(rg_transition_level(&t, 1) == RG_TRANSITION_DONE, "zero length ends");
    rg_transition_start(&t, RgTransitionWipe, 300, 1000);
    CHECK(rg_transition_level(&t, 900) == 0, "clock behind the start: hidden, no wrap");

    // The dither reveals monotonically and completely.
    uint32_t prev = 4096;
    bool mono = true;
    for(int level = 0; level <= 255; level += 15) {
        uint32_t hidden = 0;
        for(uint32_t y = 0; y < 64; y++)
            for(uint32_t x = 0; x < 64; x++)
                hidden += rg_dither_hidden(x, y, (uint8_t)level);
        if(hidden > prev) mono = false;
        prev = hidden;
    }
    CHECK(mono, "monotonic");
    uint32_t all = 0, none = 0;
    for(uint32_t y = 0; y < 8; y++)
        for(uint32_t x = 0; x < 8; x++) {
            all += rg_dither_hidden(x, y, 0);
            none += rg_dither_hidden(x, y, 255);
        }
    CHECK(all == 64 && none == 0, "0 hides all, 255 none");
}

static void test_menu_window(void) {
    printf("test_menu_window\n");
    CHECK(rg_ui_menu_top(0, 0, 8, 5) == 0, "top");
    CHECK(rg_ui_menu_top(5, 0, 8, 5) == 1, "scrolls down");
    CHECK(rg_ui_menu_top(7, 1, 8, 5) == 3, "last");
    CHECK(rg_ui_menu_top(2, 3, 8, 5) == 2, "scrolls up");
    CHECK(rg_ui_menu_top(3, 0, 4, 5) == 0, "fits");
    CHECK(rg_ui_menu_top(20, 0, 8, 5) == 3, "selection past the end");
    CHECK(rg_ui_menu_top(0, 0, 0, 5) == 0, "empty");
    CHECK(rg_ui_menu_top(0, 7, 8, 5) == 0, "stale top");
    bool ok = true;
    for(uint8_t count = 1; count < 12; count++)
        for(uint8_t rows = 1; rows < 7; rows++)
            for(uint8_t top = 0; top < 12; top++)
                for(uint8_t sel = 0; sel < count; sel++) {
                    uint8_t t = rg_ui_menu_top(sel, top, count, rows);
                    if(sel < t || sel >= t + rows || (count > rows && t + rows > count))
                        ok = false;
                }
    CHECK(ok, "selection always visible, window inside the list");
}

/* Every character 5 pixels wide plus a pixel of spacing. */
static uint16_t fixed_width(void* context, const char* text, size_t len) {
    (void)context;
    (void)text;
    return len ? (uint16_t)(len * 6 - 1) : 0;
}

static void test_wrap(void) {
    printf("test_wrap\n");
    RgUiLine lines[8];
    const char* t = "Not enough memory to open this tool";
    size_t n = rg_ui_wrap(t, 60, fixed_width, NULL, lines, 8);
    bool fits = true;
    for(size_t i = 0; i < n && i < 8; i++)
        if(fixed_width(NULL, t + lines[i].start, lines[i].len) > 60) fits = false;
    CHECK(n == 4 && fits, "wrapped between words");
    CHECK(lines[0].len == 10 && strncmp(t + lines[0].start, "Not enough", 10) == 0, "first line");

    n = rg_ui_wrap("a\nb\n\nc", 60, fixed_width, NULL, lines, 8);
    CHECK(n == 4 && lines[2].len == 0, "own line breaks kept, empty line kept");
    n = rg_ui_wrap("averyveryverylongfilename_2026.sub", 60, fixed_width, NULL, lines, 8);
    CHECK(n == 4 && lines[0].len == 10, "a long word is cut by characters");
    n = rg_ui_wrap("one two three four five six seven", 30, fixed_width, NULL, lines, 2);
    CHECK(n > 2, "reports that it does not fit");
    n = rg_ui_wrap("", 60, fixed_width, NULL, lines, 8);
    CHECK(n == 1 && lines[0].len == 0, "empty text");
    n = rg_ui_wrap("x", 0, fixed_width, NULL, lines, 8);
    CHECK(n == 1 && lines[0].len == 1, "nothing fits: still progresses");
    n = rg_ui_wrap("trailing   ", 60, fixed_width, NULL, lines, 8);
    CHECK(n == 1, "trailing spaces");
}

static void test_message_kind(void) {
    printf("test_message_kind\n");
    CHECK(rg_ui_message_is_notice("Report saved"), "report saved");
    CHECK(rg_ui_message_is_notice("Session created"), "session created");
    CHECK(!rg_ui_message_is_notice("Not saved"), "not saved");
    CHECK(!rg_ui_message_is_notice("Not created"), "not created");
    CHECK(!rg_ui_message_is_notice("Already saved"), "already saved");
    CHECK(!rg_ui_message_is_notice("Save failed"), "save failed");
    CHECK(!rg_ui_message_is_notice("Not enough memory"), "memory");
    CHECK(!rg_ui_message_is_notice(NULL), "null");
}

static void test_fw_family(void) {
    printf("test_fw_family\n");
    CHECK(strcmp(rg_ui_fw_family("1.4.3"), "Official") == 0, "official");
    CHECK(strcmp(rg_ui_fw_family("mntm-012"), "Momentum") == 0, "momentum");
    CHECK(strcmp(rg_ui_fw_family("unlshd-093"), "Unleashed") == 0, "unleashed");
    CHECK(strcmp(rg_ui_fw_family("RM0912-1234-0.420.0"), "RogueMaster") == 0, "roguemaster");
    CHECK(strcmp(rg_ui_fw_family("rm-dev"), "RogueMaster") == 0, "roguemaster dev");
    CHECK(strcmp(rg_ui_fw_family(""), "Other") == 0, "empty");
    CHECK(strcmp(rg_ui_fw_family(NULL), "Other") == 0, "null");
    CHECK(strcmp(rg_ui_fw_family("custom"), "Other") == 0, "other");
}

/* ---- sprites ----------------------------------------------------------------- */

static void test_sprites(void) {
    printf("test_sprites\n");
    bool sizes = true, inside = true, ink = true;
    for(int i = 0; i < RgSpriteCount; i++) {
        const RgSprite* s = &rg_sprites[i];
        if(s->w == 0 || s->h == 0 || s->w > 64 || s->h > 64) sizes = false;
        uint32_t bytes = (uint32_t)((s->w + 7) / 8) * s->h;
        if(s->offset + bytes > RG_SPRITE_BYTES) inside = false;
        uint32_t set = 0;
        for(uint32_t b = 0; b < bytes && s->offset + b < RG_SPRITE_BYTES; b++)
            set |= rg_sprite_bits[s->offset + b];
        if(!set) ink = false;
        // Padding bits past the width stay clear.
        for(uint32_t y = 0; y < s->h; y++) {
            uint32_t stride = (s->w + 7u) / 8u;
            uint8_t last = rg_sprite_bits[s->offset + y * stride + stride - 1];
            if(s->w % 8 && (last >> (s->w % 8))) inside = false;
        }
    }
    CHECK(sizes, "sizes");
    CHECK(inside, "data inside the table, padding clear");
    CHECK(ink, "every sprite draws something");
    CHECK(rg_sprites[RgSpriteBody].w == 32 && rg_sprites[RgSpriteBody].h == 32, "body 32x32");
    CHECK(RG_SPRITE_BYTES < 1024, "under 1 KB of art");
}

/* Beta 9: celebrations, the look and the animation levels. */
static void test_celebrate_look_anim(void) {
    printf("test_celebrate_look_anim\n");
    RgMascot m;
    rg_mascot_init(&m, 0, 3);
    rg_mascot_event(&m, RgMascotEventAchievement, 0, 1000);
    CHECK(rg_mascot_state(&m, 1100) == RgMascotStateCelebrate, "an achievement celebrates");
    rg_mascot_event(&m, RgMascotEventSaveCompleted, 0, 1200);
    CHECK(rg_mascot_state(&m, 1300) == RgMascotStateCelebrate, "celebrate outranks saved");
    rg_mascot_sync(&m, false, false, true, false, 1400);
    CHECK(rg_mascot_state(&m, 1500) == RgMascotStateRecording, "recording outranks celebrate");
    rg_mascot_sync(&m, false, false, false, false, 1600);
    CHECK(rg_mascot_state(&m, 1000 + RG_MASCOT_CELEBRATE_MS) != RgMascotStateCelebrate, "short");
    rg_mascot_event(&m, RgMascotEventMatch, 0, 9000);
    CHECK(rg_mascot_state(&m, 9001) == RgMascotStateCelebrate, "a match celebrates");
    CHECK(strcmp(rg_mascot_state_name(RgMascotStateGreet), "Welcome back") == 0, "names");

    // The look is copied into every frame; a damaged look is sanitized.
    RgMascotLook look;
    rg_mascot_look_default(&look);
    CHECK(look.phones == 1 && look.antenna == RgLookAntennaClassic, "default look");
    look.goggles = RgLookGogglesOff;
    look.antenna = 99;
    look.accessory = RgLookAccessoryBowtie;
    look.expression = RgLookExpressionCheerful;
    rg_mascot_init(&m, 0, 3);
    rg_mascot_set_look(&m, &look, RgAnimNormal);
    CHECK(m.look.antenna == RgLookAntennaClassic, "out of range antenna sanitized");
    RgMascotFrame f;
    rg_mascot_frame(&m, 10, &f);
    CHECK(f.accessory == RgLookAccessoryBowtie && f.goggles == 0, "look applied");
    CHECK(f.eyes == RgMascotEyesHappy || f.eyes == RgMascotEyesBlink, "cheerful idle eyes");
    rg_mascot_event(&m, RgMascotEventOperationFailed, 0, 20);
    rg_mascot_frame(&m, 30, &f);
    CHECK(f.eyes == RgMascotEyesWorried, "reactions keep their own eyes");

    // Off: a still frame per state, information kept.
    rg_mascot_init(&m, 0, 3);
    rg_mascot_look_default(&look);
    rg_mascot_set_look(&m, &look, RgAnimOff);
    RgMascotFrame first, now;
    rg_mascot_frame(&m, 0, &first);
    bool still = true;
    for(uint32_t t = 100; t < RG_MASCOT_BORED_MS - 100; t += 100) {
        rg_mascot_frame(&m, t, &now);
        if(rg_mascot_frame_changed(&first, &now)) still = false;
    }
    CHECK(still, "Off: idle never changes the picture");
    rg_mascot_sync(&m, false, false, true, false, 50000);
    rg_mascot_frame(&m, 50000, &first);
    rg_mascot_frame(&m, 50600, &now);
    CHECK(first.fx == RgMascotFxRec && first.fx_on && now.fx_on, "Off: REC stays lit");
    CHECK(!rg_mascot_frame_changed(&first, &now), "Off: recording is still too");
    // Minimal changes less often than Normal; Full at least as often.
    uint32_t changes[RgAnimCount] = {0};
    for(uint8_t level = 0; level < RgAnimCount; level++) {
        rg_mascot_init(&m, 0, 77);
        rg_mascot_set_look(&m, &look, level);
        RgMascotFrame prev;
        rg_mascot_frame(&m, 0, &prev);
        for(uint32_t t = 100; t < 40000; t += 100) {
            rg_mascot_frame(&m, t, &now);
            if(rg_mascot_frame_changed(&prev, &now)) changes[level]++;
            prev = now;
        }
    }
    printf(
        "  idle redraws in 40 s: Off %u, Minimal %u, Normal %u, Full %u\n",
        (unsigned)changes[0],
        (unsigned)changes[1],
        (unsigned)changes[2],
        (unsigned)changes[3]);
    CHECK(changes[RgAnimOff] == 0, "Off: no idle redraws");
    CHECK(changes[RgAnimMinimal] < changes[RgAnimNormal], "Minimal redraws less");
    CHECK(changes[RgAnimFull] >= changes[RgAnimNormal], "Full redraws at least as much");
    CHECK(strcmp(rg_anim_level_name(RgAnimMinimal), "Minimal") == 0, "level names");
}

int main(void) {
    test_celebrate_look_anim();
    test_idle_and_priorities();
    test_recording_follows_the_recorder();
    test_repeated_events_do_not_queue();
    test_boredom_and_sleep();
    test_analysis_progress();
    test_unknown_events_and_determinism();
    test_poses();
    test_styles();
    test_feedback();
    test_transitions();
    test_menu_window();
    test_wrap();
    test_message_kind();
    test_fw_family();
    test_sprites();
    printf("\n%d checks, %d failures\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
