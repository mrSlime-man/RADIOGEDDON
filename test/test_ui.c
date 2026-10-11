/**
 * Offline UI tests (make -C test ui): RadioGeddon's Waterfall and Bitstream
 * Explorer screens drawn into a host 128x64 canvas with the firmware's own
 * fonts (test/ui/canvas_host.c), and driven with key events.
 *
 * Checked on every screen state: nothing is drawn off the screen and no text
 * runs past its edge; the waterfall's columns land on the pixels the mapping
 * says; navigation keeps its cursors in range and never mixes up a long
 * press with a repeat. Each state is also written as a PBM image under
 * build/ui/previews: SYNTHETIC development previews of drawing code fed with
 * synthetic data, not screenshots of a Flipper Zero, never to be used as
 * catalog screenshots.
 */
#include "ui/canvas_host.h"
#include "../views/radiogeddon_waterfall_view.h"
#include "../views/radiogeddon_bits_view.h"
#include "../views/radiogeddon_db_view.h"
#include "../helpers/rg_glyph.h"
#include "synth.h"

#include <stdio.h>
#include <stdlib.h>
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

#ifndef PREVIEW_DIR
#define PREVIEW_DIR "build/ui/previews"
#endif

static int g_previews = 0;

/* Draw, check the screen's edges, keep a preview. */
static void render(View* view, const char* name) {
    Canvas* c = canvas_host();
    canvas_host_reset(c);
    view_host_draw(view, c);
    char msg[160];
    snprintf(msg, sizeof(msg), "%s: no pixel off the screen", name);
    CHECK(canvas_host_clipped(c) == 0, msg);
    snprintf(
        msg, sizeof(msg), "%s: no text past the edge (%s)", name, canvas_host_last_overflow(c));
    CHECK(canvas_host_text_overflows(c) == 0, msg);
    char path[160];
    snprintf(path, sizeof(path), PREVIEW_DIR "/synthetic_%s.pbm", name);
    if(canvas_host_write_pbm(c, path)) g_previews++;
}

/* ---- fonts and glyphs ----------------------------------------------------- */

static void test_fonts(void) {
    printf("test_fonts\n");
    Canvas* c = canvas_host();
    canvas_host_reset(c);
    canvas_set_font(c, FontSecondary);
    uint16_t w = canvas_string_width(c, "RSSI sweep");
    CHECK(w > 25 && w < 60, "FontSecondary width plausible");
    CHECK(canvas_string_width(c, "") == 0, "empty string");
    CHECK(canvas_string_width(c, "ii") < canvas_string_width(c, "WW"), "proportional");
    canvas_draw_str(c, 0, 8, "0");
    CHECK(canvas_host_count(c, 0, 0, 8, 9) > 5, "a digit draws ink above the baseline");
    CHECK(canvas_host_count(c, 0, 9, 128, 55) == 0, "nothing below the baseline for 0");
    canvas_host_reset(c);
    canvas_set_font(c, FontSecondary);
    canvas_draw_str(c, 120, 8, "overflowing text");
    CHECK(canvas_host_text_overflows(c) == 1, "text past the right edge is detected");
    canvas_host_reset(c);
    canvas_set_font(c, FontPrimary);
    canvas_draw_str_aligned(c, 127, 8, AlignRight, AlignBottom, "Right");
    CHECK(
        canvas_host_text_overflows(c) == 0 && canvas_host_pixel(c, 127, 7) == false,
        "right aligned ends at the edge");
    CHECK(canvas_host_count(c, 100, 0, 28, 9) > 10, "right aligned text drawn");

    // Glyphs: every one distinct and inside 3x5; unknown characters show '?'.
    const char* set = "0123456789ABCDEF.X?-:+";
    bool distinct = true;
    for(const char* a = set; *a; a++)
        for(const char* b = a + 1; *b; b++)
            if(memcmp(rg_glyph_rows(*a), rg_glyph_rows(*b), RG_GLYPH_H) == 0) distinct = false;
    CHECK(distinct, "glyphs distinct");
    bool inside = true;
    for(const char* a = set; *a; a++)
        for(int y = 0; y < RG_GLYPH_H; y++)
            if(rg_glyph_rows(*a)[y] >> RG_GLYPH_W) inside = false;
    CHECK(inside, "glyphs 3 pixels wide");
    CHECK(rg_glyph_rows('Z') == rg_glyph_rows('?'), "unknown -> ?");
    CHECK(rg_glyph_rows('a') == rg_glyph_rows('A'), "lower-case hex");
    CHECK(!rg_glyph_pixel('8', 3, 0) && !rg_glyph_pixel('8', 0, 5), "outside the cell");
    CHECK(rg_glyph_text_width(24) == 95 && rg_glyph_text_width(0) == 0, "text width");
}

/* ---- Waterfall ------------------------------------------------------------ */

/* The Waterfall view asks the range engine for its frame; here a synthetic
 * history stands in for the engine (no radio, no thread). */
static uint8_t g_wf_buf[16384];
static RgWaterfall g_wf;
static int8_t g_floor = -100;
static bool g_paused;
static bool g_calibrating;
static uint32_t g_wf_points;

static void fake_frame(
    void* instance,
    const RadioGeddonWaterfallRequest* request,
    RadioGeddonWaterfallFrame* out) {
    (void)instance;
    RgWaterfall* wf = &g_wf;
    memset(out, 0, sizeof(*out));
    out->points = g_wf_points;
    out->paused = g_paused;
    out->calibrating = g_calibrating;
    out->floor = g_floor;
    out->threshold_db = 10;
    out->estimate_ms = rg_range_sweep_ms(g_wf_points, 5);
    out->sweep_ms = wf->filled ? 1300 : 0;
    out->columns = wf->columns;
    out->rows = wf->rows;
    out->filled = wf->filled;
    out->stored = wf->sweeps;
    out->max_scroll = rg_waterfall_max_scroll(wf, RADIOGEDDON_WF_VIEW_H);
    out->scroll = request->scroll > out->max_scroll ? out->max_scroll : request->scroll;
    out->cursor = wf->columns && request->cursor >= wf->columns ? wf->columns - 1 :
                                                                  request->cursor;
    RgWaterfallStyle st = {
        .span_db = request->span_db,
        .strong_db = 30,
        .noise_comp = request->noise_comp,
        .floors = NULL,
        .floor_all = g_floor,
    };
    out->drawn = rg_waterfall_render(
        wf, &st, out->scroll, out->xbm, RADIOGEDDON_WF_VIEW_W, RADIOGEDDON_WF_VIEW_H);
    rg_waterfall_column_span(
        wf, out->cursor, RADIOGEDDON_WF_VIEW_W, &out->cursor_x, &out->cursor_w);
    out->cursor_hz = 433000000u + out->cursor * 25000u;
    out->cursor_dbm = rg_waterfall_cell(wf, out->scroll, out->cursor);
    out->cursor_peak = rg_waterfall_cell(wf, 0, out->cursor);
    out->have_peak = rg_waterfall_peak(wf, &out->peak_column, &out->peak_age, &out->peak_dbm);
    out->peak_hz = 433000000u + out->peak_column * 25000u;
    out->top_age_ms = out->scroll * 1300u;
    if(wf->columns > 2) out->seg_start[64 / 8] |= 1u; // a band segment starts at x=64
}

static int g_wf_events[8];
static void wf_cb(RadioGeddonWaterfallEvent event, void* context) {
    (void)context;
    g_wf_events[event]++;
}

/* Fill the history: @p sweeps sweeps, a strong signal on point @p hot. */
static void wf_fill(uint32_t points, uint16_t sweeps, uint32_t hot) {
    g_wf_points = points;
    rg_waterfall_init(&g_wf, g_wf_buf, sizeof(g_wf_buf), points);
    for(uint16_t s = 0; s < sweeps; s++) {
        for(uint32_t i = 0; i < points; i++) {
            float dbm = -100.0f + (float)((i * 7 + s * 3) % 9);
            if(i == hot && s % 3 == 0) dbm = -45.0f;
            if(i + 1 == hot && s % 5 == 0) dbm = -82.0f; // a weak neighbour
            if(s == 7 && i % 4 == 0) continue; // a sweep with missing points
            rg_waterfall_add(&g_wf, i, dbm);
        }
        rg_waterfall_commit(&g_wf, 1000u + s * 1300u);
    }
}

static void test_waterfall_view(void) {
    printf("test_waterfall_view\n");
    RadioGeddonWaterfallView* v = radiogeddon_waterfall_view_alloc();
    View* view = radiogeddon_waterfall_view_get_view(v);
    radiogeddon_waterfall_view_set_callback(v, wf_cb, NULL);
    void* engine = NULL;

    // Before the first sweep: the wait message with the sweep-time estimate.
    wf_fill(128, 0, 0);
    g_calibrating = true;
    radiogeddon_waterfall_view_update(v, fake_frame, engine);
    render(view, "waterfall_waiting");
    CHECK(canvas_host_count(canvas_host(), 0, 20, 128, 30) > 50, "wait message drawn");
    g_calibrating = false;

    // A full history, live: the hot column is solid on the newest row.
    uint32_t hot = 64;
    wf_fill(128, 120, hot);
    radiogeddon_waterfall_view_update(v, fake_frame, engine);
    render(view, "waterfall_live");
    Canvas* c = canvas_host();
    CHECK(canvas_host_pixel(c, 64, 11), "strong reading solid on the newest row (x=64, y=11)");
    // The peak tick in the marker row sits over the hot column.
    CHECK(canvas_host_pixel(c, 64, 9), "peak marker over its column");
    // Header and footer text inside their bands.
    CHECK(canvas_host_count(c, 0, 0, 60, 9) > 20, "header label drawn");
    CHECK(canvas_host_count(c, 0, 56, 128, 8) > 20, "footer drawn");

    // Cursor moves: Right wraps at the end, a held key moves faster, and no
    // long-press action fires while holding it.
    for(int i = 0; i < 127; i++)
        view_host_input(view, InputKeyRight, InputTypeShort);
    radiogeddon_waterfall_view_update(v, fake_frame, engine);
    CHECK(radiogeddon_waterfall_view_get_cursor(v) == 127, "cursor at the last column");
    render(view, "waterfall_cursor_right_edge");
    view_host_input(view, InputKeyRight, InputTypeShort);
    CHECK(radiogeddon_waterfall_view_get_cursor(v) == 0, "wraps to the first column");
    view_host_input(view, InputKeyRight, InputTypeLong);
    view_host_input(view, InputKeyRight, InputTypeRepeat);
    CHECK(radiogeddon_waterfall_view_get_cursor(v) == 8, "held: 4 columns a step");
    int before = g_wf_events[RadioGeddonWaterfallEventSave] +
                 g_wf_events[RadioGeddonWaterfallEventSettings];
    CHECK(before == 0, "holding Right fires no action");

    // Scrolling: Down goes back in time, clamped to the history.
    for(int i = 0; i < 100; i++)
        view_host_input(view, InputKeyDown, InputTypeRepeat);
    radiogeddon_waterfall_view_update(v, fake_frame, engine);
    render(view, "waterfall_scrolled_oldest");
    view_host_input(view, InputKeyUp, InputTypeShort);
    radiogeddon_waterfall_view_update(v, fake_frame, engine);
    render(view, "waterfall_scrolled");

    // OK pauses; holding OK opens the menu; Back closes the menu first.
    view_host_input(view, InputKeyOk, InputTypeShort);
    CHECK(g_wf_events[RadioGeddonWaterfallEventTogglePause] == 1, "OK: pause event");
    g_paused = true;
    radiogeddon_waterfall_view_update(v, fake_frame, engine);
    render(view, "waterfall_paused");
    view_host_input(view, InputKeyOk, InputTypeLong);
    render(view, "waterfall_menu");
    CHECK(view_host_input(view, InputKeyBack, InputTypeShort), "Back closes the menu (consumed)");
    CHECK(!view_host_input(view, InputKeyBack, InputTypeShort), "Back then leaves the screen");
    // Menu: Cursor to peak, then Newest sweeps, then Receive here.
    view_host_input(view, InputKeyOk, InputTypeLong);
    view_host_input(view, InputKeyDown, InputTypeShort);
    view_host_input(view, InputKeyOk, InputTypeShort);
    CHECK(radiogeddon_waterfall_view_get_cursor(v) == hot, "cursor to peak");
    view_host_input(view, InputKeyOk, InputTypeLong);
    view_host_input(view, InputKeyDown, InputTypeShort);
    view_host_input(view, InputKeyDown, InputTypeShort);
    view_host_input(view, InputKeyOk, InputTypeShort);
    radiogeddon_waterfall_view_update(v, fake_frame, engine);
    render(view, "waterfall_newest");
    view_host_input(view, InputKeyOk, InputTypeLong);
    view_host_input(view, InputKeyOk, InputTypeShort);
    CHECK(g_wf_events[RadioGeddonWaterfallEventReceive] == 1, "Receive here");
    CHECK(
        radiogeddon_waterfall_view_get_cursor_hz(v) == 433000000u + hot * 25000u, "its frequency");
    // Sensitivity cycles both ways and wraps; floor compensation toggles.
    view_host_input(view, InputKeyOk, InputTypeLong);
    for(int i = 0; i < 3; i++)
        view_host_input(view, InputKeyDown, InputTypeShort);
    uint8_t span = 9;
    bool comp = false;
    for(unsigned i = 0; i < RADIOGEDDON_WF_SPANS; i++)
        view_host_input(view, InputKeyRight, InputTypeShort);
    radiogeddon_waterfall_view_get_style(v, &span, &comp);
    CHECK(span == 2, "a full cycle returns to the same span");
    view_host_input(view, InputKeyLeft, InputTypeShort);
    radiogeddon_waterfall_view_get_style(v, &span, &comp);
    CHECK(span == 1, "Left steps back");
    view_host_input(view, InputKeyDown, InputTypeShort);
    view_host_input(view, InputKeyOk, InputTypeShort);
    radiogeddon_waterfall_view_get_style(v, &span, &comp);
    CHECK(!comp, "floor compensation off");
    render(view, "waterfall_menu_settings");
    CHECK(
        g_wf_events[RadioGeddonWaterfallEventSettings] == RADIOGEDDON_WF_SPANS + 2,
        "settings events");
    view_host_input(view, InputKeyDown, InputTypeShort);
    view_host_input(view, InputKeyOk, InputTypeShort);
    CHECK(g_wf_events[RadioGeddonWaterfallEventSave] == 1, "Save history CSV");
    g_paused = false;

    // Extreme widths: one point, three points, external radio, all strong.
    radiogeddon_waterfall_view_set_external(v, true);
    wf_fill(1, 60, 0);
    radiogeddon_waterfall_view_update(v, fake_frame, engine);
    render(view, "waterfall_one_point_ext");
    CHECK(
        canvas_host_pixel(canvas_host(), 0, 11) && canvas_host_pixel(canvas_host(), 127, 11),
        "one point spans the picture");
    wf_fill(3, 60, 1);
    radiogeddon_waterfall_view_update(v, fake_frame, engine);
    render(view, "waterfall_three_points");
    // Point 1 of 3 covers pixels 43..85.
    CHECK(
        canvas_host_pixel(canvas_host(), 43, 11) && canvas_host_pixel(canvas_host(), 85, 11),
        "middle column pixels");
    wf_fill(256, 255, 200);
    radiogeddon_waterfall_view_update(v, fake_frame, engine);
    render(view, "waterfall_256_points");
    radiogeddon_waterfall_view_set_external(v, false);
    // Empty plan: no columns.
    g_wf_points = 0;
    memset(&g_wf, 0, sizeof(g_wf));
    radiogeddon_waterfall_view_update(v, fake_frame, engine);
    render(view, "waterfall_no_points");
    radiogeddon_waterfall_view_free(v);
}

/* ---- Bitstream Explorer --------------------------------------------------- */

static RgAnalysis g_r;
static int32_t g_samples[60000];

static void analysis_of(const char* const* frames, size_t count, int repeats) {
    size_t pos = 0;
    for(int r = 0; r < repeats; r++)
        for(size_t i = 0; i < count; i++)
            synth_pwm(g_samples, &pos, frames[i], 2);
    rg_analyzer_run(g_samples, pos, &g_r);
}

static void all_modes(RadioGeddonBitsView* v, const char* prefix) {
    View* view = radiogeddon_bits_view_get_view(v);
    static const char* const names[] = {"frames", "bits", "hex", "diff", "field"};
    for(int m = 0; m < RadioGeddonBitsModeCount; m++) {
        radiogeddon_bits_view_set_mode(v, (RadioGeddonBitsMode)m);
        char name[64];
        snprintf(name, sizeof(name), "%s_%s", prefix, names[m]);
        render(view, name);
    }
}

static void test_bits_view(void) {
    printf("test_bits_view\n");
    RadioGeddonBitsView* v = radiogeddon_bits_view_alloc();
    View* view = radiogeddon_bits_view_get_view(v);

    // No analysis / no frames: an explanation, not an empty screen.
    radiogeddon_bits_view_set_analysis(v, NULL);
    render(view, "bits_empty");
    CHECK(canvas_host_count(canvas_host(), 0, 15, 128, 40) > 50, "empty explained");

    // Four button presses of a 24-bit remote, sent twice each.
    const char* frames[] = {
        "101100111000101011110001",
        "101100111000101011110010",
        "101100111000101011110100",
        "101100111000101011111000"};
    analysis_of(frames, 4, 2);
    radiogeddon_bits_view_set_analysis(v, &g_r);
    all_modes(v, "bits_remote");

    // OK cycles the five views and returns.
    radiogeddon_bits_view_set_mode(v, RadioGeddonBitsModeFrames);
    for(int i = 0; i < RadioGeddonBitsModeCount; i++)
        view_host_input(view, InputKeyOk, InputTypeShort);
    CHECK(radiogeddon_bits_view_get_mode(v) == RadioGeddonBitsModeFrames, "OK cycles 5 views");
    CHECK(!view_host_input(view, InputKeyBack, InputTypeShort), "Back leaves");

    // Hold OK in BITS: a field starting at the cursor.
    radiogeddon_bits_view_set_mode(v, RadioGeddonBitsModeBits);
    for(int i = 0; i < 20; i++)
        view_host_input(view, InputKeyRight, InputTypeShort);
    view_host_input(view, InputKeyOk, InputTypeLong);
    CHECK(radiogeddon_bits_view_get_mode(v) == RadioGeddonBitsModeField, "hold OK: field");
    render(view, "bits_remote_field_at_20");

    // Long frames (256 bits after repeat cutting: 64-bit frames, 40 of them)
    // and many frames: scrolling to the ends never leaves the screen.
    char longf[4][65];
    for(int f = 0; f < 4; f++) {
        for(int i = 0; i < 64; i++)
            longf[f][i] = ((i * 5 + f * 3) % 7) < 3 ? '1' : '0';
        longf[f][64] = '\0';
    }
    const char* lf[] = {longf[0], longf[1], longf[2], longf[3]};
    analysis_of(lf, 4, 12);
    CHECK(g_r.frames_kept == RG_ANALYZER_MAX_FRAMES, "48 frames kept");
    radiogeddon_bits_view_set_analysis(v, &g_r);
    radiogeddon_bits_view_set_mode(v, RadioGeddonBitsModeFrames);
    for(int i = 0; i < 47; i++)
        view_host_input(view, InputKeyDown, InputTypeRepeat);
    render(view, "bits_long_frames_last");
    all_modes(v, "bits_long");
    radiogeddon_bits_view_set_mode(v, RadioGeddonBitsModeBits);
    for(int i = 0; i < 20; i++)
        view_host_input(view, InputKeyDown, InputTypeRepeat);
    render(view, "bits_long_bits_end");
    radiogeddon_bits_view_set_mode(v, RadioGeddonBitsModeDiff);
    for(int i = 0; i < 20; i++)
        view_host_input(view, InputKeyDown, InputTypeRepeat);
    render(view, "bits_long_diff_end");
    // HEX: every byte offset, the byte cursor at both ends.
    radiogeddon_bits_view_set_mode(v, RadioGeddonBitsModeHex);
    for(int a = 0; a < 8; a++) {
        view_host_input(view, InputKeyLeft, InputTypeShort); // wraps to the last byte
        char name[48];
        snprintf(name, sizeof(name), "bits_long_hex_offset%d", a);
        render(view, name);
        view_host_input(view, InputKeyOk, InputTypeLong);
    }
    // FIELD: the end never passes the frame, the start never passes the end.
    radiogeddon_bits_view_set_mode(v, RadioGeddonBitsModeField);
    for(int i = 0; i < 300; i++)
        view_host_input(view, InputKeyRight, InputTypeRepeat);
    for(int i = 0; i < 300; i++)
        view_host_input(view, InputKeyDown, InputTypeRepeat);
    render(view, "bits_long_field_last_bit");
    for(int i = 0; i < 300; i++)
        view_host_input(view, InputKeyUp, InputTypeRepeat);
    render(view, "bits_long_field_whole");

    // Random key sequences: every state the keys reach draws inside the screen.
    synth_rng_state = 99;
    static const InputKey keys[] = {
        InputKeyUp, InputKeyDown, InputKeyLeft, InputKeyRight, InputKeyOk};
    static const InputType types[] = {InputTypeShort, InputTypeLong, InputTypeRepeat};
    uint32_t bad = 0;
    for(int i = 0; i < 3000; i++) {
        view_host_input(view, keys[synth_rng() % 5], types[synth_rng() % 3]);
        Canvas* c = canvas_host();
        canvas_host_reset(c);
        view_host_draw(view, c);
        if(canvas_host_clipped(c) || canvas_host_text_overflows(c)) {
            if(!bad) printf("  random keys: %s\n", canvas_host_last_overflow(c));
            bad++;
        }
    }
    CHECK(bad == 0, "3000 random keys: every state on screen");

    // A noisy capture: frames that are noise say so.
    size_t pos = 0;
    for(int i = 0; i < 6; i++) {
        synth_pwm(g_samples, &pos, "110010101100", 2);
        for(int k = 0; k < 20; k++)
            g_samples[pos++] = (k % 2) ? -(int32_t)(150 + synth_rng() % 2000) :
                                         (int32_t)(150 + synth_rng() % 2000);
        g_samples[pos++] = -20000;
    }
    rg_analyzer_run(g_samples, pos, &g_r);
    radiogeddon_bits_view_set_analysis(v, &g_r);
    all_modes(v, "bits_noisy");
    radiogeddon_bits_view_free(v);
}

/* ---- Database list ------------------------------------------------------- */

static RadioGeddonDb* db_make(size_t count, size_t name_len) {
    RadioGeddonDb* db = calloc(1, sizeof(RadioGeddonDb));
    size_t cap = count ? count : 1;
    RgDbEntry* entries = calloc(cap, sizeof(RgDbEntry));
    char* names = calloc(cap, 72);
    db->view = calloc(cap, sizeof(uint16_t));
    rg_db_init(&db->db, entries, cap, names, cap * 72);
    char name[72];
    for(size_t i = 0; i < count; i++) {
        size_t n = 0;
        n += (size_t)snprintf(name, sizeof(name), "%03u_", (unsigned)i);
        while(n < name_len && n + 1 < sizeof(name)) {
            name[n] = (char)('a' + (i + n) % 26);
            n++;
        }
        name[n] = '\0';
        strncat(name, ".sub", sizeof(name) - strlen(name) - 1);
        RgDbEntry* e = rg_db_add(&db->db, name);
        if(!e) break;
        e->kind = (i % 7 == 3) ? RgDbKindCorrupt : (i % 2) ? RgDbKindRaw : RgDbKindProtocol;
        e->frequency = 433920000u + (uint32_t)i * 10000u;
        e->mtime = 1760000000u + (uint32_t)i * 60u;
        e->bits = 24;
        snprintf(
            e->protocol, sizeof(e->protocol), "%s", e->kind == RgDbKindRaw ? "RAW" : "Princeton");
    }
    db->total_files = count;
    db->query.sort = RgDbSortDate;
    db->query.show = RgDbShowAll;
    radiogeddon_db_apply(db);
    return db;
}

static void db_free_test(RadioGeddonDb* db) {
    free(db->db.entries);
    free(db->db.names);
    free(db->view);
    free(db);
}

static void test_db_view(void) {
    printf("test_db_view\n");
    RadioGeddonDbView* v = radiogeddon_db_view_alloc();
    View* view = radiogeddon_db_view_get_view(v);
    render(view, "db_loading");

    // Empty: a message, not a blank list.
    RadioGeddonDb* empty = db_make(0, 0);
    radiogeddon_db_view_set_db(v, empty);
    render(view, "db_empty");
    CHECK(canvas_host_count(canvas_host(), 0, 12, 128, 38) > 30, "empty list explained");

    // Very long names (60 characters): cut to fit, never past the edge.
    RadioGeddonDb* longn = db_make(5, 60);
    radiogeddon_db_view_set_db(v, longn);
    render(view, "db_long_names");
    radiogeddon_db_view_set_selected(v, 4);
    render(view, "db_long_names_last");

    // A full list: the cursor walks to the end and wraps; every step on screen.
    RadioGeddonDb* many = db_make(200, 20);
    radiogeddon_db_view_set_db(v, many);
    uint32_t bad = 0;
    for(int i = 0; i < 205; i++) {
        view_host_input(view, InputKeyDown, i % 3 ? InputTypeRepeat : InputTypeShort);
        Canvas* c = canvas_host();
        canvas_host_reset(c);
        view_host_draw(view, c);
        if(canvas_host_clipped(c) || canvas_host_text_overflows(c)) bad++;
    }
    CHECK(bad == 0, "200 files, every position on screen");
    radiogeddon_db_view_set_selected(v, 199);
    render(view, "db_200_files_last");
    CHECK(radiogeddon_db_view_get_selected(v) == 199, "last selected");
    radiogeddon_db_view_set_db(v, NULL);
    radiogeddon_db_view_free(v);
    db_free_test(empty);
    db_free_test(longn);
    db_free_test(many);
}

int main(void) {
    test_fonts();
    test_db_view();
    test_waterfall_view();
    test_bits_view();
    printf(
        "%d previews written to %s (synthetic, not device screenshots)\n",
        g_previews,
        PREVIEW_DIR);
    printf("%d checks, %d failures\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
