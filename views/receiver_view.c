#include "receiver_view.h"

#include <gui/elements.h>
#include <furi.h>

typedef struct {
    RgReceiverMode mode;
    bool radio_ok;
    uint32_t frequency;
    float rssi;
    uint32_t detected_count;
    uint32_t samples;
    char last_proto[28];
    bool last_rolling;
    char status[28];
} RgReceiverModel;

struct RgReceiverView {
    View* view;
    RgReceiverViewOkCallback ok_callback;
    void* ok_context;
};

static const char* rg_receiver_mode_title(RgReceiverMode mode) {
    switch(mode) {
    case RgReceiverModeHopper:
        return "Frequency Hopper";
    case RgReceiverModeRecord:
        return "RAW Record";
    case RgReceiverModeReceiver:
    default:
        return "Receiver";
    }
}

/* Map RSSI dBm (roughly -90..-30) to a 0..1 bar fill. */
static float rg_receiver_rssi_to_bar(float rssi) {
    float norm = (rssi + 90.0f) / 60.0f;
    if(norm < 0.0f) norm = 0.0f;
    if(norm > 1.0f) norm = 1.0f;
    return norm;
}

static void rg_receiver_view_draw(Canvas* canvas, void* model) {
    RgReceiverModel* m = model;
    canvas_clear(canvas);

    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, 2, 10, rg_receiver_mode_title(m->mode));

    canvas_set_font(canvas, FontSecondary);

    if(!m->radio_ok) {
        canvas_draw_str_aligned(canvas, 64, 32, AlignCenter, AlignCenter, "Radio not available");
        canvas_draw_str_aligned(canvas, 64, 44, AlignCenter, AlignCenter, "Press Back to exit");
        return;
    }

    /* Frequency */
    char freq_str[24];
    snprintf(
        freq_str,
        sizeof(freq_str),
        "%lu.%02lu MHz",
        m->frequency / 1000000UL,
        (m->frequency % 1000000UL) / 10000UL);
    canvas_draw_str(canvas, 2, 24, freq_str);

    /* RSSI bar */
    canvas_draw_str(canvas, 2, 36, "RSSI");
    elements_progress_bar(canvas, 26, 29, 86, rg_receiver_rssi_to_bar(m->rssi));
    char rssi_str[16];
    snprintf(rssi_str, sizeof(rssi_str), "%d dBm", (int)m->rssi);
    canvas_draw_str(canvas, 2, 47, rssi_str);

    /* Mode-specific line */
    char line[32];
    if(m->mode == RgReceiverModeRecord) {
        snprintf(line, sizeof(line), "Samples: %lu", m->samples);
        canvas_draw_str(canvas, 60, 47, line);
    } else {
        snprintf(line, sizeof(line), "Found: %lu", m->detected_count);
        canvas_draw_str(canvas, 60, 47, line);
    }

    /* Last decoded protocol */
    if(m->last_proto[0] != '\0') {
        char proto_line[40];
        snprintf(
            proto_line,
            sizeof(proto_line),
            "%s%s",
            m->last_proto,
            m->last_rolling ? " (rolling)" : "");
        canvas_draw_str(canvas, 2, 58, proto_line);
    } else if(m->status[0] != '\0') {
        canvas_draw_str(canvas, 2, 58, m->status);
    }

    if(m->mode == RgReceiverModeRecord) {
        elements_button_center(canvas, "Stop");
    }
}

static bool rg_receiver_view_input(InputEvent* event, void* context) {
    RgReceiverView* v = context;
    if(event->type == InputTypeShort && event->key == InputKeyOk) {
        if(v->ok_callback) {
            v->ok_callback(v->ok_context);
            return true;
        }
    }
    return false; /* let Back propagate to the scene manager */
}

RgReceiverView* rg_receiver_view_alloc(void) {
    RgReceiverView* v = malloc(sizeof(RgReceiverView));
    v->ok_callback = NULL;
    v->ok_context = NULL;
    v->view = view_alloc();
    view_set_context(v->view, v);
    view_allocate_model(v->view, ViewModelTypeLocking, sizeof(RgReceiverModel));
    view_set_draw_callback(v->view, rg_receiver_view_draw);
    view_set_input_callback(v->view, rg_receiver_view_input);

    with_view_model(
        v->view,
        RgReceiverModel * model,
        {
            model->mode = RgReceiverModeReceiver;
            model->radio_ok = true;
            model->frequency = 433920000;
            model->rssi = -90.0f;
            model->detected_count = 0;
            model->samples = 0;
            model->last_proto[0] = '\0';
            model->last_rolling = false;
            model->status[0] = '\0';
        },
        true);
    return v;
}

void rg_receiver_view_free(RgReceiverView* v) {
    furi_check(v);
    view_free(v->view);
    free(v);
}

View* rg_receiver_view_get_view(RgReceiverView* v) {
    furi_check(v);
    return v->view;
}

void rg_receiver_view_set_mode(RgReceiverView* v, RgReceiverMode mode) {
    with_view_model(v->view, RgReceiverModel * model, { model->mode = mode; }, true);
}

void rg_receiver_view_set_radio_ok(RgReceiverView* v, bool ok) {
    with_view_model(v->view, RgReceiverModel * model, { model->radio_ok = ok; }, true);
}

void rg_receiver_view_set_frequency(RgReceiverView* v, uint32_t frequency) {
    with_view_model(v->view, RgReceiverModel * model, { model->frequency = frequency; }, true);
}

void rg_receiver_view_set_rssi(RgReceiverView* v, float rssi) {
    with_view_model(v->view, RgReceiverModel * model, { model->rssi = rssi; }, true);
}

void rg_receiver_view_set_status(RgReceiverView* v, const char* status) {
    with_view_model(
        v->view,
        RgReceiverModel * model,
        { strncpy(model->status, status ? status : "", sizeof(model->status) - 1); },
        true);
}

void rg_receiver_view_set_samples(RgReceiverView* v, uint32_t samples) {
    with_view_model(v->view, RgReceiverModel * model, { model->samples = samples; }, true);
}

void rg_receiver_view_set_detections(RgReceiverView* v, uint32_t count) {
    with_view_model(v->view, RgReceiverModel * model, { model->detected_count = count; }, true);
}

void rg_receiver_view_set_last_protocol(RgReceiverView* v, const char* proto, bool rolling) {
    with_view_model(
        v->view,
        RgReceiverModel * model,
        {
            strncpy(model->last_proto, proto ? proto : "", sizeof(model->last_proto) - 1);
            model->last_proto[sizeof(model->last_proto) - 1] = '\0';
            model->last_rolling = rolling;
        },
        true);
}

void rg_receiver_view_reset(RgReceiverView* v) {
    with_view_model(
        v->view,
        RgReceiverModel * model,
        {
            model->detected_count = 0;
            model->samples = 0;
            model->last_proto[0] = '\0';
            model->last_rolling = false;
            model->status[0] = '\0';
        },
        true);
}

void rg_receiver_view_set_ok_callback(
    RgReceiverView* v,
    RgReceiverViewOkCallback callback,
    void* context) {
    furi_check(v);
    v->ok_callback = callback;
    v->ok_context = context;
}
