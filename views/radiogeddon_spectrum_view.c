#include "radiogeddon_spectrum_view.h"

#if RG_FEATURE_RANGE_SCAN

#include <gui/elements.h>
#include <furi.h>
#include "../helpers/rg_freq.h"

#define GRAPH_TOP    11
#define GRAPH_BOTTOM 51 // inclusive
#define GRAPH_W      128
#define DBM_BOTTOM   (-110)
#define DBM_TOP      (-30)

struct RadioGeddonSpectrumView {
    View* view;
    RadioGeddonSpectrumCallback callback;
    void* context;
};

typedef struct {
    RadioGeddonRangeSnapshot snap;
    uint32_t cursor;
    bool external;
    // Display columns, rebuilt from the snapshot on every update.
    int8_t col_last[GRAPH_W];
    int8_t col_peak[GRAPH_W];
    uint8_t col_flags[GRAPH_W];
} RadioGeddonSpectrumModel;

static int32_t radiogeddon_spectrum_y(int32_t dbm) {
    if(dbm < DBM_BOTTOM) dbm = DBM_BOTTOM;
    if(dbm > DBM_TOP) dbm = DBM_TOP;
    return GRAPH_BOTTOM - (dbm - DBM_BOTTOM) * (GRAPH_BOTTOM - GRAPH_TOP) / (DBM_TOP - DBM_BOTTOM);
}

static void radiogeddon_spectrum_view_draw(Canvas* canvas, void* model) {
    RadioGeddonSpectrumModel* m = model;
    const RadioGeddonRangeSnapshot* s = &m->snap;
    canvas_clear(canvas);
    canvas_set_font(canvas, FontSecondary);

    char text[32];
    // Header: state, then the sweep time.
    const char* state = m->external ? "RANGE EXT" : "RANGE";
    if(s->paused) {
        state = "PAUSED";
    } else if(s->hold_index >= 0) {
        state = "HOLD";
    } else if(s->calibrating) {
        state = "CALIBRATING";
    }
    canvas_draw_str(canvas, 0, 8, state);
    if(s->floor == RG_SPECTRUM_NONE) {
        snprintf(text, sizeof(text), "NF-- T+%u", (unsigned)s->threshold_db);
    } else {
        snprintf(text, sizeof(text), "NF%d T+%u", (int)s->floor, (unsigned)s->threshold_db);
    }
    canvas_draw_str_aligned(canvas, 127, 8, AlignRight, AlignBottom, text);

    if(s->count == 0) {
        canvas_draw_str(canvas, 0, 32, "No points to scan.");
        return;
    }

    // Threshold over the median floor: a dotted line.
    if(s->floor != RG_SPECTRUM_NONE) {
        int32_t ty = radiogeddon_spectrum_y((int32_t)s->floor + s->threshold_db);
        for(int32_t x = 0; x < GRAPH_W; x += 3)
            canvas_draw_dot(canvas, x, ty);
    }
    for(int32_t x = 0; x < GRAPH_W; x++) {
        if(m->col_last[x] != RG_SPECTRUM_NONE) {
            int32_t y = radiogeddon_spectrum_y(m->col_last[x]);
            canvas_draw_line(canvas, x, GRAPH_BOTTOM, x, y);
        }
        if(m->col_peak[x] != RG_SPECTRUM_NONE) {
            canvas_draw_dot(canvas, x, radiogeddon_spectrum_y(m->col_peak[x]));
        }
        if(m->col_flags[x] & RgSpectrumActive) canvas_draw_line(canvas, x, 9, x, 10);
    }
    // Sweep position: a tick under the graph. Cursor: a dashed line.
    size_t pos_col = rg_spectrum_column_of(s->position, s->count, GRAPH_W);
    canvas_draw_dot(canvas, (int32_t)pos_col, GRAPH_BOTTOM + 1);
    size_t cur_col = rg_spectrum_column_of(m->cursor, s->count, GRAPH_W);
    for(int32_t y = GRAPH_TOP; y <= GRAPH_BOTTOM; y += 2) {
        canvas_invert_color(canvas);
        canvas_draw_dot(canvas, (int32_t)cur_col, y);
        canvas_invert_color(canvas);
        canvas_draw_dot(canvas, (int32_t)cur_col, y + 1);
    }

    // Footer: the cursor point.
    char freq[RG_FREQ_TEXT_SIZE];
    rg_freq_text(rg_range_frequency(&s->range, m->cursor), freq, sizeof(freq));
    const RgSpectrumPoint* p = &s->point[m->cursor < s->count ? m->cursor : 0];
    if(p->flags & RgSpectrumMeasured) {
        snprintf(
            text, sizeof(text), "%s %d/%d %u", freq, (int)p->last, (int)p->peak, (unsigned)p->hits);
    } else {
        snprintf(text, sizeof(text), "%s --", freq);
    }
    canvas_draw_str(canvas, 0, 63, text);
    if(s->sweep_ms) {
        if(s->sweep_ms >= 10000) {
            snprintf(text, sizeof(text), "%lus", (unsigned long)(s->sweep_ms / 1000));
        } else {
            snprintf(
                text,
                sizeof(text),
                "%lu.%lus",
                (unsigned long)(s->sweep_ms / 1000),
                (unsigned long)((s->sweep_ms % 1000) / 100));
        }
        canvas_draw_str_aligned(canvas, 127, 63, AlignRight, AlignBottom, text);
    }
}

static void
    radiogeddon_spectrum_view_send(RadioGeddonSpectrumView* instance, RadioGeddonSpectrumEvent e) {
    if(instance->callback) instance->callback(e, instance->context);
}

static bool radiogeddon_spectrum_view_input(InputEvent* event, void* context) {
    RadioGeddonSpectrumView* instance = context;
    bool consumed = false;

    if((event->type == InputTypeShort || event->type == InputTypeRepeat) &&
       (event->key == InputKeyLeft || event->key == InputKeyRight)) {
        bool right = event->key == InputKeyRight;
        bool fast = event->type == InputTypeRepeat;
        with_view_model(
            instance->view,
            RadioGeddonSpectrumModel * m,
            {
                uint32_t n = m->snap.count;
                if(n > 0) {
                    // Held: about one screen column per step.
                    uint32_t step = (fast && n > GRAPH_W) ? n / GRAPH_W : 1;
                    if(right) {
                        m->cursor = (m->cursor + step < n) ? m->cursor + step : 0;
                    } else {
                        m->cursor = (m->cursor >= step) ? m->cursor - step : n - 1;
                    }
                }
            },
            true);
        return true;
    }
    if(event->type == InputTypeShort) {
        switch(event->key) {
        case InputKeyOk:
            radiogeddon_spectrum_view_send(instance, RadioGeddonSpectrumEventReceive);
            consumed = true;
            break;
        case InputKeyUp:
            radiogeddon_spectrum_view_send(instance, RadioGeddonSpectrumEventTogglePause);
            consumed = true;
            break;
        case InputKeyDown:
            with_view_model(
                instance->view,
                RadioGeddonSpectrumModel * m,
                {
                    size_t best = rg_spectrum_strongest(m->snap.point, m->snap.count);
                    if(best < m->snap.count) m->cursor = (uint32_t)best;
                },
                true);
            consumed = true;
            break;
        default:
            break;
        }
    } else if(event->type == InputTypeLong) {
        switch(event->key) {
        case InputKeyOk:
            radiogeddon_spectrum_view_send(instance, RadioGeddonSpectrumEventRecord);
            consumed = true;
            break;
        case InputKeyUp:
            radiogeddon_spectrum_view_send(instance, RadioGeddonSpectrumEventRecalibrate);
            consumed = true;
            break;
        case InputKeyDown:
            radiogeddon_spectrum_view_send(instance, RadioGeddonSpectrumEventResetPeaks);
            consumed = true;
            break;
        case InputKeyRight:
            radiogeddon_spectrum_view_send(instance, RadioGeddonSpectrumEventSave);
            consumed = true;
            break;
        default:
            break;
        }
    }
    return consumed;
}

RadioGeddonSpectrumView* radiogeddon_spectrum_view_alloc(void) {
    RadioGeddonSpectrumView* instance = malloc(sizeof(RadioGeddonSpectrumView));
    instance->view = view_alloc();
    instance->callback = NULL;
    instance->context = NULL;
    view_set_context(instance->view, instance);
    view_allocate_model(instance->view, ViewModelTypeLocking, sizeof(RadioGeddonSpectrumModel));
    view_set_draw_callback(instance->view, radiogeddon_spectrum_view_draw);
    view_set_input_callback(instance->view, radiogeddon_spectrum_view_input);
    with_view_model(
        instance->view,
        RadioGeddonSpectrumModel * m,
        {
            memset(m, 0, sizeof(*m));
            m->snap.hold_index = -1;
            m->snap.floor = RG_SPECTRUM_NONE;
        },
        false);
    return instance;
}

void radiogeddon_spectrum_view_free(RadioGeddonSpectrumView* instance) {
    furi_assert(instance);
    view_free(instance->view);
    free(instance);
}

View* radiogeddon_spectrum_view_get_view(RadioGeddonSpectrumView* instance) {
    return instance->view;
}

void radiogeddon_spectrum_view_set_callback(
    RadioGeddonSpectrumView* instance,
    RadioGeddonSpectrumCallback callback,
    void* context) {
    instance->callback = callback;
    instance->context = context;
}

void radiogeddon_spectrum_view_update(
    RadioGeddonSpectrumView* instance,
    RadioGeddonRangeScan* scan) {
    with_view_model(
        instance->view,
        RadioGeddonSpectrumModel * m,
        {
            radiogeddon_rangescan_snapshot(scan, &m->snap);
            if(m->cursor >= m->snap.count) m->cursor = m->snap.count ? m->snap.count - 1 : 0;
            rg_spectrum_columns(
                m->snap.point, m->snap.count, GRAPH_W, m->col_last, m->col_peak, m->col_flags);
        },
        true);
}

void radiogeddon_spectrum_view_set_external(RadioGeddonSpectrumView* instance, bool external) {
    with_view_model(
        instance->view, RadioGeddonSpectrumModel * m, { m->external = external; }, true);
}

void radiogeddon_spectrum_view_set_cursor(RadioGeddonSpectrumView* instance, uint32_t index) {
    with_view_model(
        instance->view,
        RadioGeddonSpectrumModel * m,
        {
            if(index < m->snap.count) m->cursor = index;
        },
        true);
}

uint32_t radiogeddon_spectrum_view_get_cursor(RadioGeddonSpectrumView* instance) {
    uint32_t cursor = 0;
    with_view_model(instance->view, RadioGeddonSpectrumModel * m, { cursor = m->cursor; }, false);
    return cursor;
}

#endif
