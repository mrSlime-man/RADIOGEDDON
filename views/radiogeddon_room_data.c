/*
 * Dolphin Lab room (Full edition, in its module): what the room reads from
 * the SD card. Only on a key press or on entry, never while drawing.
 */
#include "radiogeddon_room_view.h"

#if RG_FEATURE_DOLPHIN_LAB

#include "../radiogeddon_version.h"
#include "../helpers/radiogeddon_storage.h"
#include "../helpers/radiogeddon_sessions.h"
#include "../helpers/rg_raw.h"
#include "../helpers/rg_ui.h"
#include <toolbox/stream/file_stream.h>

/* The oscilloscope's samples: the first ones of the file. */
#define ROOM_SAMPLES 512u

/* Files in @p folder whose names end in @p suffix. */
static uint32_t room_count(Storage* storage, const char* folder, const char* suffix) {
    uint32_t n = 0;
    File* dir = storage_file_alloc(storage);
    FileInfo info;
    char name[64];
    size_t sl = strlen(suffix);
    if(storage_dir_open(dir, folder)) {
        while(storage_dir_read(dir, &info, name, sizeof(name))) {
            if(info.flags & FSF_DIRECTORY) continue;
            size_t len = strlen(name);
            if(len > sl && strcmp(name + len - sl, suffix) == 0) n++;
        }
    }
    storage_dir_close(dir);
    storage_file_free(dir);
    return n;
}

void radiogeddon_room_load_terminal(
    RadioGeddonRoomView* instance,
    Storage* storage,
    const char* fw) {
    char lines[RADIOGEDDON_ROOM_LINES][RADIOGEDDON_ROOM_LINE_LEN];
    uint32_t recordings =
        room_count(storage, RADIOGEDDON_SIGNALS_FOLDER, RADIOGEDDON_SUB_EXTENSION);
    snprintf(lines[0], sizeof(lines[0]), "RadioGeddon %s", RADIOGEDDON_VERSION);
    snprintf(lines[1], sizeof(lines[1]), "%s %s", rg_ui_fw_family(fw), fw ? fw : "");
    snprintf(lines[2], sizeof(lines[2]), "Recordings: %lu", (unsigned long)recordings);
    snprintf(
        lines[3],
        sizeof(lines[3]),
        "Saved reports: %lu",
        (unsigned long)room_count(storage, RADIOGEDDON_REPORTS_FOLDER, ".txt"));
    snprintf(
        lines[4],
        sizeof(lines[4]),
        "Sessions: %lu",
        (unsigned long)room_count(storage, RADIOGEDDON_SESSIONS_FOLDER, ".txt"));
    radiogeddon_room_view_set_terminal(
        instance, (const char(*)[RADIOGEDDON_ROOM_LINE_LEN])lines, recordings);
}

static size_t room_stream_read(void* context, uint8_t* buf, size_t size) {
    return stream_read(context, buf, size);
}

static bool room_stream_seek(void* context, uint32_t offset) {
    return stream_seek(context, (int32_t)offset, StreamOffsetFromStart);
}

void radiogeddon_room_load_scope(RadioGeddonRoomView* instance, Storage* storage, const char* path) {
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
