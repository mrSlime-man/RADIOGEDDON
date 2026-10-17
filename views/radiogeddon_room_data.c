/*
 * Dolphin Lab room (Full edition, in its module): what the room reads from
 * the SD card. Only on a key press or on entry, never while drawing.
 */
#include "radiogeddon_room_view.h"
#include "../radiogeddon_edition.h" /* RG_UI_TEXT */

#if RG_FEATURE_DOLPHIN_LAB

#include "../radiogeddon_version.h"
#include "../helpers/radiogeddon_storage.h"
#include "../helpers/rg_raw.h"
#include "../helpers/rg_ui.h"
#include <toolbox/stream/file_stream.h>

/* The oscilloscope's samples: the first ones of the file. */
#define ROOM_SAMPLES 512u

RG_UI_TEXT void radiogeddon_room_load_terminal(
    RadioGeddonRoomView* instance,
    const RgJournal* j,
    bool xp_on,
    const char* fw) {
    // The journal's counts: nothing is read from the card for them.
    char lines[RADIOGEDDON_ROOM_LINES][RADIOGEDDON_ROOM_LINE_LEN];
    uint32_t xp = rg_journal_xp(j);
    snprintf(lines[0], sizeof(lines[0]), "RadioGeddon %s", RADIOGEDDON_VERSION);
    snprintf(lines[1], sizeof(lines[1]), "%s %s", rg_ui_fw_family(fw), fw ? fw : "");
    if(xp_on) {
        snprintf(
            lines[2],
            sizeof(lines[2]),
            "Level %u, %lu XP",
            (unsigned)rg_journal_level(xp),
            (unsigned long)xp);
    } else {
        snprintf(
            lines[2],
            sizeof(lines[2]),
            "Studio %lu, similar %lu",
            (unsigned long)j->counters[RgJrnStudio],
            (unsigned long)j->counters[RgJrnSimilar]);
    }
    snprintf(
        lines[3],
        sizeof(lines[3]),
        "Saved %lu, analysed %lu",
        (unsigned long)j->counters[RgJrnCapture],
        (unsigned long)j->counters[RgJrnAnalysis] + (unsigned long)j->counters[RgJrnDiscovery]);
    uint32_t badges = 0;
    for(size_t a = 0; a < RgAchCount; a++)
        if(j->achievements & (1u << a)) badges++;
    if(xp_on) {
        snprintf(
            lines[4],
            sizeof(lines[4]),
            "Badges %lu/%u, logs %lu",
            (unsigned long)badges,
            (unsigned)RgAchCount,
            (unsigned long)j->counters[RgJrnActlog]);
    } else {
        snprintf(
            lines[4],
            sizeof(lines[4]),
            "Reports %lu, logs %lu",
            (unsigned long)j->counters[RgJrnReport],
            (unsigned long)j->counters[RgJrnActlog]);
    }
    radiogeddon_room_view_set_terminal(
        instance, (const char(*)[RADIOGEDDON_ROOM_LINE_LEN])lines, j->counters[RgJrnCapture]);
}

static RG_UI_TEXT size_t room_stream_read(void* context, uint8_t* buf, size_t size) {
    return stream_read(context, buf, size);
}

static RG_UI_TEXT bool room_stream_seek(void* context, uint32_t offset) {
    return stream_seek(context, (int32_t)offset, StreamOffsetFromStart);
}

RG_UI_TEXT void
    radiogeddon_room_load_scope(RadioGeddonRoomView* instance, Storage* storage, const char* path) {
    RadioGeddonRoomTrace trace;
    memset(&trace, 0, sizeof(trace));
    trace.loaded = true;
    const char* slash = strrchr(path, '/');
    strlcpy(trace.name, slash ? slash + 1 : path, sizeof(trace.name));

    Stream* stream = file_stream_alloc(storage);
    RgRawReader* reader = malloc(sizeof(RgRawReader));
    int32_t* samples = malloc(ROOM_SAMPLES * sizeof(int32_t));
    size_t n = 0;
    if(file_stream_open(stream, path, FSAM_READ, FSOM_OPEN_EXISTING)) {
        RgRawSource src = {room_stream_read, room_stream_seek, stream};
        rg_raw_reader_init(reader, src);
        n = rg_raw_reader_read(reader, samples, ROOM_SAMPLES);
    }
    file_stream_close(stream);
    stream_free(stream);
    uint64_t span = rg_ui_trace(samples, n, RADIOGEDDON_ROOM_SCOPE_W, trace.bits);
    trace.empty = span == 0;
    trace.span_us = span > UINT32_MAX ? UINT32_MAX : (uint32_t)span;
    free(samples);
    free(reader);
    radiogeddon_room_view_set_scope(instance, &trace);
}

#endif
