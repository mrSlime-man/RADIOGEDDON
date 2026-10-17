#include "radiogeddon_heatmap_view.h"

#if RG_FEATURE_ACTIVITY_LOG

#include <furi.h>
#include <gui/elements.h>

#define HM_TOP    11 /* first grid line */
#define HM_HEIGHT 44 /* grid lines */
#define HM_CELL_W 2

typedef enum {
    HmShowBursts,
    HmShowStrength,
    HmShowLegend,
    HmShowCount,
} HmShow;

struct RadioGeddonHeatmapView {
    View* view;
};

typedef struct {
    const RgHeatmap* map;
    uint8_t row; // cursor (display row, by frequency)
    uint8_t col;
    uint8_t top; // first visible row
    uint8_t show; // HmShow
} HeatmapModel;

/* Rows of the grid: as tall as fit, 3 to 8 lines (one blank between). */
static uint8_t hm_row_h(const RgHeatmap* h) {
    uint8_t n = h->channels ? h->channels : 1;
    uint8_t rh = (uint8_t)(HM_HEIGHT / n);
    if(rh > 8) rh = 8;
    if(rh < 3) rh = 3;
    return rh;
}

/* One cell: the number of lit pixels grows with the level, in an order that
 * keeps them spread; a quiet cell is a single dot, a cell nobody listened to
 * is blank. */
static void hm_cell(Canvas* canvas, int x, int y, uint8_t hh, RgHeatLevel level) {
    if(level == RgHeatNotObserved) return;
    if(level == RgHeatQuiet) {
        canvas_draw_dot(canvas, x, y + hh / 2);
        return;
    }
    if(level == RgHeatHigh) {
        canvas_draw_box(canvas, x, y, HM_CELL_W, hh);
        return;
    }
    uint8_t pixels = (uint8_t)(HM_CELL_W * hh);
    uint8_t lit = level == RgHeatLow ? pixels / 4u : pixels / 2u;
    if(level == RgHeatLow && lit < 2) lit = 2;
    if(level == RgHeatMid && lit < 3) lit = 3;
    for(uint8_t i = 0; i < lit; i++) {
        uint8_t k = i < hh ? i : (uint8_t)(i - hh);
        uint8_t dx = i < hh ? (k & 1u) : (uint8_t)(1u - (k & 1u));
        canvas_draw_dot(canvas, x + dx, y + k);
    }
}

static void hm_legend(Canvas* canvas) {
    static const char* const text[] = {
        "not observed",
        "observed, quiet",
        "some activity",
        "more",
        "most",
    };
    static const RgHeatLevel level[] = {
        RgHeatNotObserved, RgHeatQuiet, RgHeatLow, RgHeatMid, RgHeatHigh};
    canvas_set_font(canvas, FontSecondary);
    for(int i = 0; i < 5; i++) {
        int y = HM_TOP + 1 + i * 9;
        canvas_draw_frame(canvas, 2, y - 1, 10, 8);
        for(int c = 0; c < 4; c++)
            hm_cell(canvas, 3 + c * HM_CELL_W, y, 6, level[i]);
        canvas_draw_str(canvas, 16, y + 6, text[i]);
    }
}

static void hm_draw(Canvas* canvas, void* model) {
    HeatmapModel* m = model;
    const RgHeatmap* h = m->map;
    canvas_clear(canvas);
    canvas_set_color(canvas, ColorBlack);
    canvas_draw_box(canvas, 0, 0, 128, 10);
    canvas_set_color(canvas, ColorWhite);
    canvas_set_font(canvas, FontSecondary);
    if(!h || h->channels == 0 || h->cols_used == 0) {
        canvas_draw_str(canvas, 2, 8, "Activity map");
        canvas_set_color(canvas, ColorBlack);
        canvas_draw_str_aligned(canvas, 64, 32, AlignCenter, AlignCenter, "No rows in this log.");
        return;
    }
    char text[40];
    rg_heatmap_col_time(h, m->col, text, sizeof(text));
    canvas_draw_str(canvas, 2, 8, text);
    static const char* const shown[HmShowCount] = {"bursts", "strength", "legend"};
    canvas_draw_str_aligned(canvas, 126, 8, AlignRight, AlignBottom, shown[m->show]);
    canvas_set_color(canvas, ColorBlack);
    if(m->show == HmShowLegend) {
        hm_legend(canvas);
        return;
    }
    RgHeatMetric metric = m->show == HmShowStrength ? RgHeatMetricStrength : RgHeatMetricEvents;
    uint8_t rh = hm_row_h(h);
    uint8_t visible = (uint8_t)(HM_HEIGHT / rh);
    for(uint8_t r = 0; r < visible && m->top + r < h->channels; r++) {
        int y = HM_TOP + r * rh;
        for(uint8_t c = 0; c < h->cols_used; c++) {
            const RgHeatCell* cell = rg_heatmap_cell(h, m->top + r, c);
            hm_cell(
                canvas, c * HM_CELL_W, y, (uint8_t)(rh - 1), rg_heatmap_level(h, cell, metric));
        }
    }
    // The cursor: an inverted frame around its cell, visible on any state.
    // Clipped to the screen at the first and last columns.
    int cy = HM_TOP + (m->row - m->top) * rh;
    int x0 = m->col * HM_CELL_W - 1, x1 = m->col * HM_CELL_W + HM_CELL_W;
    if(x0 < 0) x0 = 0;
    if(x1 > 127) x1 = 127;
    canvas_set_color(canvas, ColorXOR);
    canvas_draw_frame(canvas, x0, cy - 1, (size_t)(x1 - x0 + 1), rh + 1);
    canvas_set_color(canvas, ColorBlack);
    // More rows above / below.
    if(m->top > 0) canvas_draw_dot(canvas, 127, HM_TOP);
    if(m->top + visible < h->channels) canvas_draw_dot(canvas, 127, HM_TOP + HM_HEIGHT - 1);
    rg_heatmap_cell_text(h, m->row, m->col, text, sizeof(text));
    canvas_draw_str(canvas, 0, 62, text);
}

static bool hm_input(InputEvent* event, void* context) {
    RadioGeddonHeatmapView* instance = context;
    if(event->key == InputKeyBack) return false;
    if(event->type != InputTypeShort && event->type != InputTypeRepeat) return true;
    with_view_model(
        instance->view,
        HeatmapModel * m,
        {
            const RgHeatmap* h = m->map;
            if(event->key == InputKeyOk && event->type == InputTypeShort) {
                m->show = (uint8_t)((m->show + 1u) % HmShowCount);
            } else if(h && h->channels && h->cols_used) {
                uint8_t visible = (uint8_t)(HM_HEIGHT / hm_row_h(h));
                if(event->key == InputKeyLeft && m->col > 0) m->col--;
                if(event->key == InputKeyRight && m->col + 1u < h->cols_used) m->col++;
                if(event->key == InputKeyUp && m->row > 0) m->row--;
                if(event->key == InputKeyDown && m->row + 1u < h->channels) m->row++;
                if(m->row < m->top) m->top = m->row;
                if(m->row >= m->top + visible) m->top = (uint8_t)(m->row - visible + 1u);
            }
        },
        true);
    return true;
}

RadioGeddonHeatmapView* radiogeddon_heatmap_view_alloc(void) {
    RadioGeddonHeatmapView* instance = malloc(sizeof(RadioGeddonHeatmapView));
    instance->view = view_alloc();
    view_set_context(instance->view, instance);
    view_allocate_model(instance->view, ViewModelTypeLocking, sizeof(HeatmapModel));
    view_set_draw_callback(instance->view, hm_draw);
    view_set_input_callback(instance->view, hm_input);
    with_view_model(instance->view, HeatmapModel * m, { memset(m, 0, sizeof(*m)); }, false);
    return instance;
}

void radiogeddon_heatmap_view_free(RadioGeddonHeatmapView* instance) {
    view_free(instance->view);
    free(instance);
}

View* radiogeddon_heatmap_view_get_view(RadioGeddonHeatmapView* instance) {
    return instance->view;
}

void radiogeddon_heatmap_view_set_map(RadioGeddonHeatmapView* instance, const RgHeatmap* map) {
    with_view_model(
        instance->view,
        HeatmapModel * m,
        {
            m->map = map;
            m->row = m->top = 0;
            // Start at the end of the log: the latest activity.
            m->col = map && map->cols_used ? (uint8_t)(map->cols_used - 1u) : 0;
            m->show = HmShowBursts;
        },
        true);
}

size_t radiogeddon_heatmap_view_memory(void) {
    return sizeof(RadioGeddonHeatmapView) + sizeof(HeatmapModel) + 256u;
}

#endif
