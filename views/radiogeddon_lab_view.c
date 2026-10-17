#include "radiogeddon_lab_view.h"
#include "../radiogeddon_edition.h" /* RG_UI_TEXT */

#if RG_FEATURE_DOLPHIN_LAB
#include "radiogeddon_mascot_draw.h"

#include <gui/elements.h>
#include <furi.h>

/* Dolphin Lab home: lab scene on the left, menu on the right, status bar. */
#define HOME_PANEL_W   48
#define HOME_DESK_Y    42
#define HOME_MENU_X    50
#define HOME_ROW_H     10
#define HOME_ROWS      5
#define HOME_STATUS_Y  53
/* Cyber Lab home. */
#define CYBER_ROWS     4
#define CYBER_ROW_H    10
#define CYBER_MENU_X   36
#define CYBER_TOP      11
/* Card text. */
#define CARD_LINE_H    10
#define CARD_MAX_LINES 5
#define CARD_TEXT_X    34

struct RadioGeddonLabView {
    View* view;
    RadioGeddonLabCallback callback;
    void* context;
    volatile bool visible;
};

typedef enum {
    LabModeHome,
    LabModeCard,
} LabMode;

typedef struct {
    uint8_t mode; // LabMode
    uint8_t style; // RgUiStyle
    uint8_t card; // RadioGeddonLabCard
    uint8_t progress; // percent, or RG_MASCOT_PROGRESS_NONE
    RgMascotFrame frame;
    uint8_t phase; // Cyber Lab radar sweep, 0..15
    uint32_t phase_ms;
    // Home
    const RadioGeddonLabItem* items;
    uint8_t count;
    uint8_t selected;
    uint8_t top;
    bool warn;
    char status_left[24];
    char status_right[20];
    char readout[24];
    uint8_t decoration; // RgDecoration on the wall
    // Card
    char title[32];
    char text[RADIOGEDDON_LAB_TEXT_MAX];
    // Transition into the current content
    RgTransition transition;
    uint8_t level; // RG_TRANSITION_DONE when none runs
} LabModel;

/* ---- text ------------------------------------------------------------------ */

static RG_UI_TEXT uint16_t lab_width(void* context, const char* text, size_t len) {
    Canvas* canvas = context;
    char buf[64];
    if(len >= sizeof(buf)) len = sizeof(buf) - 1;
    memcpy(buf, text, len);
    buf[len] = '\0';
    return canvas_string_width(canvas, buf);
}

static RG_UI_TEXT void lab_draw_line(
    Canvas* canvas,
    int32_t x,
    int32_t y,
    const char* text,
    const RgUiLine* l,
    Align align) {
    char buf[64];
    size_t len = l->len < sizeof(buf) - 1 ? l->len : sizeof(buf) - 1;
    memcpy(buf, text + l->start, len);
    buf[len] = '\0';
    canvas_draw_str_aligned(canvas, x, y, align, AlignBottom, buf);
}

/* Draw @p text wrapped into the box from @p x, @p w wide, lines from
 * baseline @p y; centred on @p cx when @p center. Returns the lines needed. */
static RG_UI_TEXT size_t lab_draw_text(
    Canvas* canvas,
    const char* text,
    int32_t x,
    int32_t y,
    uint16_t w,
    size_t max_lines,
    bool center) {
    RgUiLine lines[CARD_MAX_LINES];
    if(max_lines > CARD_MAX_LINES) max_lines = CARD_MAX_LINES;
    size_t n = rg_ui_wrap(text, w, lab_width, canvas, lines, max_lines);
    size_t shown = n < max_lines ? n : max_lines;
    for(size_t i = 0; i < shown; i++) {
        int32_t yy = y + (int32_t)i * CARD_LINE_H;
        if(center) {
            lab_draw_line(canvas, x + w / 2, yy, text, &lines[i], AlignCenter);
        } else {
            lab_draw_line(canvas, x, yy, text, &lines[i], AlignLeft);
        }
    }
    return n;
}

/* Lines @p text needs at width @p w. */
static RG_UI_TEXT size_t lab_text_lines(Canvas* canvas, const char* text, uint16_t w) {
    RgUiLine lines[CARD_MAX_LINES];
    return rg_ui_wrap(text, w, lab_width, canvas, lines, CARD_MAX_LINES);
}

/* ---- home: Dolphin Lab ----------------------------------------------------- */

static RG_UI_TEXT void
    lab_draw_desk(Canvas* canvas, int32_t x, int32_t y, int32_t w, int32_t bottom) {
    // A white desk in front of the dolphin: its top and a drawer.
    canvas_set_color(canvas, ColorWhite);
    canvas_draw_box(canvas, x, y, (size_t)w, (size_t)(bottom - y));
    canvas_set_color(canvas, ColorBlack);
    canvas_draw_line(canvas, x, y, x + w - 1, y);
    canvas_draw_line(canvas, x + 2, y + 1, x + 2, bottom - 1);
    canvas_draw_line(canvas, x + w - 3, y + 1, x + w - 3, bottom - 1);
    canvas_draw_frame(canvas, x + 6, y + 3, (size_t)(w - 12), 5);
    canvas_draw_line(canvas, x + w / 2 - 2, y + 5, x + w / 2 + 1, y + 5);
}

/* Draw @p text from (x, baseline y) cut to @p max_w pixels ("..." when cut). */
static RG_UI_TEXT void lab_draw_fit(
    Canvas* canvas,
    int32_t x,
    int32_t y,
    const char* text,
    uint16_t max_w,
    Align align) {
    char buf[32];
    strlcpy(buf, text, sizeof(buf));
    size_t len = strlen(buf);
    if(canvas_string_width(canvas, buf) > max_w) {
        while(len > 0) {
            buf[--len] = '\0';
            char tmp[36];
            snprintf(tmp, sizeof(tmp), "%s..", buf);
            if(canvas_string_width(canvas, tmp) <= max_w) {
                strlcpy(buf, tmp, sizeof(buf));
                break;
            }
        }
    }
    canvas_draw_str_aligned(canvas, x, y, align, AlignBottom, buf);
}

static RG_UI_TEXT void lab_draw_status(Canvas* canvas, LabModel* m, int32_t y) {
    canvas_set_color(canvas, ColorBlack);
    canvas_draw_line(canvas, 0, y, 127, y);
    canvas_set_font(canvas, FontSecondary);
    // The right part (session, warning, edition) gets up to 60 pixels, the
    // frequency the rest.
    uint16_t rw = 0;
    if(m->status_right[0]) {
        rw = canvas_string_width(canvas, m->status_right);
        if(rw > 60) rw = 60;
        if(m->warn) {
            canvas_draw_box(canvas, 127 - rw - 3, y + 2, rw + 3, 9);
            canvas_set_color(canvas, ColorWhite);
        }
        lab_draw_fit(canvas, 126, 63, m->status_right, 60, AlignRight);
        canvas_set_color(canvas, ColorBlack);
    }
    lab_draw_fit(canvas, 1, 63, m->status_left, (uint16_t)(122 - rw), AlignLeft);
}

static RG_UI_TEXT void lab_draw_home_menu(
    Canvas* canvas,
    LabModel* m,
    int32_t x,
    int32_t y0,
    int32_t row_h,
    uint8_t rows,
    bool cyber) {
    canvas_set_font(canvas, FontSecondary);
    for(uint8_t r = 0; r < rows; r++) {
        uint8_t i = (uint8_t)(m->top + r);
        if(i >= m->count) break;
        int32_t y = y0 + r * row_h;
        bool sel = i == m->selected;
        if(sel) {
            if(cyber) {
                canvas_draw_box(canvas, x, y, (size_t)(124 - x), (size_t)(row_h - 1));
            } else {
                canvas_draw_rbox(canvas, x, y, (size_t)(124 - x), (size_t)(row_h - 1), 2);
            }
            canvas_set_color(canvas, ColorWhite);
        }
        const RadioGeddonLabItem* it = &m->items[i];
        if(cyber) {
            canvas_draw_str(canvas, x + 2, y + row_h - 2, sel ? ">" : " ");
            canvas_draw_str(canvas, x + 8, y + row_h - 2, it->label);
        } else {
            // The icon is drawn in the colour set above.
            if(it->icon < RgSpriteCount) {
                const RgSprite* s = &rg_sprites[it->icon];
                canvas_set_bitmap_mode(canvas, true);
                canvas_draw_xbm(canvas, x + 2, y + 1, s->w, s->h, rg_sprite_bits + s->offset);
                canvas_set_bitmap_mode(canvas, false);
            }
            canvas_draw_str(canvas, x + 12, y + row_h - 2, it->label);
        }
        canvas_set_color(canvas, ColorBlack);
    }
    if(m->count > rows) {
        elements_scrollbar_pos(canvas, 128, y0, (size_t)(rows * row_h - 1), m->selected, m->count);
    }
}

static RG_UI_TEXT void lab_draw_home_dolphin(Canvas* canvas, LabModel* m) {
    const RgMascotFrame* f = &m->frame;
    // Lab: a shelf with a lamp on the wall, the dolphin behind its desk.
    canvas_draw_line(canvas, 0, 2, 9, 2);
    canvas_draw_dot(canvas, 2, 3);
    canvas_draw_dot(canvas, 7, 3);
    canvas_draw_frame(canvas, 3, 0, 3, 2);
    // The decoration (unlocked by Research level) hangs on the wall.
    rg_draw_decoration(canvas, 36, 1, m->decoration);
    rg_draw_mascot(canvas, 2, 9, f, HOME_DESK_Y, false);
    lab_draw_desk(canvas, 0, HOME_DESK_Y, HOME_PANEL_W, HOME_STATUS_Y);
    rg_draw_prop(canvas, 33, HOME_DESK_Y, f->prop, f->prop_phase, f->progress);
    // Effects last, over the scene (never over the menu).
    rg_draw_mascot_fx(canvas, 2, 9, f);
    canvas_draw_line(canvas, HOME_PANEL_W, 0, HOME_PANEL_W, HOME_STATUS_Y - 1);
    lab_draw_home_menu(canvas, m, HOME_MENU_X, 1, HOME_ROW_H, HOME_ROWS, false);
    lab_draw_status(canvas, m, HOME_STATUS_Y);
}

/* ---- home: Cyber Lab ------------------------------------------------------- */

static RG_UI_TEXT void
    lab_draw_corners(Canvas* canvas, int32_t x, int32_t y, int32_t w, int32_t h) {
    int32_t r = x + w - 1, b = y + h - 1;
    canvas_draw_line(canvas, x, y, x + 3, y);
    canvas_draw_line(canvas, x, y, x, y + 3);
    canvas_draw_line(canvas, r - 3, y, r, y);
    canvas_draw_line(canvas, r, y, r, y + 3);
    canvas_draw_line(canvas, x, b - 3, x, b);
    canvas_draw_line(canvas, x, b, x + 3, b);
    canvas_draw_line(canvas, r, b - 3, r, b);
    canvas_draw_line(canvas, r - 3, b, r, b);
}

static RG_UI_TEXT void lab_draw_home_cyber(Canvas* canvas, LabModel* m) {
    // Title tab.
    canvas_draw_box(canvas, 0, 0, 128, 10);
    canvas_set_color(canvas, ColorWhite);
    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str(canvas, 2, 8, "CYBER LAB");
    canvas_draw_str_aligned(canvas, 126, 8, AlignRight, AlignBottom, m->readout);
    canvas_set_color(canvas, ColorBlack);
    // Radar with grid ticks; the mascot is only a small head under it.
    rg_draw_radar(canvas, 16, 27, 13, m->phase);
    canvas_draw_line(canvas, 16, 13, 16, 15);
    canvas_draw_line(canvas, 16, 39, 16, 41);
    canvas_draw_line(canvas, 2, 27, 4, 27);
    canvas_draw_line(canvas, 28, 27, 30, 27);
    rg_draw_badge(canvas, 9, 43, false, &m->frame);
    lab_draw_corners(canvas, 0, 11, 33, 42);
    lab_draw_home_menu(canvas, m, CYBER_MENU_X, CYBER_TOP + 1, CYBER_ROW_H, CYBER_ROWS, true);
    lab_draw_corners(canvas, CYBER_MENU_X - 2, CYBER_TOP, 128 - CYBER_MENU_X + 2, 42);
    lab_draw_status(canvas, m, HOME_STATUS_Y + 1);
}

/* ---- cards ----------------------------------------------------------------- */

static RG_UI_TEXT void lab_draw_card_progress(Canvas* canvas, LabModel* m) {
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str_aligned(canvas, 64, 1, AlignCenter, AlignTop, m->title);
    bool cyber = m->style == RgUiStyleCyberLab;
    if(cyber) {
        rg_draw_radar(canvas, 20, 36, 12, m->phase);
        lab_draw_corners(canvas, 4, 20, 33, 33);
    } else {
        RgMascotFrame f = m->frame;
        f.prop = RgMascotPropComputer;
        f.progress = m->card == RadioGeddonLabCardProgress ? m->progress : RG_MASCOT_PROGRESS_NONE;
        rg_draw_mascot(canvas, 0, 22, &f, 54, false);
        lab_draw_desk(canvas, 0, 54, 50, 64);
        rg_draw_prop(canvas, 28, 54, RgMascotPropComputer, f.prop_phase, f.progress);
    }
    if(m->card == RadioGeddonLabCardProgress && m->progress <= 100) {
        char pct[8];
        snprintf(pct, sizeof(pct), "%u%%", (unsigned)m->progress);
        canvas_set_font(canvas, FontPrimary);
        canvas_draw_str_aligned(canvas, 88, 32, AlignCenter, AlignBottom, pct);
        canvas_draw_rframe(canvas, 56, 37, 66, 7, 2);
        int32_t w = (int32_t)m->progress * 62 / 100;
        if(w > 0) canvas_draw_box(canvas, 58, 39, (size_t)w, 3);
    } else {
        canvas_set_font(canvas, FontSecondary);
        canvas_draw_str_aligned(canvas, 88, 36, AlignCenter, AlignBottom, "Please wait");
    }
}

static RG_UI_TEXT void lab_draw_card_text(Canvas* canvas, LabModel* m) {
    bool cyber = m->style == RgUiStyleCyberLab;
    canvas_set_font(canvas, FontSecondary);
    // With the dolphin when the text fits beside it, else across the screen.
    uint16_t narrow = (uint16_t)(127 - CARD_TEXT_X);
    canvas_set_font(canvas, FontPrimary);
    bool title_fits = canvas_string_width(canvas, m->title) <= narrow;
    canvas_set_font(canvas, FontSecondary);
    bool beside = !cyber && title_fits && lab_text_lines(canvas, m->text, narrow) <= 4;

    if(beside) {
        RgMascotFrame f = m->frame;
        f.prop = RgMascotPropNone;
        rg_draw_mascot(canvas, 0, 22, &f, 64, true);
        canvas_set_font(canvas, FontPrimary);
        canvas_draw_str(canvas, CARD_TEXT_X, 11, m->title);
        canvas_set_font(canvas, FontSecondary);
        lab_draw_text(canvas, m->text, CARD_TEXT_X, 24, narrow, 4, false);
    } else {
        if(cyber) {
            canvas_draw_box(canvas, 0, 0, 128, 12);
            canvas_set_color(canvas, ColorWhite);
        } else {
            rg_draw_badge(canvas, 1, 1, false, &m->frame);
        }
        canvas_set_font(canvas, FontPrimary);
        canvas_draw_str_aligned(canvas, 64, 10, AlignCenter, AlignBottom, m->title);
        canvas_set_color(canvas, ColorBlack);
        canvas_set_font(canvas, FontSecondary);
        lab_draw_text(canvas, m->text, 1, 23, 126, CARD_MAX_LINES, true);
    }
}

static RG_UI_TEXT void lab_draw_card(Canvas* canvas, LabModel* m) {
    switch(m->card) {
    case RadioGeddonLabCardBusy:
    case RadioGeddonLabCardProgress:
        lab_draw_card_progress(canvas, m);
        break;
    default:
        lab_draw_card_text(canvas, m);
        break;
    }
}

/* ---- transitions ----------------------------------------------------------- */

static RG_UI_TEXT void lab_draw_transition(Canvas* canvas, LabModel* m) {
    uint8_t level = m->level;
    if(level == RG_TRANSITION_DONE) return;
    switch(m->transition.kind) {
    case RgTransitionWipe: {
        // An oscilloscope trace sweeps left to right, revealing behind it.
        int32_t edge = (int32_t)level * 128 / 255;
        canvas_set_color(canvas, ColorWhite);
        if(edge < 128) canvas_draw_box(canvas, edge, 0, (size_t)(128 - edge), 64);
        canvas_set_color(canvas, ColorBlack);
        for(int32_t y = 0; y < 64; y++) {
            static const int8_t wiggle[8] = {0, 1, 2, 1, 0, -1, -2, -1};
            int32_t x = edge + wiggle[(y + level / 16) & 7];
            if(x >= 0 && x < 128) canvas_draw_dot(canvas, x, y);
        }
        break;
    }
    case RgTransitionDither:
        // Ordered dither: the pixels not yet revealed are cleared.
        canvas_set_color(canvas, ColorWhite);
        for(int32_t y = 0; y < 64; y++)
            for(int32_t x = 0; x < 128; x++)
                if(rg_dither_hidden((uint32_t)x, (uint32_t)y, level))
                    canvas_draw_dot(canvas, x, y);
        canvas_set_color(canvas, ColorBlack);
        break;
    default:
        break;
    }
}

/* ---- view ------------------------------------------------------------------ */

static RG_UI_TEXT void lab_draw(Canvas* canvas, void* model) {
    LabModel* m = model;
    canvas_clear(canvas);
    canvas_set_color(canvas, ColorBlack);
    if(m->mode == LabModeHome) {
        if(m->style == RgUiStyleCyberLab) {
            lab_draw_home_cyber(canvas, m);
        } else {
            lab_draw_home_dolphin(canvas, m);
        }
    } else {
        lab_draw_card(canvas, m);
    }
    lab_draw_transition(canvas, m);
}

static RG_UI_TEXT void
    lab_send(RadioGeddonLabView* instance, RadioGeddonLabEvent event, uint32_t index) {
    if(instance->callback) instance->callback(event, index, instance->context);
}

static RG_UI_TEXT bool lab_input(InputEvent* event, void* context) {
    RadioGeddonLabView* instance = context;
    if(event->type != InputTypeShort && event->type != InputTypeLong &&
       event->type != InputTypeRepeat)
        return false;
    uint8_t mode = LabModeHome, card = 0, selected = 0;
    with_view_model(
        instance->view,
        LabModel * m,
        {
            // A key never waits for an animation.
            rg_transition_finish(&m->transition);
            m->level = RG_TRANSITION_DONE;
            mode = m->mode;
            card = m->card;
            if(mode == LabModeHome && m->count > 0 && event->type != InputTypeLong) {
                uint8_t n = m->count;
                switch(event->key) {
                case InputKeyUp:
                    m->selected = m->selected ? (uint8_t)(m->selected - 1) : (uint8_t)(n - 1);
                    break;
                case InputKeyDown:
                    m->selected = (uint8_t)(m->selected + 1 < n ? m->selected + 1 : 0);
                    break;
                case InputKeyLeft:
                    m->selected = m->selected >= HOME_ROWS ? (uint8_t)(m->selected - HOME_ROWS) :
                                                             0;
                    break;
                case InputKeyRight:
                    m->selected =
                        (uint8_t)(m->selected + HOME_ROWS < n ? m->selected + HOME_ROWS : n - 1);
                    break;
                default:
                    break;
                }
                uint8_t rows = m->style == RgUiStyleCyberLab ? CYBER_ROWS : HOME_ROWS;
                m->top = rg_ui_menu_top(m->selected, m->top, n, rows);
            }
            selected = m->selected;
        },
        true);
    if(event->key != InputKeyBack) lab_send(instance, RadioGeddonLabEventKey, 0);

    if(mode == LabModeHome) {
        if(event->key == InputKeyOk && event->type == InputTypeShort) {
            lab_send(instance, RadioGeddonLabEventSelect, selected);
            return true;
        }
        if(event->key == InputKeyOk && event->type == InputTypeLong) {
            lab_send(instance, RadioGeddonLabEventShortcut, selected);
            return true;
        }
        return event->key != InputKeyBack;
    }
    // Cards: OK closes the ones that can be closed; Back is the scene's.
    if(event->key == InputKeyOk && event->type == InputTypeShort &&
       (card == RadioGeddonLabCardMessage || card == RadioGeddonLabCardSaved ||
        card == RadioGeddonLabCardReveal)) {
        lab_send(instance, RadioGeddonLabEventDismiss, 0);
        return true;
    }
    return event->key != InputKeyBack;
}

static RG_UI_TEXT void lab_enter(void* context) {
    RadioGeddonLabView* instance = context;
    instance->visible = true;
}

static RG_UI_TEXT void lab_exit(void* context) {
    RadioGeddonLabView* instance = context;
    instance->visible = false;
}

RG_UI_TEXT RadioGeddonLabView* radiogeddon_lab_view_alloc(void) {
    RadioGeddonLabView* instance = malloc(sizeof(RadioGeddonLabView));
    instance->view = view_alloc();
    instance->callback = NULL;
    instance->context = NULL;
    instance->visible = false;
    view_set_context(instance->view, instance);
    view_allocate_model(instance->view, ViewModelTypeLocking, sizeof(LabModel));
    view_set_draw_callback(instance->view, lab_draw);
    view_set_input_callback(instance->view, lab_input);
    view_set_enter_callback(instance->view, lab_enter);
    view_set_exit_callback(instance->view, lab_exit);
    with_view_model(
        instance->view,
        LabModel * m,
        {
            memset(m, 0, sizeof(*m));
            m->level = RG_TRANSITION_DONE;
            m->progress = RG_MASCOT_PROGRESS_NONE;
            rg_mascot_pose(RgMascotStateIdle, 0, 0, RG_MASCOT_PROGRESS_NONE, false, &m->frame);
        },
        false);
    return instance;
}

RG_UI_TEXT void radiogeddon_lab_view_free(RadioGeddonLabView* instance) {
    furi_assert(instance);
    view_free(instance->view);
    free(instance);
}

RG_UI_TEXT View* radiogeddon_lab_view_get_view(RadioGeddonLabView* instance) {
    return instance->view;
}

RG_UI_TEXT void radiogeddon_lab_view_set_callback(
    RadioGeddonLabView* instance,
    RadioGeddonLabCallback callback,
    void* context) {
    instance->callback = callback;
    instance->context = context;
}

RG_UI_TEXT bool radiogeddon_lab_view_is_visible(RadioGeddonLabView* instance) {
    return instance->visible;
}

RG_UI_TEXT void radiogeddon_lab_view_set_style(RadioGeddonLabView* instance, RgUiStyle style) {
    with_view_model(instance->view, LabModel * m, { m->style = (uint8_t)style; }, true);
}

RG_UI_TEXT void radiogeddon_lab_view_show_home(
    RadioGeddonLabView* instance,
    const RadioGeddonLabItem* items,
    uint8_t count,
    uint8_t selected) {
    if(count > RADIOGEDDON_LAB_MAX_ITEMS) count = RADIOGEDDON_LAB_MAX_ITEMS;
    with_view_model(
        instance->view,
        LabModel * m,
        {
            m->mode = LabModeHome;
            m->items = items;
            m->count = count;
            m->selected = count && selected < count ? selected : 0;
            uint8_t rows = m->style == RgUiStyleCyberLab ? CYBER_ROWS : HOME_ROWS;
            m->top = rg_ui_menu_top(m->selected, 0, count, rows);
        },
        true);
}

RG_UI_TEXT uint8_t radiogeddon_lab_view_get_selected(RadioGeddonLabView* instance) {
    uint8_t sel = 0;
    with_view_model(instance->view, LabModel * m, { sel = m->selected; }, false);
    return sel;
}

RG_UI_TEXT void
    radiogeddon_lab_view_set_decoration(RadioGeddonLabView* instance, uint8_t decoration) {
    with_view_model(instance->view, LabModel * m, { m->decoration = decoration; }, false);
}

RG_UI_TEXT void radiogeddon_lab_view_set_status(
    RadioGeddonLabView* instance,
    const char* left,
    const char* right,
    bool warn) {
    with_view_model(
        instance->view,
        LabModel * m,
        {
            strlcpy(m->status_left, left ? left : "", sizeof(m->status_left));
            strlcpy(m->status_right, right ? right : "", sizeof(m->status_right));
            m->warn = warn;
        },
        true);
}

RG_UI_TEXT void radiogeddon_lab_view_set_readout(RadioGeddonLabView* instance, const char* text) {
    with_view_model(
        instance->view,
        LabModel * m,
        { strlcpy(m->readout, text ? text : "", sizeof(m->readout)); },
        true);
}

RG_UI_TEXT void radiogeddon_lab_view_show_card(
    RadioGeddonLabView* instance,
    RadioGeddonLabCard card,
    const char* title,
    const char* text) {
    with_view_model(
        instance->view,
        LabModel * m,
        {
            m->mode = LabModeCard;
            m->card = (uint8_t)(card < RadioGeddonLabCardCount ? card : RadioGeddonLabCardMessage);
            m->progress = RG_MASCOT_PROGRESS_NONE;
            strlcpy(m->title, title ? title : "", sizeof(m->title));
            strlcpy(m->text, text ? text : "", sizeof(m->text));
            rg_transition_finish(&m->transition);
            m->level = RG_TRANSITION_DONE;
        },
        true);
}

RG_UI_TEXT void radiogeddon_lab_view_set_progress(RadioGeddonLabView* instance, uint8_t percent) {
    with_view_model(
        instance->view,
        LabModel * m,
        {
            m->card = RadioGeddonLabCardProgress;
            m->progress = percent;
        },
        true);
}

RG_UI_TEXT void radiogeddon_lab_view_transition(
    RadioGeddonLabView* instance,
    RgTransitionKind kind,
    uint16_t duration_ms,
    uint32_t now) {
    with_view_model(
        instance->view,
        LabModel * m,
        {
            rg_transition_start(&m->transition, kind, duration_ms, now);
            m->level = rg_transition_level(&m->transition, now);
        },
        true);
}

RG_UI_TEXT bool radiogeddon_lab_view_transition_active(RadioGeddonLabView* instance) {
    bool active = false;
    with_view_model(
        instance->view, LabModel * m, { active = m->level != RG_TRANSITION_DONE; }, false);
    return active;
}

RG_UI_TEXT void radiogeddon_lab_view_update(
    RadioGeddonLabView* instance,
    const RgMascotFrame* frame,
    uint32_t now) {
    bool changed = false;
    with_view_model(
        instance->view,
        LabModel * m,
        {
            if(frame && rg_mascot_frame_changed(&m->frame, frame)) {
                m->frame = *frame;
                changed = true;
            }
            uint8_t level = rg_transition_level(&m->transition, now);
            if(level != m->level) {
                m->level = level;
                changed = true;
            }
            // The Cyber Lab radar turns at 4 steps a second.
            if(m->style == RgUiStyleCyberLab && now - m->phase_ms >= 250u) {
                m->phase_ms = now;
                m->phase = (uint8_t)((m->phase + 1u) & 15u);
                changed = true;
            }
        },
        changed);
}

#endif
