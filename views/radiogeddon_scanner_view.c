#include "radiogeddon_scanner_view.h"
#include <gui/elements.h>
#include <furi.h>

#define RADIOGEDDON_SCANNER_MAX 24
#define RSSI_FLOOR              (-100.0f)
#define RSSI_CEIL               (-30.0f)

struct RadioGeddonScannerView {
    View* view;
    RadioGeddonScannerCallback callback;
    void* context;
};

typedef struct {
    const uint32_t* frequencies;
    size_t count;
    float rssi[RADIOGEDDON_SCANNER_MAX];
    size_t selected;
    size_t top; // first visible row
} RadioGeddonScannerModel;

#define VISIBLE_ROWS 4

static void radiogeddon_scanner_view_draw(Canvas* canvas, void* model) {
    RadioGeddonScannerModel* m = model;
    canvas_clear(canvas);

    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, 2, 9, "Scanner");
    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str_aligned(canvas, 126, 9, AlignRight, AlignBottom, "RSSI dBm");
    canvas_draw_line(canvas, 0, 11, 128, 11);

    if(m->count == 0) {
        canvas_draw_str(canvas, 2, 30, "No frequencies.");
        return;
    }

    // Keep selection within the visible window.
    if(m->selected < m->top) m->top = m->selected;
    if(m->selected >= m->top + VISIBLE_ROWS) m->top = m->selected - (VISIBLE_ROWS - 1);

    for(size_t row = 0; row < VISIBLE_ROWS; row++) {
        size_t idx = m->top + row;
        if(idx >= m->count) break;
        int32_t y = 14 + (int32_t)row * 13;
        bool sel = (idx == m->selected);
        if(sel) {
            canvas_set_color(canvas, ColorBlack);
            canvas_draw_box(canvas, 0, y, 128, 13);
            canvas_set_color(canvas, ColorWhite);
        }

        char freq_str[16];
        uint32_t hz = m->frequencies[idx];
        snprintf(
            freq_str,
            sizeof(freq_str),
            "%lu.%02lu",
            (unsigned long)(hz / 1000000),
            (unsigned long)((hz % 1000000) / 10000));
        canvas_draw_str(canvas, 2, y + 10, freq_str);

        float r = m->rssi[idx];
        // Bar
        float norm = (r - RSSI_FLOOR) / (RSSI_CEIL - RSSI_FLOOR);
        if(norm < 0) norm = 0;
        if(norm > 1) norm = 1;
        int32_t bar_x = 44;
        int32_t bar_w = 54;
        canvas_draw_frame(canvas, bar_x, y + 2, bar_w, 8);
        int32_t fill = (int32_t)(norm * (bar_w - 2));
        if(fill > 0) canvas_draw_box(canvas, bar_x + 1, y + 3, fill, 6);

        char rssi_str[8];
        snprintf(rssi_str, sizeof(rssi_str), "%d", (int)r);
        canvas_draw_str_aligned(canvas, 126, y + 10, AlignRight, AlignBottom, rssi_str);

        if(sel) canvas_set_color(canvas, ColorBlack);
    }

    elements_scrollbar(canvas, m->selected, m->count);
}

static bool radiogeddon_scanner_view_input(InputEvent* event, void* context) {
    RadioGeddonScannerView* instance = context;
    bool consumed = false;

    if(event->type == InputTypeShort || event->type == InputTypeRepeat) {
        if(event->key == InputKeyUp) {
            with_view_model(
                instance->view,
                RadioGeddonScannerModel * m,
                {
                    if(m->selected > 0) m->selected--;
                },
                true);
            consumed = true;
        } else if(event->key == InputKeyDown) {
            with_view_model(
                instance->view,
                RadioGeddonScannerModel * m,
                {
                    if(m->selected + 1 < m->count) m->selected++;
                },
                true);
            consumed = true;
        } else if(event->key == InputKeyOk) {
            if(instance->callback) {
                instance->callback(RadioGeddonScannerEventSelect, instance->context);
            }
            consumed = true;
        }
    }
    return consumed;
}

RadioGeddonScannerView* radiogeddon_scanner_view_alloc(void) {
    RadioGeddonScannerView* instance = malloc(sizeof(RadioGeddonScannerView));
    instance->view = view_alloc();
    instance->callback = NULL;
    instance->context = NULL;
    view_set_context(instance->view, instance);
    view_allocate_model(
        instance->view, ViewModelTypeLocking, sizeof(RadioGeddonScannerModel));
    view_set_draw_callback(instance->view, radiogeddon_scanner_view_draw);
    view_set_input_callback(instance->view, radiogeddon_scanner_view_input);

    with_view_model(
        instance->view,
        RadioGeddonScannerModel * m,
        {
            m->frequencies = NULL;
            m->count = 0;
            m->selected = 0;
            m->top = 0;
            for(size_t i = 0; i < RADIOGEDDON_SCANNER_MAX; i++) m->rssi[i] = RSSI_FLOOR;
        },
        true);

    return instance;
}

void radiogeddon_scanner_view_free(RadioGeddonScannerView* instance) {
    furi_assert(instance);
    view_free(instance->view);
    free(instance);
}

View* radiogeddon_scanner_view_get_view(RadioGeddonScannerView* instance) {
    return instance->view;
}

void radiogeddon_scanner_view_set_callback(
    RadioGeddonScannerView* instance,
    RadioGeddonScannerCallback callback,
    void* context) {
    instance->callback = callback;
    instance->context = context;
}

void radiogeddon_scanner_view_set_frequencies(
    RadioGeddonScannerView* instance,
    const uint32_t* frequencies,
    size_t count) {
    if(count > RADIOGEDDON_SCANNER_MAX) count = RADIOGEDDON_SCANNER_MAX;
    with_view_model(
        instance->view,
        RadioGeddonScannerModel * m,
        {
            m->frequencies = frequencies;
            m->count = count;
            m->selected = 0;
            m->top = 0;
        },
        true);
}

void radiogeddon_scanner_view_set_rssi(
    RadioGeddonScannerView* instance,
    size_t index,
    float rssi) {
    if(index >= RADIOGEDDON_SCANNER_MAX) return;
    with_view_model(
        instance->view, RadioGeddonScannerModel * m, { m->rssi[index] = rssi; }, true);
}

size_t radiogeddon_scanner_view_get_selected(RadioGeddonScannerView* instance) {
    size_t sel = 0;
    with_view_model(
        instance->view, RadioGeddonScannerModel * m, { sel = m->selected; }, false);
    return sel;
}
