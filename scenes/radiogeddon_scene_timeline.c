#include "radiogeddon_scene.h"
#include "../helpers/rg_timeline.h"

/*
 * Pulse Timeline: a zoomable, scrollable waveform of a RAW capture. On entry
 * the file is analysed once (frame starts, Te, and the reader's seek
 * checkpoints); afterwards only a window of samples around the visible span
 * is read, so the recording is never loaded whole.
 */

#define TIMELINE_CHUNK      32u
/* Heap kept free beyond the view's model when deciding whether to open. */
#define TIMELINE_HEAP_SPARE (4u * 1024u)

typedef enum {
    TimelineEventReload = 700,
} TimelineEvent;

static void radiogeddon_scene_timeline_view_cb(RadioGeddonTimelineEvent event, void* context) {
    RadioGeddonApp* app = context;
    if(event == RadioGeddonTimelineEventReload) {
        view_dispatcher_send_custom_event(app->view_dispatcher, TimelineEventReload);
    }
}

/* Stream a window that starts a quarter screen before the visible span. */
static void radiogeddon_scene_timeline_load(RadioGeddonApp* app) {
    RadioGeddonTimelineView* view = app->timeline_view;
    RgRawReader* reader = &app->timeline_file->reader;
    uint64_t left = 0, span = 0;
    radiogeddon_timeline_view_get_span(view, &left, &span);
    uint64_t target = left > span / 4 ? left - span / 4 : 0;

    rg_raw_reader_seek_time(reader, target);
    uint64_t t = reader->time_us;
    uint32_t index = reader->index;
    bool started = false;
    size_t loaded = 0;
    int32_t chunk[TIMELINE_CHUNK];
    while(loaded < RADIOGEDDON_TIMELINE_WINDOW) {
        size_t n = rg_raw_reader_read(reader, chunk, TIMELINE_CHUNK);
        if(n == 0) break;
        size_t i = 0;
        if(!started) {
            // Skip samples that end before the target.
            while(i < n) {
                uint32_t d = (uint32_t)(chunk[i] < 0 ? -(int64_t)chunk[i] : chunk[i]);
                if(t + d > target) break;
                t += d;
                index++;
                i++;
            }
            if(i == n) continue;
            started = true;
            radiogeddon_timeline_view_window_begin(view, index, t);
        }
        size_t take = n - i;
        if(take > RADIOGEDDON_TIMELINE_WINDOW - loaded)
            take = RADIOGEDDON_TIMELINE_WINDOW - loaded;
        radiogeddon_timeline_view_window_append(view, chunk + i, take);
        loaded += take;
    }
    if(!started) radiogeddon_timeline_view_window_begin(view, index, t);
    radiogeddon_timeline_view_window_end(
        view, reader->eof && loaded < RADIOGEDDON_TIMELINE_WINDOW);
}

static void radiogeddon_scene_timeline_show_text(RadioGeddonApp* app) {
    text_box_reset(app->text_box);
    text_box_set_font(app->text_box, TextBoxFontText);
    text_box_set_text(app->text_box, furi_string_get_cstr(app->temp_str));
    view_dispatcher_switch_to_view(app->view_dispatcher, RadioGeddonViewTextBox);
}

void radiogeddon_scene_timeline_on_enter(void* context) {
    RadioGeddonApp* app = context;
    furi_string_reset(app->temp_str);
    radiogeddon_scene_show_progress(app, "Indexing...");

    app->timeline_file =
        radiogeddon_storage_raw_open(app->storage, furi_string_get_cstr(app->file_path));
    if(!app->timeline_file) {
        radiogeddon_scene_progress_end(app);
        radiogeddon_analysis_cat_status(app->temp_str, RadioGeddonAnalysisOpenFailed);
        radiogeddon_scene_timeline_show_text(app);
        return;
    }

    // One analysis pass set: frame starts and Te for navigation, and the
    // reader's checkpoints for seeking. The analyzer is freed before the view
    // is allocated, so the two are never in memory together.
    RadioGeddonAnalysisStatus status;
    RgAnalyzer* a = radiogeddon_analysis_run_raw(app->timeline_file, &status);
    radiogeddon_scene_progress_end(app);
    if(!a) {
        radiogeddon_analysis_cat_status(app->temp_str, status);
        radiogeddon_scene_timeline_show_text(app);
        return;
    }
    const RgAnalysis* r = &a->result;
    uint64_t starts[RADIOGEDDON_TIMELINE_MAX_FRAMES];
    size_t frames = 0;
    // Navigate the decodable frames; fall back to every frame when none decode.
    for(int pass = 0; pass < 2 && frames == 0; pass++) {
        for(size_t i = 0; i < r->frames_kept && frames < RADIOGEDDON_TIMELINE_MAX_FRAMES; i++) {
            if(pass == 0 && r->frames[i].fit < RG_ANALYZER_GOOD_FIT) continue;
            starts[frames++] = r->frames[i].start_us;
        }
    }
    uint32_t te = r->te_us;
    uint64_t total_us = app->timeline_file->reader.time_us;
    uint32_t total_samples = app->timeline_file->reader.index;
    free(a);

    if(memmgr_heap_get_max_free_block() <
       radiogeddon_timeline_view_heap_size() + TIMELINE_HEAP_SPARE) {
        radiogeddon_analysis_cat_status(app->temp_str, RadioGeddonAnalysisNoMemory);
        radiogeddon_scene_timeline_show_text(app);
        return;
    }
    app->timeline_view = radiogeddon_timeline_view_alloc();
    radiogeddon_timeline_view_set_callback(
        app->timeline_view, radiogeddon_scene_timeline_view_cb, app);
    view_dispatcher_add_view(
        app->view_dispatcher,
        RadioGeddonViewTimeline,
        radiogeddon_timeline_view_get_view(app->timeline_view));

    // Start zoomed so about ten PWM bits (40 Te) fill the screen, with the
    // first frame on the frame marker.
    int zoom = rg_timeline_zoom_for(te ? (uint64_t)te * 40u : 6400u, 128);
    uint64_t span = (uint64_t)rg_timeline_zoom_us[zoom] * 128u;
    uint64_t left = 0;
    if(frames > 0) left = starts[0] > span / 8 ? starts[0] - span / 8 : 0;
    radiogeddon_timeline_view_set_recording(
        app->timeline_view, total_us, total_samples, starts, frames, zoom, left);
    radiogeddon_scene_timeline_load(app);
    view_dispatcher_switch_to_view(app->view_dispatcher, RadioGeddonViewTimeline);
}

bool radiogeddon_scene_timeline_on_event(void* context, SceneManagerEvent event) {
    RadioGeddonApp* app = context;
    if(event.type == SceneManagerEventTypeCustom && event.event == TimelineEventReload) {
        if(app->timeline_view && app->timeline_file) radiogeddon_scene_timeline_load(app);
        return true;
    }
    return false;
}

void radiogeddon_scene_timeline_on_exit(void* context) {
    RadioGeddonApp* app = context;
    if(app->timeline_view) {
        view_dispatcher_remove_view(app->view_dispatcher, RadioGeddonViewTimeline);
        radiogeddon_timeline_view_free(app->timeline_view);
        app->timeline_view = NULL;
    }
    radiogeddon_storage_raw_close(app->timeline_file);
    app->timeline_file = NULL;
    text_box_reset(app->text_box);
}
