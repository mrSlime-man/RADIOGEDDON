#include "radiogeddon_storage.h"
#include "radiogeddon_dsp.h"

#include <lib/flipper_format/flipper_format.h>
#include <lib/flipper_format/flipper_format_i.h>
#include <lib/toolbox/stream/stream.h>
#include <lib/toolbox/stream/file_stream.h>
#include <datetime/datetime.h>
#include <furi_hal_rtc.h>
#include <stdlib.h>

void radiogeddon_storage_ensure_paths(Storage* storage) {
    storage_common_mkdir(storage, EXT_PATH("apps_data"));
    storage_common_mkdir(storage, RADIOGEDDON_APP_FOLDER);
    storage_common_mkdir(storage, RADIOGEDDON_SIGNALS_FOLDER);
    storage_common_mkdir(storage, RADIOGEDDON_SCANS_FOLDER);
}

void radiogeddon_storage_default_name(FuriString* out) {
    DateTime dt;
    furi_hal_rtc_get_datetime(&dt);
    furi_string_printf(
        out,
        "RG_%04u%02u%02u_%02u%02u%02u",
        dt.year,
        dt.month,
        dt.day,
        dt.hour,
        dt.minute,
        dt.second);
}

void radiogeddon_storage_make_path(FuriString* out, const char* name) {
    furi_string_printf(
        out, "%s/%s%s", RADIOGEDDON_SIGNALS_FOLDER, name, RADIOGEDDON_SUB_EXTENSION);
}

bool radiogeddon_storage_write_serialized(
    Storage* storage,
    const char* path,
    FuriString* serialized) {
    FlipperFormat* ff = flipper_format_file_alloc(storage);
    bool ok = false;
    do {
        if(!flipper_format_file_open_always(ff, path)) break;
        Stream* stream = flipper_format_get_raw_stream(ff);
        size_t len = furi_string_size(serialized);
        ok = (stream_write_cstring(stream, furi_string_get_cstr(serialized)) == len);
    } while(false);
    flipper_format_free(ff);
    return ok;
}

void radiogeddon_loaded_signal_init(RadioGeddonLoadedSignal* sig) {
    memset(sig, 0, sizeof(RadioGeddonLoadedSignal));
    sig->name = furi_string_alloc();
    sig->protocol = furi_string_alloc();
    sig->preset = furi_string_alloc();
}

void radiogeddon_loaded_signal_reset(RadioGeddonLoadedSignal* sig) {
    if(sig->name) furi_string_free(sig->name);
    if(sig->protocol) furi_string_free(sig->protocol);
    if(sig->preset) furi_string_free(sig->preset);
    memset(sig, 0, sizeof(RadioGeddonLoadedSignal));
}

bool radiogeddon_storage_load(Storage* storage, const char* path, RadioGeddonLoadedSignal* out) {
    FlipperFormat* ff = flipper_format_file_alloc(storage);
    FuriString* type = furi_string_alloc();
    FuriString* value = furi_string_alloc();
    uint32_t version = 0;
    bool ok = false;

    // Derive display name from the file stem.
    {
        const char* slash = strrchr(path, '/');
        const char* stem = slash ? slash + 1 : path;
        furi_string_set(out->name, stem);
        size_t dot = furi_string_search_str(out->name, RADIOGEDDON_SUB_EXTENSION, 0);
        if(dot != FURI_STRING_FAILURE) furi_string_left(out->name, dot);
    }

    do {
        if(!flipper_format_file_open_existing(ff, path)) break;
        if(!flipper_format_read_header(ff, type, &version)) break;

        if(flipper_format_read_uint32(ff, "Frequency", &out->frequency, 1)) {
            // optional
        }
        flipper_format_rewind(ff);
        if(flipper_format_read_string(ff, "Preset", value)) {
            furi_string_set(out->preset, value);
        }
        flipper_format_rewind(ff);
        if(!flipper_format_read_string(ff, "Protocol", value)) {
            furi_string_set(out->protocol, "Unknown");
            out->kind = RadioGeddonSignalKindUnknown;
            ok = true; // header parsed; treat as unknown
            break;
        }
        furi_string_set(out->protocol, value);

        if(furi_string_equal_str(value, "RAW")) {
            out->kind = RadioGeddonSignalKindRaw;
            flipper_format_rewind(ff);
            while(flipper_format_read_string(ff, "RAW_Data", value)) {
                radiogeddon_dsp_parse_line(
                    furi_string_get_cstr(value),
                    &out->raw_sample_count,
                    &out->raw_min_us,
                    &out->raw_max_us,
                    NULL,
                    NULL,
                    0);
            }
        } else {
            out->kind = RadioGeddonSignalKindProtocol;
            flipper_format_rewind(ff);
            uint32_t bit = 0;
            if(flipper_format_read_uint32(ff, "Bit", &bit, 1)) out->bit_count = bit;
            flipper_format_rewind(ff);
            // Key is stored as a hex byte array (up to 8 bytes) under "Key".
            uint8_t key_bytes[8] = {0};
            if(flipper_format_read_hex(ff, "Key", key_bytes, sizeof(key_bytes))) {
                uint64_t k = 0;
                for(size_t i = 0; i < sizeof(key_bytes); i++) {
                    k = (k << 8) | key_bytes[i];
                }
                out->key = k;
            }
        }
        ok = true;
    } while(false);

    out->valid = ok;
    furi_string_free(type);
    furi_string_free(value);
    flipper_format_free(ff);
    return ok;
}

static size_t radiogeddon_raw_stream_read(void* ctx, uint8_t* buf, size_t len) {
    return stream_read((Stream*)ctx, buf, len);
}

static bool radiogeddon_raw_stream_seek(void* ctx, uint32_t offset) {
    return stream_seek((Stream*)ctx, (int32_t)offset, StreamOffsetFromStart);
}

RadioGeddonRawFile* radiogeddon_storage_raw_open(Storage* storage, const char* path) {
    RadioGeddonRawFile* file = malloc(sizeof(RadioGeddonRawFile));
    file->stream = file_stream_alloc(storage);
    if(!file_stream_open(file->stream, path, FSAM_READ, FSOM_OPEN_EXISTING)) {
        file_stream_close(file->stream);
        stream_free(file->stream);
        free(file);
        return NULL;
    }
    RgRawSource src = {radiogeddon_raw_stream_read, radiogeddon_raw_stream_seek, file->stream};
    rg_raw_reader_init(&file->reader, src);
    return file;
}

void radiogeddon_storage_raw_close(RadioGeddonRawFile* file) {
    if(!file) return;
    file_stream_close(file->stream);
    stream_free(file->stream);
    free(file);
}

void radiogeddon_storage_make_scan_path(FuriString* out) {
    DateTime dt;
    furi_hal_rtc_get_datetime(&dt);
    furi_string_printf(
        out,
        "%s/SCAN_%04u%02u%02u_%02u%02u%02u.csv",
        RADIOGEDDON_SCANS_FOLDER,
        dt.year,
        dt.month,
        dt.day,
        dt.hour,
        dt.minute,
        dt.second);
}

bool radiogeddon_storage_make_unique_path_in(
    Storage* storage,
    FuriString* out,
    const char* folder,
    const char* name,
    const char* ext) {
    furi_string_printf(out, "%s/%s%s", folder, name, ext);
    if(!storage_common_exists(storage, furi_string_get_cstr(out))) return true;
    for(unsigned i = 2; i <= 99; i++) {
        furi_string_printf(out, "%s/%s_%u%s", folder, name, i, ext);
        if(!storage_common_exists(storage, furi_string_get_cstr(out))) return true;
    }
    return false;
}

bool radiogeddon_storage_make_unique_path(Storage* storage, FuriString* out, const char* name) {
    return radiogeddon_storage_make_unique_path_in(
        storage, out, RADIOGEDDON_SIGNALS_FOLDER, name, RADIOGEDDON_SUB_EXTENSION);
}
