/**
 * @file radiogeddon_timeline_view.h
 * @brief Zoomable, scrollable pulse timeline of a RAW recording.
 *
 * Draws the recording as a high/low waveform with frame-start markers, pulse
 * durations where there is room, and an overview bar. The view holds only a
 * window of RADIOGEDDON_TIMELINE_WINDOW samples; when panning or zooming
 * leaves that window it sets a loading state and asks its owner (through the
 * callback) to stream a new window from the file. Allocate it only while it
 * is on screen: its model is about 4.5 KB.
 *
 * Controls: Left/Right pan a quarter screen (hold to repeat), Up/Down zoom in
 * and out, OK jumps to the next frame and holding OK to the previous one.
 */
#pragma once

#include <gui/view.h>
#include "../helpers/rg_analyzer.h"

#define RADIOGEDDON_TIMELINE_WINDOW     1024u
#define RADIOGEDDON_TIMELINE_MAX_FRAMES RG_ANALYZER_MAX_FRAMES

typedef struct RadioGeddonTimelineView RadioGeddonTimelineView;

typedef enum {
    RadioGeddonTimelineEventReload, /* the visible span left the loaded window */
} RadioGeddonTimelineEvent;

typedef void (*RadioGeddonTimelineCallback)(RadioGeddonTimelineEvent event, void* context);

/** Heap the view needs (instance, View and model), for a check before alloc. */
size_t radiogeddon_timeline_view_heap_size(void);

RadioGeddonTimelineView* radiogeddon_timeline_view_alloc(void);
void radiogeddon_timeline_view_free(RadioGeddonTimelineView* instance);
View* radiogeddon_timeline_view_get_view(RadioGeddonTimelineView* instance);

void radiogeddon_timeline_view_set_callback(
    RadioGeddonTimelineView* instance,
    RadioGeddonTimelineCallback callback,
    void* context);

/** Recording length, frame starts (us) and the initial zoom step and position. */
void radiogeddon_timeline_view_set_recording(
    RadioGeddonTimelineView* instance,
    uint64_t total_us,
    uint32_t total_samples,
    const uint64_t* frame_starts,
    size_t frame_count,
    int zoom,
    uint64_t left_us);

/** Visible span: left edge and width in microseconds. */
void radiogeddon_timeline_view_get_span(
    RadioGeddonTimelineView* instance,
    uint64_t* left_us,
    uint64_t* span_us);

/**
 * Replace the sample window: begin, append the samples in order (in chunks),
 * then end. @p first_index / @p first_us locate the first sample in the file.
 */
void radiogeddon_timeline_view_window_begin(
    RadioGeddonTimelineView* instance,
    uint32_t first_index,
    uint64_t first_us);
void radiogeddon_timeline_view_window_append(
    RadioGeddonTimelineView* instance,
    const int32_t* samples,
    size_t count);
void radiogeddon_timeline_view_window_end(RadioGeddonTimelineView* instance, bool at_end);
