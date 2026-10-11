#include "radiogeddon_waterfall_view.h"

#if RG_FEATURE_WATERFALL

#include <gui/elements.h>
#include <furi.h>
#include "../helpers/rg_freq.h"

#define WF_TOP    11 // first picture row
#define WF_MARK_Y 9 // marker row (peak tick, segment dots)

const uint8_t radiogeddon_waterfall_spans[RADIOGEDDON_WF_SPANS] = {6, 10, 20, 30, 40};

struct RadioGeddonWaterfallView {
    View* view;
    RadioGeddonWaterfallCallback callback;
    void* context;
};

typedef struct {
    RadioGeddonWaterfallFrame frame;
    uint16_t scroll; // requested; anchored to its sweep while new ones arrive
    uint16_t cursor;
    uint8_t span_index;
    bool noise_comp;
    bool external;
    bool have_frame;
    uint32_t last_stored; // frame.stored at the previous update
    bool menu_open; // hold OK: the action menu over the picture
    uint8_t menu_index;
} RadioGeddonWaterfallModel;

typedef enum {
    WfMenuReceive,
    WfMenuPeak,
    WfMenuLive,
    WfMenuSpan,
    WfMenuComp,
    WfMenuSave,
    WfMenuCount,
} WfMenuItem;

static void
    radiogeddon_waterfall_seconds(char* out, size_t size, uint32_t ms, const char* prefix) {
    if(ms >= 10000) {
        snprintf(out, size, "%s%lus", prefix, (unsigned long)(ms / 1000));
    } else {
        snprintf(
            out,
            size,
            "%s%lu.%lus",
            prefix,
            (unsigned long)(ms / 1000),
            (unsigned long)((ms % 1000) / 100));
    }
}

static void radiogeddon_waterfall_dbm(char* out, size_t size, const char* prefix, int16_t dbm) {
    if(dbm == RG_WF_NO_DATA) {
        snprintf(out, size, "%s--", prefix);
    } else {
        snprintf(out, size, "%s%d", prefix, (int)dbm);
    }
}

static void radiogeddon_waterfall_view_draw(Canvas* canvas, void* model) {
    RadioGeddonWaterfallModel* m = model;
    const RadioGeddonWaterfallFrame* f = &m->frame;
    canvas_clear(canvas);
    canvas_set_font(canvas, FontSecondary);
    char text[40];

    // Header: what this is (a sweep history, not a spectrum capture) and state.
    canvas_draw_str(canvas, 0, 8, m->external ? "RSSI sweep EXT" : "RSSI sweep");
    const char* state = "LIVE";
    char scrolled[8];
    if(m->have_frame && f->scroll > 0) {
        snprintf(scrolled, sizeof(scrolled), "-%u", (unsigned)f->scroll);
        state = scrolled;
    } else if(f->paused) {
        state = "PAUSE";
    } else if(f->calibrating) {
        state = "CAL";
    }
    snprintf(
        text,
        sizeof(text),
        "%s S%u%c",
        state,
        (unsigned)radiogeddon_waterfall_spans[m->span_index % RADIOGEDDON_WF_SPANS],
        m->noise_comp ? 'N' : 'A');
    canvas_draw_str_aligned(canvas, 127, 8, AlignRight, AlignBottom, text);

    if(!m->have_frame || f->columns == 0) {
        canvas_draw_str_aligned(canvas, 64, 32, AlignCenter, AlignCenter, "No points to sweep.");
        return;
    }

    if(f->filled == 0) {
        // Nothing measured yet: say how long the first row takes.
        canvas_draw_str_aligned(canvas, 64, 26, AlignCenter, AlignCenter, "Waiting for the");
        canvas_draw_str_aligned(canvas, 64, 36, AlignCenter, AlignCenter, "first full sweep");
        radiogeddon_waterfall_seconds(text, sizeof(text), f->estimate_ms, "~");
        size_t n = strlen(text);
        snprintf(text + n, sizeof(text) - n, " per sweep");
        canvas_draw_str_aligned(canvas, 64, 46, AlignCenter, AlignCenter, text);
    } else {
        canvas_draw_xbm(canvas, 0, WF_TOP, RADIOGEDDON_WF_VIEW_W, RADIOGEDDON_WF_VIEW_H, f->xbm);
    }

    // Marker row: band segment starts (a gap was skipped) and the peak.
    for(uint16_t x = 0; x < RADIOGEDDON_WF_VIEW_W; x++) {
        if(f->seg_start[x / 8u] & (1u << (x % 8u))) {
            canvas_draw_dot(canvas, x, WF_MARK_Y + 1);
            canvas_draw_dot(canvas, x, WF_MARK_Y - 1);
        }
    }
    if(f->have_peak) {
        uint16_t x = 0, w = 0;
        // Same column-to-pixel rule as the picture.
        x = (uint16_t)(((uint32_t)f->peak_column * RADIOGEDDON_WF_VIEW_W + f->columns - 1u) /
                       f->columns);
        uint16_t x2 = (uint16_t)(((uint32_t)(f->peak_column + 1u) * RADIOGEDDON_WF_VIEW_W +
                                  f->columns - 1u) /
                                 f->columns);
        w = x2 > x ? (uint16_t)(x2 - x) : 1u;
        canvas_draw_line(canvas, x, WF_MARK_Y, x + w - 1, WF_MARK_Y);
        canvas_draw_line(canvas, x + (w - 1) / 2, WF_MARK_Y - 1, x + (w - 1) / 2, WF_MARK_Y + 1);
    }

    // Cursor: a dashed line of alternating colours, visible on any pattern,
    // and a tick under the picture.
    int32_t cx = (int32_t)f->cursor_x + (int32_t)(f->cursor_w ? (f->cursor_w - 1u) / 2u : 0u);
    for(int32_t y = WF_TOP; y < WF_TOP + (int32_t)RADIOGEDDON_WF_VIEW_H; y += 2) {
        canvas_set_color(canvas, ColorWhite);
        canvas_draw_dot(canvas, cx, y);
        canvas_set_color(canvas, ColorBlack);
        canvas_draw_dot(canvas, cx, y + 1);
    }
    canvas_draw_line(
        canvas,
        cx > 0 ? cx - 1 : 0,
        WF_TOP + RADIOGEDDON_WF_VIEW_H,
        cx < (int32_t)RADIOGEDDON_WF_VIEW_W - 1 ? cx + 1 : cx,
        WF_TOP + RADIOGEDDON_WF_VIEW_H);

    // Footer: cursor frequency, reading in the top visible row, column peak.
    char freq[RG_FREQ_TEXT_SIZE];
    rg_freq_text(f->cursor_hz, freq, sizeof(freq));
    char now[8], peak[10];
    radiogeddon_waterfall_dbm(now, sizeof(now), "", f->cursor_dbm);
    radiogeddon_waterfall_dbm(peak, sizeof(peak), "p", f->cursor_peak);
    snprintf(text, sizeof(text), "%s %s %s", freq, now, peak);
    canvas_draw_str(canvas, 0, 61, text);
    if(f->scroll > 0 && f->filled) {
        radiogeddon_waterfall_seconds(text, sizeof(text), f->top_age_ms, "-");
    } else if(f->sweep_ms) {
        radiogeddon_waterfall_seconds(text, sizeof(text), f->sweep_ms, "");
    } else {
        radiogeddon_waterfall_seconds(text, sizeof(text), f->estimate_ms, "~");
    }
    canvas_draw_str_aligned(canvas, 127, 61, AlignRight, AlignBottom, text);

    if(m->menu_open) {
        // Action menu: a framed list over the picture.
        canvas_set_color(canvas, ColorWhite);
        canvas_draw_box(canvas, 10, 8, 108, 56);
        canvas_set_color(canvas, ColorBlack);
        canvas_draw_frame(canvas, 10, 8, 108, 56);
        for(uint8_t i = 0; i < WfMenuCount; i++) {
            int32_t y = 17 + i * 9;
            switch(i) {
            case WfMenuReceive:
                snprintf(text, sizeof(text), "Receive here");
                break;
            case WfMenuPeak:
                snprintf(text, sizeof(text), "Cursor to peak");
                break;
            case WfMenuLive:
                snprintf(text, sizeof(text), "Newest sweeps");
                break;
            case WfMenuSpan:
                snprintf(
                    text,
                    sizeof(text),
                    "< Sensitivity %u dB >",
                    (unsigned)radiogeddon_waterfall_spans[m->span_index % RADIOGEDDON_WF_SPANS]);
                break;
            case WfMenuComp:
                snprintf(text, sizeof(text), "< Floor comp %s >", m->noise_comp ? "On" : "Off");
                break;
            default:
                snprintf(text, sizeof(text), "Save history CSV");
                break;
            }
            if(i == m->menu_index) {
                canvas_draw_box(canvas, 11, y - 8, 106, 9);
                canvas_set_color(canvas, ColorWhite);
            }
            canvas_draw_str(canvas, 14, y, text);
            canvas_set_color(canvas, ColorBlack);
        }
    }
}

static void radiogeddon_waterfall_view_send(
    RadioGeddonWaterfallView* instance,
    RadioGeddonWaterfallEvent e) {
    if(instance->callback) instance->callback(e, instance->context);
}

/* Menu action on the model; returns the event to send, or -1. */
static int radiogeddon_waterfall_menu_act(RadioGeddonWaterfallModel* m, InputKey key) {
    switch(m->menu_index) {
    case WfMenuReceive:
        if(key != InputKeyOk) return -1;
        m->menu_open = false;
        return RadioGeddonWaterfallEventReceive;
    case WfMenuPeak:
        if(key != InputKeyOk) return -1;
        if(m->frame.have_peak) {
            m->cursor = m->frame.peak_column;
            // Scroll so the peak's sweep is on screen.
            uint16_t age = m->frame.peak_age;
            if(age < m->scroll || age >= m->scroll + RADIOGEDDON_WF_VIEW_H) {
                uint16_t want = age > RADIOGEDDON_WF_VIEW_H / 2 ?
                                    (uint16_t)(age - RADIOGEDDON_WF_VIEW_H / 2) :
                                    0;
                m->scroll = want > m->frame.max_scroll ? m->frame.max_scroll : want;
            }
        }
        m->menu_open = false;
        return -1;
    case WfMenuLive:
        if(key != InputKeyOk) return -1;
        m->scroll = 0;
        m->menu_open = false;
        return -1;
    case WfMenuSpan:
        m->span_index =
            (uint8_t)((m->span_index + (key == InputKeyLeft ? RADIOGEDDON_WF_SPANS - 1u : 1u)) %
                      RADIOGEDDON_WF_SPANS);
        return RadioGeddonWaterfallEventSettings;
    case WfMenuComp:
        m->noise_comp = !m->noise_comp;
        return RadioGeddonWaterfallEventSettings;
    default:
        if(key != InputKeyOk) return -1;
        m->menu_open = false;
        return RadioGeddonWaterfallEventSave;
    }
}

/* Keys on the model; returns the event to send, or -1. A held key gives a
 * long press and then repeats, so no key has both a "fast" and a separate
 * long-press meaning. */
static int radiogeddon_waterfall_input_model(RadioGeddonWaterfallModel* m, InputEvent* event) {
    bool any = event->type == InputTypeShort || event->type == InputTypeLong ||
               event->type == InputTypeRepeat;
    if(m->menu_open) {
        if(event->key == InputKeyBack) {
            if(event->type == InputTypeShort) m->menu_open = false;
            return -1;
        }
        if(event->type != InputTypeShort && event->type != InputTypeRepeat) return -1;
        if(event->key == InputKeyUp) {
            m->menu_index = (uint8_t)(m->menu_index ? m->menu_index - 1u : WfMenuCount - 1u);
        } else if(event->key == InputKeyDown) {
            m->menu_index = (uint8_t)((m->menu_index + 1u) % WfMenuCount);
        } else if(event->type == InputTypeShort) {
            return radiogeddon_waterfall_menu_act(m, event->key);
        }
        return -1;
    }
    if(event->key == InputKeyOk) {
        if(event->type == InputTypeShort) return RadioGeddonWaterfallEventTogglePause;
        if(event->type == InputTypeLong) {
            m->menu_open = true;
            m->menu_index = WfMenuReceive;
        }
        return -1;
    }
    if(!any) return -1;
    if(event->key == InputKeyLeft || event->key == InputKeyRight) {
        // Held: the long press and its repeats move faster.
        uint16_t n = m->frame.columns;
        if(n > 0) {
            uint16_t step = (event->type != InputTypeShort && n >= 32) ? (uint16_t)(n / 32u) : 1u;
            if(event->key == InputKeyRight) {
                m->cursor = (uint16_t)((m->cursor + step < n) ? m->cursor + step : 0);
            } else {
                m->cursor = (uint16_t)((m->cursor >= step) ? m->cursor - step : n - 1);
            }
        }
    } else if(event->key == InputKeyUp || event->key == InputKeyDown) {
        // One sweep is one pixel row: scroll a few at a time.
        uint16_t step = 4u;
        uint16_t max = m->frame.max_scroll;
        if(event->key == InputKeyDown) {
            m->scroll = (uint16_t)(m->scroll + step > max ? max : m->scroll + step);
        } else {
            m->scroll = (uint16_t)(m->scroll > step ? m->scroll - step : 0);
        }
    }
    return -1;
}

static bool radiogeddon_waterfall_view_input(InputEvent* event, void* context) {
    RadioGeddonWaterfallView* instance = context;
    int send = -1;
    bool consumed = true;
    with_view_model(
        instance->view,
        RadioGeddonWaterfallModel * m,
        {
            // Back leaves the screen unless it closes the menu.
            if(event->key == InputKeyBack && !m->menu_open) consumed = false;
            if(consumed) send = radiogeddon_waterfall_input_model(m, event);
        },
        true);
    if(send >= 0) radiogeddon_waterfall_view_send(instance, (RadioGeddonWaterfallEvent)send);
    return consumed;
}

RadioGeddonWaterfallView* radiogeddon_waterfall_view_alloc(void) {
    RadioGeddonWaterfallView* instance = malloc(sizeof(RadioGeddonWaterfallView));
    instance->view = view_alloc();
    instance->callback = NULL;
    instance->context = NULL;
    view_set_context(instance->view, instance);
    view_allocate_model(instance->view, ViewModelTypeLocking, sizeof(RadioGeddonWaterfallModel));
    view_set_draw_callback(instance->view, radiogeddon_waterfall_view_draw);
    view_set_input_callback(instance->view, radiogeddon_waterfall_view_input);
    with_view_model(
        instance->view,
        RadioGeddonWaterfallModel * m,
        {
            memset(m, 0, sizeof(*m));
            m->span_index = 2;
            m->noise_comp = true;
            m->frame.cursor_dbm = RG_WF_NO_DATA;
            m->frame.cursor_peak = RG_WF_NO_DATA;
        },
        false);
    return instance;
}

void radiogeddon_waterfall_view_free(RadioGeddonWaterfallView* instance) {
    furi_assert(instance);
    view_free(instance->view);
    free(instance);
}

View* radiogeddon_waterfall_view_get_view(RadioGeddonWaterfallView* instance) {
    return instance->view;
}

void radiogeddon_waterfall_view_set_callback(
    RadioGeddonWaterfallView* instance,
    RadioGeddonWaterfallCallback callback,
    void* context) {
    instance->callback = callback;
    instance->context = context;
}

/* One update of the model (its lock held). */
static void
    radiogeddon_waterfall_view_refresh(RadioGeddonWaterfallModel* m, RadioGeddonRangeScan* scan) {
    RadioGeddonWaterfallRequest req;
    req.scroll = m->scroll;
    req.cursor = m->cursor;
    req.span_db = radiogeddon_waterfall_spans[m->span_index % RADIOGEDDON_WF_SPANS];
    req.noise_comp = m->noise_comp;
    radiogeddon_rangescan_waterfall_frame(scan, &req, &m->frame);
    // Scrolled back: keep showing the same sweeps as new ones arrive.
    if(m->have_frame && m->scroll > 0 && m->frame.stored > m->last_stored) {
        uint32_t scroll = (uint32_t)m->scroll + (m->frame.stored - m->last_stored);
        req.scroll = (uint16_t)(scroll > UINT16_MAX ? UINT16_MAX : scroll);
        radiogeddon_rangescan_waterfall_frame(scan, &req, &m->frame);
    }
    m->last_stored = m->frame.stored;
    m->scroll = m->frame.scroll;
    m->cursor = m->frame.cursor;
    m->have_frame = true;
}

void radiogeddon_waterfall_view_update(
    RadioGeddonWaterfallView* instance,
    RadioGeddonRangeScan* scan) {
    with_view_model(
        instance->view,
        RadioGeddonWaterfallModel * m,
        { radiogeddon_waterfall_view_refresh(m, scan); },
        true);
}

void radiogeddon_waterfall_view_set_style(
    RadioGeddonWaterfallView* instance,
    uint8_t span_index,
    bool noise_comp) {
    with_view_model(
        instance->view,
        RadioGeddonWaterfallModel * m,
        {
            m->span_index = (uint8_t)(span_index % RADIOGEDDON_WF_SPANS);
            m->noise_comp = noise_comp;
        },
        true);
}

void radiogeddon_waterfall_view_get_style(
    RadioGeddonWaterfallView* instance,
    uint8_t* span_index,
    bool* noise_comp) {
    with_view_model(
        instance->view,
        RadioGeddonWaterfallModel * m,
        {
            *span_index = m->span_index;
            *noise_comp = m->noise_comp;
        },
        false);
}

void radiogeddon_waterfall_view_set_external(RadioGeddonWaterfallView* instance, bool external) {
    with_view_model(
        instance->view, RadioGeddonWaterfallModel * m, { m->external = external; }, true);
}

uint32_t radiogeddon_waterfall_view_get_cursor_hz(RadioGeddonWaterfallView* instance) {
    uint32_t hz = 0;
    with_view_model(
        instance->view,
        RadioGeddonWaterfallModel * m,
        { hz = m->have_frame ? m->frame.cursor_hz : 0; },
        false);
    return hz;
}

uint16_t radiogeddon_waterfall_view_get_cursor(RadioGeddonWaterfallView* instance) {
    uint16_t c = 0;
    with_view_model(instance->view, RadioGeddonWaterfallModel * m, { c = m->cursor; }, false);
    return c;
}

void radiogeddon_waterfall_view_set_cursor(RadioGeddonWaterfallView* instance, uint16_t column) {
    with_view_model(instance->view, RadioGeddonWaterfallModel * m, { m->cursor = column; }, true);
}

#endif
