#include "radiogeddon_scanner_view.h"
#include "../radiogeddon_edition.h" /* RG_UI_TEXT */
#include <gui/elements.h>
#include <furi.h>
#if RG_FEATURE_DOLPHIN_LAB
#include "radiogeddon_mascot_draw.h"
#endif

#define RSSI_FLOOR   (-110.0f)
#define RSSI_CEIL    (-30.0f)
#define VISIBLE_ROWS 4
#define ROW_H        13
#define ROWS_Y       12
#define BAR_X        38
#define BAR_W        50

struct RadioGeddonScannerView {
    View* view;
    RadioGeddonScannerCallback callback;
    void* context;
};

typedef struct {
    RadioGeddonScannerSnapshot snap;
    size_t selected;
    size_t top; // first visible row
    bool external; // the external CC1101 module is in use
#if RG_FEATURE_DOLPHIN_LAB
    RgMascotFrame badge;
    bool badge_on;
    bool badge_cyber;
#endif
} RadioGeddonScannerModel;

static RG_UI_TEXT int32_t radiogeddon_scanner_bar_pos(float dbm) {
    float norm = (dbm - RSSI_FLOOR) / (RSSI_CEIL - RSSI_FLOOR);
    if(norm < 0) norm = 0;
    if(norm > 1) norm = 1;
    return (int32_t)(norm * (BAR_W - 2));
}

static RG_UI_TEXT void
    radiogeddon_scanner_view_draw_header(Canvas* canvas, RadioGeddonScannerModel* m) {
    const RadioGeddonScannerSnapshot* s = &m->snap;
    canvas_set_font(canvas, FontSecondary);

    const char* status = m->external ? "RSSI EXT" : "RSSI scan";
#if RG_FEATURE_ADAPTIVE_SCAN
    if(s->mode != RgAdaptStandard) status = rg_adapt_mode_name((RgAdaptMode)s->mode);
#endif
    if(s->paused) {
        status = "PAUSED";
    } else if(s->hold_index >= 0) {
        status = "HOLD";
    }
    canvas_draw_str(canvas, 2, 9, status);

    char right[24];
#if RG_FEATURE_ADAPTIVE_SCAN
    if(s->mode != RgAdaptStandard) {
        // The longest any frequency waited unheard: the honest coverage figure.
        snprintf(
            right,
            sizeof(right),
            "gap %lu.%lus",
            (unsigned long)(s->worst_gap_ms / 1000u),
            (unsigned long)(s->worst_gap_ms % 1000u / 100u));
    } else
#endif
        if(s->global_floor <= RG_SCAN_RSSI_NONE) {
        snprintf(right, sizeof(right), "NF -- T+%u", (unsigned)s->threshold_db);
    } else {
        snprintf(
            right,
            sizeof(right),
            "NF%d T+%u",
            (int)(s->global_floor - 0.5f),
            (unsigned)s->threshold_db);
    }
    canvas_draw_str_aligned(canvas, 126, 9, AlignRight, AlignBottom, right);
    canvas_draw_line(canvas, 0, 11, 127, 11);
    // The sweep's position along the list: a thick mark on the line under
    // the header, at the channel being measured right now.
    if(s->measuring >= 0 && (size_t)s->measuring < s->count && s->count > 0) {
        int32_t w = (int32_t)(128 / s->count);
        if(w < 3) w = 3;
        int32_t x = (int32_t)((size_t)s->measuring * 128 / s->count);
        if(x + w > 128) x = 128 - w;
        canvas_draw_box(canvas, x, 10, (size_t)w, 2);
    }
#if RG_FEATURE_DOLPHIN_LAB
    if(m->badge_on) rg_draw_badge(canvas, 56, 1, m->badge_cyber, &m->badge);
#endif
}

static RG_UI_TEXT void radiogeddon_scanner_view_draw_row(
    Canvas* canvas,
    const RadioGeddonScannerSnapshot* s,
    size_t idx,
    int32_t y,
    bool sel) {
    const RgScanChannel* ch = &s->channels[idx];
    if(sel) {
        canvas_set_color(canvas, ColorBlack);
        canvas_draw_box(canvas, 0, y, 128, ROW_H);
        canvas_set_color(canvas, ColorWhite);
    }

    if(ch->active) canvas_draw_disc(canvas, 2, y + 6, 2);

    char text[16];
    uint32_t hz = s->frequencies[idx];
    snprintf(
        text,
        sizeof(text),
        "%lu.%02lu",
        (unsigned long)(hz / 1000000),
        (unsigned long)((hz % 1000000) / 10000));
    canvas_draw_str(canvas, 6, y + 10, text);
#if RG_FEATURE_ADAPTIVE_SCAN
    // Adaptive: the share of visits this frequency gets, under its name.
    if(s->mode != RgAdaptStandard && s->share[idx]) {
        int32_t w = (int32_t)s->share[idx] * 30 / 100;
        canvas_draw_line(canvas, 6, y + 12, 6 + (w > 0 ? w : 1), y + 12);
    }
#endif

    canvas_draw_frame(canvas, BAR_X, y + 2, BAR_W, 9);
    if(ch->samples > 0) {
        int32_t fill = radiogeddon_scanner_bar_pos(ch->last);
        if(fill > 0) canvas_draw_box(canvas, BAR_X + 1, y + 3, fill, 7);
        // Peak hold: a full-height tick.
        int32_t peak = BAR_X + 1 + radiogeddon_scanner_bar_pos(ch->peak);
        canvas_draw_line(canvas, peak, y + 1, peak, y + 11);
        if(ch->samples >= RG_SCAN_WARMUP_SAMPLES) {
            // Detection threshold: small marks above and below the bar.
            int32_t thr =
                BAR_X + 1 + radiogeddon_scanner_bar_pos(ch->floor + (float)s->threshold_db);
            canvas_draw_dot(canvas, thr, y + 1);
            canvas_draw_dot(canvas, thr, y + 11);
        }
        snprintf(text, sizeof(text), "%d", (int)(ch->last - 0.5f));
    } else {
        snprintf(text, sizeof(text), "--");
    }
    canvas_draw_str_aligned(canvas, 110, y + 10, AlignRight, AlignBottom, text);

    if(ch->hits > 99) {
        snprintf(text, sizeof(text), "99+");
    } else {
        snprintf(text, sizeof(text), "%u", (unsigned)ch->hits);
    }
    canvas_draw_str_aligned(canvas, 127, y + 10, AlignRight, AlignBottom, text);

    if(sel) canvas_set_color(canvas, ColorBlack);
}

static RG_UI_TEXT void radiogeddon_scanner_view_draw(Canvas* canvas, void* model) {
    RadioGeddonScannerModel* m = model;
    canvas_clear(canvas);
    radiogeddon_scanner_view_draw_header(canvas, m);

    if(m->snap.count == 0) {
        canvas_draw_str(canvas, 2, 30, "No frequencies to scan.");
        canvas_draw_str(canvas, 2, 42, "Check Settings > Scan list.");
        return;
    }

    if(m->selected >= m->snap.count) m->selected = m->snap.count - 1;
    if(m->selected < m->top) m->top = m->selected;
    if(m->selected >= m->top + VISIBLE_ROWS) m->top = m->selected - (VISIBLE_ROWS - 1);

    for(size_t row = 0; row < VISIBLE_ROWS; row++) {
        size_t idx = m->top + row;
        if(idx >= m->snap.count) break;
        radiogeddon_scanner_view_draw_row(
            canvas, &m->snap, idx, ROWS_Y + (int32_t)row * ROW_H, idx == m->selected);
    }
}

static RG_UI_TEXT void
    radiogeddon_scanner_view_send(RadioGeddonScannerView* instance, RadioGeddonScannerEvent e) {
    if(instance->callback) instance->callback(e, instance->context);
}

static RG_UI_TEXT bool radiogeddon_scanner_view_input(InputEvent* event, void* context) {
    RadioGeddonScannerView* instance = context;
    bool consumed = false;

    if(event->type == InputTypeShort || event->type == InputTypeRepeat) {
        if(event->key == InputKeyUp || event->key == InputKeyDown) {
            bool up = (event->key == InputKeyUp);
            with_view_model(
                instance->view,
                RadioGeddonScannerModel * m,
                {
                    if(m->snap.count > 0) {
                        if(up) {
                            m->selected = (m->selected > 0) ? m->selected - 1 : m->snap.count - 1;
                        } else {
                            m->selected = (m->selected + 1 < m->snap.count) ? m->selected + 1 : 0;
                        }
                    }
                },
                true);
            consumed = true;
        }
    }
    if(event->type == InputTypeShort) {
        if(event->key == InputKeyOk) {
            radiogeddon_scanner_view_send(instance, RadioGeddonScannerEventSelect);
            consumed = true;
        } else if(event->key == InputKeyLeft) {
            radiogeddon_scanner_view_send(instance, RadioGeddonScannerEventTogglePause);
            consumed = true;
        } else if(event->key == InputKeyRight) {
            radiogeddon_scanner_view_send(instance, RadioGeddonScannerEventSave);
            consumed = true;
        }
    } else if(event->type == InputTypeLong && event->key == InputKeyLeft) {
        radiogeddon_scanner_view_send(instance, RadioGeddonScannerEventResetPeaks);
        consumed = true;
    } else if(event->type == InputTypeLong && event->key == InputKeyOk) {
        radiogeddon_scanner_view_send(instance, RadioGeddonScannerEventRecord);
        consumed = true;
    }
    return consumed;
}

RG_UI_TEXT RadioGeddonScannerView* radiogeddon_scanner_view_alloc(void) {
    RadioGeddonScannerView* instance = malloc(sizeof(RadioGeddonScannerView));
    instance->view = view_alloc();
    instance->callback = NULL;
    instance->context = NULL;
    view_set_context(instance->view, instance);
    view_allocate_model(instance->view, ViewModelTypeLocking, sizeof(RadioGeddonScannerModel));
    view_set_draw_callback(instance->view, radiogeddon_scanner_view_draw);
    view_set_input_callback(instance->view, radiogeddon_scanner_view_input);

    with_view_model(
        instance->view,
        RadioGeddonScannerModel * m,
        {
            memset(m, 0, sizeof(*m));
            m->snap.hold_index = -1;
            m->snap.measuring = -1;
            m->snap.global_floor = RG_SCAN_RSSI_NONE;
        },
        false);

    return instance;
}

RG_UI_TEXT void radiogeddon_scanner_view_free(RadioGeddonScannerView* instance) {
    furi_assert(instance);
    view_free(instance->view);
    free(instance);
}

RG_UI_TEXT View* radiogeddon_scanner_view_get_view(RadioGeddonScannerView* instance) {
    return instance->view;
}

RG_UI_TEXT void radiogeddon_scanner_view_set_callback(
    RadioGeddonScannerView* instance,
    RadioGeddonScannerCallback callback,
    void* context) {
    instance->callback = callback;
    instance->context = context;
}

RG_UI_TEXT void
    radiogeddon_scanner_view_update(RadioGeddonScannerView* instance, RadioGeddonScanner* scanner) {
    with_view_model(
        instance->view,
        RadioGeddonScannerModel * m,
        { radiogeddon_scanner_snapshot(scanner, &m->snap); },
        true);
}

RG_UI_TEXT void
    radiogeddon_scanner_view_set_external(RadioGeddonScannerView* instance, bool external) {
    with_view_model(
        instance->view, RadioGeddonScannerModel * m, { m->external = external; }, true);
}

RG_UI_TEXT void
    radiogeddon_scanner_view_set_selected(RadioGeddonScannerView* instance, size_t index) {
    with_view_model(
        instance->view,
        RadioGeddonScannerModel * m,
        {
            if(index < m->snap.count) m->selected = index;
        },
        true);
}

RG_UI_TEXT size_t radiogeddon_scanner_view_get_selected(RadioGeddonScannerView* instance) {
    size_t sel = 0;
    with_view_model(instance->view, RadioGeddonScannerModel * m, { sel = m->selected; }, false);
    return sel;
}

#if RG_FEATURE_DOLPHIN_LAB
RG_UI_TEXT void radiogeddon_scanner_view_set_badge(
    RadioGeddonScannerView* instance,
    const RgMascotFrame* frame,
    bool cyber) {
    with_view_model(
        instance->view,
        RadioGeddonScannerModel * m,
        {
            m->badge_on = frame != NULL;
            if(frame) m->badge = *frame;
            m->badge_cyber = cyber;
        },
        true);
}
#endif
