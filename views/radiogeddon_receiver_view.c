#include "radiogeddon_receiver_view.h"
#include <gui/elements.h>
#include <furi.h>

#define RSSI_FLOOR (-100.0f)
#define RSSI_CEIL  (-30.0f)

struct RadioGeddonReceiverView {
    View* view;
    RadioGeddonReceiverCallback callback;
    void* context;
};

typedef struct {
    uint32_t frequency;
    char preset_label[12];
    float rssi;
    bool recording;
    uint32_t samples;
    uint32_t elapsed_ms;
    uint32_t lost;
    uint8_t fill_pct;
    size_t history_count;
    size_t selected;
    char latest[22];
    bool hopping;
    float threshold;
    char status[26];
} RadioGeddonReceiverModel;

/* "12345", or "123k" from 100000 up, so the REC line stays short. */
static void radiogeddon_receiver_view_compact(char* out, size_t size, uint32_t v) {
    if(v < 100000u) {
        snprintf(out, size, "%lu", (unsigned long)v);
    } else {
        snprintf(out, size, "%luk", (unsigned long)(v / 1000u));
    }
}

static void radiogeddon_receiver_view_draw(Canvas* canvas, void* model) {
    RadioGeddonReceiverModel* m = model;
    canvas_clear(canvas);

    // Header: frequency + preset
    canvas_set_font(canvas, FontPrimary);
    char header[24];
    snprintf(
        header,
        sizeof(header),
        "%lu.%02lu %s",
        (unsigned long)(m->frequency / 1000000),
        (unsigned long)((m->frequency % 1000000) / 10000),
        m->preset_label);
    canvas_draw_str(canvas, 2, 9, header);
    canvas_draw_line(canvas, 0, 11, 128, 11);

    // RSSI meter
    canvas_set_font(canvas, FontSecondary);
    canvas_draw_str(canvas, 2, 21, "RSSI");
    float norm = (m->rssi - RSSI_FLOOR) / (RSSI_CEIL - RSSI_FLOOR);
    if(norm < 0) norm = 0;
    if(norm > 1) norm = 1;
    canvas_draw_frame(canvas, 26, 14, 72, 8);
    int32_t fill = (int32_t)(norm * 70);
    if(fill > 0) canvas_draw_box(canvas, 27, 15, fill, 6);
    if(m->threshold > -127.0f) {
        float tn = (m->threshold - RSSI_FLOOR) / (RSSI_CEIL - RSSI_FLOOR);
        if(tn < 0) tn = 0;
        if(tn > 1) tn = 1;
        int32_t tx = 27 + (int32_t)(tn * 70);
        canvas_draw_line(canvas, tx, 13, tx, 22);
    }
    char rssi_str[8];
    snprintf(rssi_str, sizeof(rssi_str), "%d", (int)m->rssi);
    canvas_draw_str_aligned(canvas, 126, 21, AlignRight, AlignBottom, rssi_str);

    // Recording status line: time, samples, and losses or buffer fill.
    if(m->recording) {
        canvas_set_color(canvas, ColorBlack);
        canvas_draw_box(canvas, 0, 24, 128, 11);
        canvas_set_color(canvas, ColorWhite);
        char rec[40];
        char num[12];
        radiogeddon_receiver_view_compact(num, sizeof(num), m->samples);
        uint32_t s10 = m->elapsed_ms / 100;
        if(s10 < 1000) {
            snprintf(
                rec,
                sizeof(rec),
                "REC %lu.%lus %s",
                (unsigned long)(s10 / 10),
                (unsigned long)(s10 % 10),
                num);
        } else {
            uint32_t sec = s10 / 10;
            snprintf(
                rec,
                sizeof(rec),
                "REC %lum%02lus %s",
                (unsigned long)(sec / 60),
                (unsigned long)(sec % 60),
                num);
        }
        canvas_draw_str(canvas, 2, 33, rec);
        char right[24];
        right[0] = '\0';
        if(m->lost > 0) {
            radiogeddon_receiver_view_compact(num, sizeof(num), m->lost);
            snprintf(right, sizeof(right), "lost %s", num);
        } else if(m->fill_pct >= 50) {
            snprintf(right, sizeof(right), "buf %u%%", (unsigned)m->fill_pct);
        }
        if(right[0]) canvas_draw_str_aligned(canvas, 126, 33, AlignRight, AlignBottom, right);
        canvas_set_color(canvas, ColorBlack);
    } else if(m->hopping) {
        canvas_draw_str(canvas, 2, 33, m->status[0] ? m->status : "Hopping frequencies...");
    } else {
        canvas_draw_str(canvas, 2, 33, m->status[0] ? m->status : "Left: record RAW");
    }
    canvas_draw_line(canvas, 0, 36, 128, 36);

    // Decoded signals
    char dec[28];
    snprintf(dec, sizeof(dec), "Decoded: %u", (unsigned)m->history_count);
    canvas_draw_str(canvas, 2, 46, dec);

    if(m->history_count > 0) {
        char sel[28];
        snprintf(sel, sizeof(sel), "[%u] %s", (unsigned)(m->selected + 1), m->latest);
        canvas_draw_str(canvas, 2, 56, sel);
        elements_button_center(canvas, "Save");
    } else {
        canvas_draw_str(canvas, 2, 56, "Listening...");
    }
}

static bool radiogeddon_receiver_view_input(InputEvent* event, void* context) {
    RadioGeddonReceiverView* instance = context;
    bool consumed = false;

    if(event->type == InputTypeShort || event->type == InputTypeRepeat) {
        switch(event->key) {
        case InputKeyUp:
            with_view_model(
                instance->view,
                RadioGeddonReceiverModel * m,
                {
                    if(m->selected > 0) m->selected--;
                },
                true);
            consumed = true;
            break;
        case InputKeyDown:
            with_view_model(
                instance->view,
                RadioGeddonReceiverModel * m,
                {
                    if(m->selected + 1 < m->history_count) m->selected++;
                },
                true);
            consumed = true;
            break;
        case InputKeyOk:
            if(event->type == InputTypeShort && instance->callback)
                instance->callback(RadioGeddonReceiverEventSave, instance->context);
            consumed = true;
            break;
        case InputKeyRight:
            if(event->type == InputTypeShort && instance->callback)
                instance->callback(RadioGeddonReceiverEventRight, instance->context);
            consumed = true;
            break;
        case InputKeyLeft:
            if(event->type == InputTypeShort && instance->callback)
                instance->callback(RadioGeddonReceiverEventToggleRecord, instance->context);
            consumed = true;
            break;
        default:
            break;
        }
    } else if(event->type == InputTypeLong && event->key == InputKeyOk) {
        if(instance->callback) instance->callback(RadioGeddonReceiverEventMore, instance->context);
        consumed = true;
    }
    return consumed;
}

RadioGeddonReceiverView* radiogeddon_receiver_view_alloc(void) {
    RadioGeddonReceiverView* instance = malloc(sizeof(RadioGeddonReceiverView));
    instance->view = view_alloc();
    instance->callback = NULL;
    instance->context = NULL;
    view_set_context(instance->view, instance);
    view_allocate_model(instance->view, ViewModelTypeLocking, sizeof(RadioGeddonReceiverModel));
    view_set_draw_callback(instance->view, radiogeddon_receiver_view_draw);
    view_set_input_callback(instance->view, radiogeddon_receiver_view_input);

    with_view_model(
        instance->view,
        RadioGeddonReceiverModel * m,
        {
            m->frequency = 433920000;
            strncpy(m->preset_label, "AM650", sizeof(m->preset_label) - 1);
            m->rssi = RSSI_FLOOR;
            m->recording = false;
            m->samples = 0;
            m->elapsed_ms = 0;
            m->lost = 0;
            m->fill_pct = 0;
            m->history_count = 0;
            m->selected = 0;
            m->latest[0] = '\0';
            m->hopping = false;
            m->threshold = -127.0f;
            m->status[0] = '\0';
        },
        true);

    return instance;
}

void radiogeddon_receiver_view_free(RadioGeddonReceiverView* instance) {
    furi_assert(instance);
    view_free(instance->view);
    free(instance);
}

View* radiogeddon_receiver_view_get_view(RadioGeddonReceiverView* instance) {
    return instance->view;
}

void radiogeddon_receiver_view_set_callback(
    RadioGeddonReceiverView* instance,
    RadioGeddonReceiverCallback callback,
    void* context) {
    instance->callback = callback;
    instance->context = context;
}

void radiogeddon_receiver_view_set_config(
    RadioGeddonReceiverView* instance,
    uint32_t frequency,
    const char* preset_label) {
    with_view_model(
        instance->view,
        RadioGeddonReceiverModel * m,
        {
            m->frequency = frequency;
            strncpy(m->preset_label, preset_label, sizeof(m->preset_label) - 1);
            m->preset_label[sizeof(m->preset_label) - 1] = '\0';
        },
        true);
}

void radiogeddon_receiver_view_set_rssi(RadioGeddonReceiverView* instance, float rssi) {
    with_view_model(instance->view, RadioGeddonReceiverModel * m, { m->rssi = rssi; }, true);
}

void radiogeddon_receiver_view_set_hopping(RadioGeddonReceiverView* instance, bool hopping) {
    with_view_model(instance->view, RadioGeddonReceiverModel * m, { m->hopping = hopping; }, true);
}

void radiogeddon_receiver_view_set_recording(
    RadioGeddonReceiverView* instance,
    bool recording,
    const RadioGeddonRecordStats* stats) {
    with_view_model(
        instance->view,
        RadioGeddonReceiverModel * m,
        {
            m->recording = recording;
            m->samples = stats ? stats->samples : 0;
            m->elapsed_ms = stats ? stats->elapsed_ms : 0;
            m->lost = stats ? stats->lost : 0;
            m->fill_pct = stats ? stats->fill_pct : 0;
        },
        true);
}

void radiogeddon_receiver_view_set_history(
    RadioGeddonReceiverView* instance,
    size_t count,
    const char* latest_label) {
    with_view_model(
        instance->view,
        RadioGeddonReceiverModel * m,
        {
            bool grew = count > m->history_count;
            m->history_count = count;
            if(latest_label) {
                strncpy(m->latest, latest_label, sizeof(m->latest) - 1);
                m->latest[sizeof(m->latest) - 1] = '\0';
            }
            // Auto-follow the newest decode.
            if(grew && count > 0) m->selected = count - 1;
            if(m->selected >= count && count > 0) m->selected = count - 1;
        },
        true);
}

size_t radiogeddon_receiver_view_get_selected(RadioGeddonReceiverView* instance) {
    size_t sel = 0;
    with_view_model(instance->view, RadioGeddonReceiverModel * m, { sel = m->selected; }, false);
    return sel;
}

void radiogeddon_receiver_view_set_threshold(RadioGeddonReceiverView* instance, float dbm) {
    with_view_model(instance->view, RadioGeddonReceiverModel * m, { m->threshold = dbm; }, true);
}

void radiogeddon_receiver_view_set_status(RadioGeddonReceiverView* instance, const char* text) {
    with_view_model(
        instance->view,
        RadioGeddonReceiverModel * m,
        {
            strncpy(m->status, text, sizeof(m->status) - 1);
            m->status[sizeof(m->status) - 1] = '\0';
        },
        true);
}
