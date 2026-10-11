#include "radiogeddon_room_view.h"

#if RG_FEATURE_DOLPHIN_LAB

#include "radiogeddon_mascot_draw.h"
#include "../helpers/rg_ui.h"
#include <furi.h>

#define ROOM_SLIDE_MS 250u
#define SCOPE_X       44
#define SCOPE_Y       14
#define SCOPE_H       26

struct RadioGeddonRoomView {
    View* view;
    RadioGeddonRoomCallback callback;
    void* context;
};

typedef struct {
    uint8_t station;
    uint8_t gallery; // RgMascotState
    int8_t slide_dir;
    uint8_t level; // slide-in, RG_TRANSITION_DONE when still
    RgTransition slide;
    uint8_t phase; // radar sweep
    uint32_t phase_ms;
    RgMascotFrame frame;
    uint32_t recordings;
    char lines[RADIOGEDDON_ROOM_LINES][RADIOGEDDON_ROOM_LINE_LEN];
    RadioGeddonRoomTrace scope;
} RoomModel;

static const char* const room_names[RadioGeddonRoomCount] = {
    "Radar",
    "Oscilloscope",
    "Antenna",
    "Recorder",
    "Terminal",
    "Gallery",
};

/* A line centred in the station's area, cut to fit it. */
static void room_line(Canvas* canvas, int32_t y, const char* text) {
    char buf[32];
    strlcpy(buf, text, sizeof(buf));
    size_t len = strlen(buf);
    while(len > 0 && canvas_string_width(canvas, buf) > 82)
        buf[--len] = '\0';
    canvas_draw_str_aligned(canvas, 86, y, AlignCenter, AlignBottom, buf);
}

static void room_hint(Canvas* canvas, const char* line1, const char* line2) {
    canvas_set_font(canvas, FontSecondary);
    if(line1) room_line(canvas, 52, line1);
    if(line2) room_line(canvas, 61, line2);
}

static void room_draw_scope(Canvas* canvas, RoomModel* m) {
    int32_t x0 = SCOPE_X;
    canvas_draw_rframe(canvas, x0, SCOPE_Y, RADIOGEDDON_ROOM_SCOPE_W + 4, SCOPE_H, 2);
    for(int32_t gx = 4; gx < RADIOGEDDON_ROOM_SCOPE_W; gx += 10)
        for(int32_t gy = SCOPE_Y + 4; gy < SCOPE_Y + SCOPE_H - 2; gy += 5)
            canvas_draw_dot(canvas, x0 + 2 + gx, gy);
    const RadioGeddonRoomTrace* s = &m->scope;
    if(!s->loaded || s->empty) {
        room_hint(canvas, s->empty ? "No RAW samples" : "OK: open a RAW", "recording");
        return;
    }
    // The recording as recorded: high where the carrier was on.
    int32_t hi = SCOPE_Y + 6, lo = SCOPE_Y + SCOPE_H - 6;
    bool prev = false;
    for(int32_t i = 0; i < RADIOGEDDON_ROOM_SCOPE_W; i++) {
        bool on = s->bits[i / 8] & (1u << (i % 8));
        int32_t x = x0 + 2 + i;
        if(i > 0 && on != prev) canvas_draw_line(canvas, x, hi, x, lo);
        canvas_draw_dot(canvas, x, on ? hi : lo);
        prev = on;
    }
    char span[24];
    if(s->span_us >= 10000) {
        snprintf(span, sizeof(span), "first %lu ms", (unsigned long)(s->span_us / 1000));
    } else {
        snprintf(span, sizeof(span), "first %lu us", (unsigned long)s->span_us);
    }
    room_hint(canvas, s->name, span);
}

/* While a new station comes in, the part not yet reached is blank and an
 * oscilloscope line marks the edge. Nothing is drawn off the screen. */
static void room_draw_wipe(Canvas* canvas, RoomModel* m, int32_t left) {
    if(m->level == RG_TRANSITION_DONE) return;
    int32_t w = 128 - left;
    int32_t edge = (int32_t)m->level * w / 255;
    int32_t x0, x1, line;
    if(m->slide_dir > 0) {
        // Coming from the right: revealed from the left edge.
        x0 = left + edge;
        x1 = 127;
        line = x0;
    } else {
        x0 = left;
        x1 = 127 - edge;
        line = x1;
    }
    if(x1 >= x0) {
        canvas_set_color(canvas, ColorWhite);
        canvas_draw_box(canvas, x0, 11, (size_t)(x1 - x0 + 1), 53);
        canvas_set_color(canvas, ColorBlack);
    }
    if(line >= left && line <= 127) {
        for(int32_t y = 11; y < 64; y++) {
            static const int8_t wiggle[8] = {0, 1, 1, 0, 0, -1, -1, 0};
            int32_t x = line + wiggle[y & 7];
            if(x >= left && x <= 127) canvas_draw_dot(canvas, x, y);
        }
    }
}

static void room_draw(Canvas* canvas, void* model) {
    RoomModel* m = model;
    canvas_clear(canvas);
    canvas_set_color(canvas, ColorBlack);

    // Header: the station's name between arrows.
    canvas_draw_box(canvas, 0, 0, 128, 10);
    canvas_set_color(canvas, ColorWhite);
    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str(canvas, 2, 8, "<");
    canvas_draw_str_aligned(canvas, 125, 8, AlignRight, AlignBottom, ">");
    char title[32];
    snprintf(
        title, sizeof(title), "DOLPHIN LAB: %s", room_names[m->station % RadioGeddonRoomCount]);
    canvas_draw_str_aligned(canvas, 64, 8, AlignCenter, AlignBottom, title);
    canvas_set_color(canvas, ColorBlack);

    // Stations are drawn in place; a wipe from the side it was reached from
    // reveals the new one (room_draw_wipe, last).

    if(m->station == RadioGeddonRoomTerminal) {
        // Full width: a terminal of figures read from the SD card.
        canvas_draw_rframe(canvas, 1, 12, 126, 52, 2);
        canvas_set_font(canvas, FontSecondary);
        for(int i = 0; i < RADIOGEDDON_ROOM_LINES; i++)
            canvas_draw_str(canvas, 5, 22 + i * 9, m->lines[i]);
        room_draw_wipe(canvas, m, 0);
        return;
    }

    rg_draw_mascot(canvas, 0, 24, &m->frame, 64, true);
    switch(m->station) {
    case RadioGeddonRoomRadar:
        rg_draw_radar(canvas, 86, 30, 15, m->phase);
        rg_draw_radar(canvas, 86, 30, 9, m->phase);
        room_hint(canvas, "Illustration", "OK: real Scanner");
        break;
    case RadioGeddonRoomScope:
        room_draw_scope(canvas, m);
        break;
    case RadioGeddonRoomAntenna: {
        int32_t ax = 86;
        // A mast with its waves (drawn, not measured).
        canvas_draw_line(canvas, ax, 25, ax, 44);
        canvas_draw_line(canvas, ax - 6, 44, ax + 6, 44);
        canvas_draw_line(canvas, ax, 30, ax - 4, 37);
        canvas_draw_line(canvas, ax, 30, ax + 4, 37);
        canvas_draw_disc(canvas, ax, 24, 1);
        uint8_t arcs = (uint8_t)(m->phase % 4u);
        for(uint8_t k = 0; k < arcs; k++)
            canvas_draw_circle(canvas, ax, 24, 4u + 4u * k);
        room_hint(canvas, "Illustration", "OK: Receive");
        break;
    }
    case RadioGeddonRoomRecorder: {
        rg_draw_prop(canvas, 79, 36, RgMascotPropRecorder, m->phase, RG_MASCOT_PROGRESS_NONE);
        char count[24];
        snprintf(count, sizeof(count), "%lu recordings", (unsigned long)m->recordings);
        room_hint(canvas, count, "OK: Database");
        break;
    }
    case RadioGeddonRoomGallery: {
        char name[24];
        snprintf(
            name,
            sizeof(name),
            "pose %u of %u",
            (unsigned)(m->gallery + 1),
            (unsigned)RgMascotStateCount);
        canvas_set_font(canvas, FontPrimary);
        canvas_draw_str_aligned(
            canvas,
            86,
            22,
            AlignCenter,
            AlignBottom,
            rg_mascot_state_name((RgMascotState)m->gallery));
        canvas_set_font(canvas, FontSecondary);
        canvas_draw_str_aligned(canvas, 86, 31, AlignCenter, AlignBottom, name);
        rg_draw_prop(canvas, 96, 46, m->frame.prop, m->frame.prop_phase, m->frame.progress);
        room_hint(canvas, NULL, "Up/Down: pose");
        break;
    }
    default:
        break;
    }
    room_draw_wipe(canvas, m, 44);
}

static bool room_input(InputEvent* event, void* context) {
    RadioGeddonRoomView* instance = context;
    if(event->key == InputKeyBack) return false;
    if(event->type != InputTypeShort && event->type != InputTypeRepeat) return true;
    uint8_t station = 0;
    bool ok = false;
    with_view_model(
        instance->view,
        RoomModel * m,
        {
            switch(event->key) {
            case InputKeyLeft:
            case InputKeyRight: {
                bool right = event->key == InputKeyRight;
                m->station =
                    right ?
                        (uint8_t)((m->station + 1u) % RadioGeddonRoomCount) :
                        (uint8_t)((m->station + RadioGeddonRoomCount - 1u) % RadioGeddonRoomCount);
                m->slide_dir = right ? 1 : -1;
                // The slide starts on the next update (it knows the time).
                m->level = 0;
                m->slide.active = 0;
                break;
            }
            case InputKeyUp:
            case InputKeyDown:
                if(m->station == RadioGeddonRoomGallery) {
                    bool up = event->key == InputKeyUp;
                    m->gallery = up ? (uint8_t)((m->gallery + RgMascotStateCount - 1u) %
                                                RgMascotStateCount) :
                                      (uint8_t)((m->gallery + 1u) % RgMascotStateCount);
                }
                break;
            case InputKeyOk:
                ok = event->type == InputTypeShort;
                m->level = RG_TRANSITION_DONE; // a key never waits for the slide
                break;
            default:
                break;
            }
            station = m->station;
        },
        true);
    if(ok && instance->callback) instance->callback(station, instance->context);
    return true;
}

RadioGeddonRoomView* radiogeddon_room_view_alloc(void) {
    RadioGeddonRoomView* instance = malloc(sizeof(RadioGeddonRoomView));
    instance->view = view_alloc();
    instance->callback = NULL;
    instance->context = NULL;
    view_set_context(instance->view, instance);
    view_allocate_model(instance->view, ViewModelTypeLocking, sizeof(RoomModel));
    view_set_draw_callback(instance->view, room_draw);
    view_set_input_callback(instance->view, room_input);
    with_view_model(
        instance->view,
        RoomModel * m,
        {
            memset(m, 0, sizeof(*m));
            m->level = RG_TRANSITION_DONE;
            m->slide_dir = 1;
            rg_mascot_pose(RgMascotStateIdle, 0, 0, RG_MASCOT_PROGRESS_NONE, false, &m->frame);
        },
        false);
    return instance;
}

void radiogeddon_room_view_free(RadioGeddonRoomView* instance) {
    furi_assert(instance);
    view_free(instance->view);
    free(instance);
}

View* radiogeddon_room_view_get_view(RadioGeddonRoomView* instance) {
    return instance->view;
}

void radiogeddon_room_view_set_callback(
    RadioGeddonRoomView* instance,
    RadioGeddonRoomCallback callback,
    void* context) {
    instance->callback = callback;
    instance->context = context;
}

void radiogeddon_room_view_set_terminal(
    RadioGeddonRoomView* instance,
    const char lines[RADIOGEDDON_ROOM_LINES][RADIOGEDDON_ROOM_LINE_LEN],
    uint32_t recordings) {
    with_view_model(
        instance->view,
        RoomModel * m,
        {
            memcpy(m->lines, lines, sizeof(m->lines));
            for(int i = 0; i < RADIOGEDDON_ROOM_LINES; i++)
                m->lines[i][RADIOGEDDON_ROOM_LINE_LEN - 1] = '\0';
            m->recordings = recordings;
        },
        true);
}

void radiogeddon_room_view_set_scope(
    RadioGeddonRoomView* instance,
    const RadioGeddonRoomTrace* scope) {
    with_view_model(
        instance->view,
        RoomModel * m,
        {
            m->scope = *scope;
            m->scope.name[sizeof(m->scope.name) - 1] = '\0';
        },
        true);
}

uint8_t radiogeddon_room_view_get_station(RadioGeddonRoomView* instance) {
    uint8_t s = 0;
    with_view_model(instance->view, RoomModel * m, { s = m->station; }, false);
    return s;
}

uint8_t radiogeddon_room_view_get_gallery(RadioGeddonRoomView* instance) {
    uint8_t g = 0;
    with_view_model(instance->view, RoomModel * m, { g = m->gallery; }, false);
    return g;
}

void radiogeddon_room_view_update(
    RadioGeddonRoomView* instance,
    const RgMascotFrame* frame,
    uint32_t now) {
    bool changed = false;
    with_view_model(
        instance->view,
        RoomModel * m,
        {
            if(m->level == 0 && !m->slide.active) {
                rg_transition_start(&m->slide, RgTransitionSlide, ROOM_SLIDE_MS, now);
            }
            if(m->level != RG_TRANSITION_DONE) {
                m->level = rg_transition_level(&m->slide, now);
                changed = true;
            }
            if(frame && rg_mascot_frame_changed(&m->frame, frame)) {
                m->frame = *frame;
                changed = true;
            }
            // The radar, antenna and recorder turn at 8 steps a second.
            if(now - m->phase_ms >= 125u) {
                m->phase_ms = now;
                m->phase = (uint8_t)((m->phase + 1u) & 15u);
                if(m->station != RadioGeddonRoomTerminal && m->station != RadioGeddonRoomScope &&
                   m->station != RadioGeddonRoomGallery)
                    changed = true;
            }
        },
        changed);
}

#endif
