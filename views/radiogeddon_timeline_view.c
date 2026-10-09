#include "radiogeddon_timeline_view.h"
#include "../helpers/rg_timeline.h"

#include <gui/elements.h>
#include <furi.h>

#define TL_WIDTH     128u
#define TL_HIGH_Y    15
#define TL_LOW_Y     37
#define TL_BAR_Y     42
/* Pulses at least this many columns wide get their duration printed. */
#define TL_LABEL_PX  22u
#define TL_MAX_LABEL 8u

struct RadioGeddonTimelineView {
    View* view;
    RadioGeddonTimelineCallback callback;
    void* context;
};

typedef struct {
    int32_t samples[RADIOGEDDON_TIMELINE_WINDOW];
    size_t count;
    uint32_t first_index;
    uint64_t first_us;
    uint64_t end_us;
    bool at_end;
    bool loading;

    uint64_t total_us;
    uint32_t total_samples;
    uint64_t frames[RADIOGEDDON_TIMELINE_MAX_FRAMES];
    size_t frame_count;

    uint64_t left_us;
    int zoom;
    /* Position the current window was loaded for. A window can be shorter
     * than the screen at coarse zoom, so only a new position reloads. */
    uint64_t window_left;
    int window_zoom;
} RadioGeddonTimelineModel;

static uint64_t tl_span(const RadioGeddonTimelineModel* m) {
    return (uint64_t)rg_timeline_zoom_us[m->zoom] * TL_WIDTH;
}

static bool tl_need_reload(const RadioGeddonTimelineModel* m) {
    if(m->left_us == m->window_left && m->zoom == m->window_zoom) return false;
    return !rg_timeline_covered(m->first_us, m->end_us, m->at_end, m->left_us, tl_span(m));
}

static void tl_cat_time(char* buf, size_t len, uint64_t us) {
    snprintf(
        buf,
        len,
        "%lu.%04lus",
        (unsigned long)(us / 1000000u),
        (unsigned long)((us % 1000000u) / 100u));
}

/* Sample index (in the file) of the first sample visible at the left edge. */
static uint32_t tl_left_index(const RadioGeddonTimelineModel* m) {
    uint64_t t = m->first_us;
    for(size_t i = 0; i < m->count; i++) {
        int32_t v = m->samples[i];
        t += (uint32_t)(v < 0 ? -(int64_t)v : v);
        if(t > m->left_us) return m->first_index + (uint32_t)i;
    }
    return m->first_index + (uint32_t)m->count;
}

static void radiogeddon_timeline_view_draw(Canvas* canvas, void* model) {
    RadioGeddonTimelineModel* m = model;
    uint32_t upp = rg_timeline_zoom_us[m->zoom];
    uint64_t span = tl_span(m);
    char buf[32];

    canvas_clear(canvas);
    canvas_set_font(canvas, FontSecondary);
    tl_cat_time(buf, sizeof(buf), m->left_us);
    canvas_draw_str(canvas, 0, 8, buf);
    if(upp >= 1000)
        snprintf(buf, sizeof(buf), "%lums/px", (unsigned long)(upp / 1000));
    else
        snprintf(buf, sizeof(buf), "%luus/px", (unsigned long)upp);
    canvas_draw_str_aligned(canvas, 127, 8, AlignRight, AlignBottom, buf);

    if(m->loading) {
        canvas_draw_str_aligned(canvas, 64, 28, AlignCenter, AlignCenter, "Loading...");
    } else {
        // Waveform: one column per us_per_px; both levels in a column = edge.
        uint8_t cols[TL_WIDTH];
        rg_timeline_raster(m->samples, m->count, m->first_us, m->left_us, upp, cols, TL_WIDTH);
        uint8_t prev = 0;
        for(int32_t x = 0; x < (int32_t)TL_WIDTH; x++) {
            uint8_t c = cols[x];
            if(c == (RG_TL_HIGH | RG_TL_LOW) ||
               (c && prev && c != prev && prev != (RG_TL_HIGH | RG_TL_LOW))) {
                canvas_draw_line(canvas, x, TL_HIGH_Y, x, TL_LOW_Y);
            } else if(c == RG_TL_HIGH) {
                canvas_draw_dot(canvas, x, TL_HIGH_Y);
            } else if(c == RG_TL_LOW) {
                canvas_draw_dot(canvas, x, TL_LOW_Y);
            }
            prev = c;
        }

        // Recording end, dotted.
        if(m->at_end && m->total_us >= m->left_us && m->total_us < m->left_us + span) {
            int32_t x = (int32_t)((m->total_us - m->left_us) / upp);
            for(int32_t y = TL_HIGH_Y; y <= TL_LOW_Y; y += 3)
                canvas_draw_dot(canvas, x, y);
        }

        // Durations of wide pulses, inside the pulse.
        RgTlLabel labels[TL_MAX_LABEL];
        size_t nl = rg_timeline_labels(
            m->samples,
            m->count,
            m->first_us,
            m->left_us,
            upp,
            TL_WIDTH,
            TL_LABEL_PX,
            labels,
            TL_MAX_LABEL);
        canvas_set_font(canvas, FontKeyboard);
        for(size_t i = 0; i < nl; i++) {
            snprintf(buf, sizeof(buf), "%lu", (unsigned long)labels[i].us);
            canvas_draw_str_aligned(
                canvas,
                labels[i].x,
                labels[i].high ? TL_HIGH_Y + 10 : TL_LOW_Y - 3,
                AlignCenter,
                AlignBottom,
                buf);
        }
        canvas_set_font(canvas, FontSecondary);
    }

    // Frame starts: small triangles above the waveform.
    for(size_t i = 0; i < m->frame_count; i++) {
        if(m->frames[i] < m->left_us || m->frames[i] >= m->left_us + span) continue;
        int32_t x = (int32_t)((m->frames[i] - m->left_us) / upp);
        canvas_draw_line(canvas, x - 2, 10, x + 2, 10);
        canvas_draw_line(canvas, x - 1, 11, x + 1, 11);
        canvas_draw_dot(canvas, x, 12);
    }

    // Overview: where the screen sits in the whole recording.
    canvas_draw_line(canvas, 0, TL_BAR_Y + 1, 127, TL_BAR_Y + 1);
    if(m->total_us > 0) {
        uint32_t bx = (uint32_t)((m->left_us * TL_WIDTH) / m->total_us);
        uint32_t bw = (uint32_t)((span * TL_WIDTH) / m->total_us);
        if(bw < 2) bw = 2;
        if(bx > TL_WIDTH - 2) bx = TL_WIDTH - 2;
        if(bx + bw > TL_WIDTH) bw = TL_WIDTH - bx;
        canvas_draw_box(canvas, (int32_t)bx, TL_BAR_Y, bw, 3);
    }

    // Position: current frame and sample index at the left edge.
    size_t f = rg_timeline_frame_at(m->frames, m->frame_count, m->left_us, span);
    if(f < m->frame_count)
        snprintf(buf, sizeof(buf), "Fr %u/%u", (unsigned)(f + 1), (unsigned)m->frame_count);
    else
        snprintf(buf, sizeof(buf), "Fr -/%u", (unsigned)m->frame_count);
    canvas_draw_str(canvas, 0, 54, buf);
    snprintf(
        buf,
        sizeof(buf),
        "#%lu/%lu",
        (unsigned long)tl_left_index(m),
        (unsigned long)m->total_samples);
    canvas_draw_str_aligned(canvas, 127, 54, AlignRight, AlignBottom, buf);
    canvas_draw_str_aligned(canvas, 64, 63, AlignCenter, AlignBottom, "^v zoom  <> pan  OK frame");
}

static bool radiogeddon_timeline_view_input(InputEvent* event, void* context) {
    RadioGeddonTimelineView* instance = context;
    if(event->key == InputKeyBack) return false;
    bool repeatable = event->type == InputTypeShort || event->type == InputTypeRepeat;

    RadioGeddonTimelineModel* m = view_get_model(instance->view);
    uint32_t upp = rg_timeline_zoom_us[m->zoom];
    uint64_t new_left = m->left_us;
    switch(event->key) {
    case InputKeyLeft:
        if(repeatable) m->left_us = rg_timeline_pan(m->left_us, -1, upp, TL_WIDTH, m->total_us);
        break;
    case InputKeyRight:
        if(repeatable) m->left_us = rg_timeline_pan(m->left_us, +1, upp, TL_WIDTH, m->total_us);
        break;
    case InputKeyUp:
        if(event->type == InputTypeShort)
            rg_timeline_zoom(&m->zoom, -1, &m->left_us, TL_WIDTH, m->total_us);
        break;
    case InputKeyDown:
        if(event->type == InputTypeShort)
            rg_timeline_zoom(&m->zoom, +1, &m->left_us, TL_WIDTH, m->total_us);
        break;
    case InputKeyOk:
        if(event->type == InputTypeShort || event->type == InputTypeLong) {
            int dir = event->type == InputTypeShort ? +1 : -1;
            if(rg_timeline_frame_step(
                   m->frames, m->frame_count, m->left_us, tl_span(m), dir, &new_left) <
               m->frame_count)
                m->left_us = rg_timeline_clamp(new_left, tl_span(m), m->total_us);
        }
        break;
    default:
        break;
    }
    bool reload = false;
    if(!m->loading && tl_need_reload(m)) {
        m->loading = true;
        reload = true;
    }
    view_commit_model(instance->view, true);

    if(reload && instance->callback)
        instance->callback(RadioGeddonTimelineEventReload, instance->context);
    return true;
}

size_t radiogeddon_timeline_view_heap_size(void) {
    // The View object itself is small; 256 bytes covers it and the allocator.
    return sizeof(RadioGeddonTimelineView) + sizeof(RadioGeddonTimelineModel) + 256u;
}

RadioGeddonTimelineView* radiogeddon_timeline_view_alloc(void) {
    RadioGeddonTimelineView* instance = malloc(sizeof(RadioGeddonTimelineView));
    instance->view = view_alloc();
    instance->callback = NULL;
    instance->context = NULL;
    view_set_context(instance->view, instance);
    view_allocate_model(instance->view, ViewModelTypeLocking, sizeof(RadioGeddonTimelineModel));
    view_set_draw_callback(instance->view, radiogeddon_timeline_view_draw);
    view_set_input_callback(instance->view, radiogeddon_timeline_view_input);
    RadioGeddonTimelineModel* m = view_get_model(instance->view);
    memset(m, 0, sizeof(RadioGeddonTimelineModel));
    m->zoom = 3;
    m->window_zoom = -1;
    m->loading = true;
    view_commit_model(instance->view, false);
    return instance;
}

void radiogeddon_timeline_view_free(RadioGeddonTimelineView* instance) {
    furi_assert(instance);
    view_free(instance->view);
    free(instance);
}

View* radiogeddon_timeline_view_get_view(RadioGeddonTimelineView* instance) {
    return instance->view;
}

void radiogeddon_timeline_view_set_callback(
    RadioGeddonTimelineView* instance,
    RadioGeddonTimelineCallback callback,
    void* context) {
    instance->callback = callback;
    instance->context = context;
}

void radiogeddon_timeline_view_set_recording(
    RadioGeddonTimelineView* instance,
    uint64_t total_us,
    uint32_t total_samples,
    const uint64_t* frame_starts,
    size_t frame_count,
    int zoom,
    uint64_t left_us) {
    RadioGeddonTimelineModel* m = view_get_model(instance->view);
    if(frame_count > RADIOGEDDON_TIMELINE_MAX_FRAMES)
        frame_count = RADIOGEDDON_TIMELINE_MAX_FRAMES;
    m->total_us = total_us;
    m->total_samples = total_samples;
    memcpy(m->frames, frame_starts, frame_count * sizeof(uint64_t));
    m->frame_count = frame_count;
    m->zoom = (zoom >= 0 && zoom < RG_TL_ZOOM_COUNT) ? zoom : 3;
    m->left_us = rg_timeline_clamp(left_us, tl_span(m), total_us);
    view_commit_model(instance->view, true);
}

void radiogeddon_timeline_view_get_span(
    RadioGeddonTimelineView* instance,
    uint64_t* left_us,
    uint64_t* span_us) {
    RadioGeddonTimelineModel* m = view_get_model(instance->view);
    *left_us = m->left_us;
    *span_us = tl_span(m);
    view_commit_model(instance->view, false);
}

void radiogeddon_timeline_view_window_begin(
    RadioGeddonTimelineView* instance,
    uint32_t first_index,
    uint64_t first_us) {
    RadioGeddonTimelineModel* m = view_get_model(instance->view);
    m->loading = true;
    m->count = 0;
    m->first_index = first_index;
    m->first_us = first_us;
    m->end_us = first_us;
    m->at_end = false;
    m->window_left = m->left_us;
    m->window_zoom = m->zoom;
    view_commit_model(instance->view, true);
}

void radiogeddon_timeline_view_window_append(
    RadioGeddonTimelineView* instance,
    const int32_t* samples,
    size_t count) {
    RadioGeddonTimelineModel* m = view_get_model(instance->view);
    for(size_t i = 0; i < count && m->count < RADIOGEDDON_TIMELINE_WINDOW; i++) {
        int32_t v = samples[i];
        m->samples[m->count++] = v;
        m->end_us += (uint32_t)(v < 0 ? -(int64_t)v : v);
    }
    view_commit_model(instance->view, false);
}

void radiogeddon_timeline_view_window_end(RadioGeddonTimelineView* instance, bool at_end) {
    RadioGeddonTimelineModel* m = view_get_model(instance->view);
    m->at_end = at_end;
    // Input that arrived while loading may already need another window.
    bool reload = tl_need_reload(m);
    m->loading = reload;
    view_commit_model(instance->view, true);
    if(reload && instance->callback)
        instance->callback(RadioGeddonTimelineEventReload, instance->context);
}
