#include "signal_storage.h"

#include <lib/flipper_format/flipper_format.h>
#include <lib/subghz/types.h>
#include <lib/subghz/protocols/raw.h>

#define TAG "RadioGeddon"

/* Read up to RG_COMPARE_MAX_SAMPLES RAW_Data timing values from an opened
 * flipper file into `buf`. Returns the number of samples read. */
static size_t rg_storage_load_raw(FlipperFormat* ff, int32_t* buf, size_t cap) {
    size_t total = 0;
    uint32_t line_count = 0;
    /* Each RAW_Data line holds a batch of values; iterate every occurrence. */
    while(total < cap && flipper_format_get_value_count(ff, "RAW_Data", &line_count)) {
        if(line_count == 0) break;
        if(line_count > cap - total) line_count = (uint32_t)(cap - total);
        if(!flipper_format_read_int32(ff, "RAW_Data", buf + total, (uint16_t)line_count)) break;
        total += line_count;
    }
    return total;
}

bool rg_storage_read_info(const char* path, FuriString* out) {
    furi_check(out);
    Storage* storage = furi_record_open(RECORD_STORAGE);
    FlipperFormat* ff = flipper_format_file_alloc(storage);
    FuriString* type = furi_string_alloc();
    FuriString* preset = furi_string_alloc();
    FuriString* protocol = furi_string_alloc();
    uint32_t version = 0;
    uint32_t frequency = 0;
    bool ok = false;

    do {
        if(!flipper_format_file_open_existing(ff, path)) {
            furi_string_set(out, "Cannot open file");
            break;
        }
        if(!flipper_format_read_header(ff, type, &version)) {
            furi_string_set(out, "Bad .sub header");
            break;
        }
        flipper_format_read_uint32(ff, "Frequency", &frequency, 1);
        flipper_format_read_string(ff, "Preset", preset);
        if(!flipper_format_read_string(ff, "Protocol", protocol)) {
            furi_string_set(protocol, "-");
        }

        bool is_raw = furi_string_cmp_str(protocol, SUBGHZ_PROTOCOL_RAW_NAME) == 0;
        furi_string_printf(
            out,
            "Type: %s\nFreq: %lu.%02lu MHz\nPreset: %s\nProtocol: %s",
            furi_string_get_cstr(type),
            frequency / 1000000UL,
            (frequency % 1000000UL) / 10000UL,
            furi_string_get_cstr(preset),
            furi_string_get_cstr(protocol));

        if(is_raw) {
            flipper_format_rewind(ff);
            uint32_t batches = 0;
            uint32_t line_count = 0;
            size_t samples = 0;
            while(flipper_format_get_value_count(ff, "RAW_Data", &line_count) && line_count > 0) {
                int32_t scratch[512];
                uint16_t n = line_count > 512 ? 512 : (uint16_t)line_count;
                if(!flipper_format_read_int32(ff, "RAW_Data", scratch, n)) break;
                samples += n;
                batches++;
                if(batches > 4096) break; /* safety bound */
            }
            furi_string_cat_printf(out, "\nRAW samples: %u", (unsigned)samples);
        }
        ok = true;
    } while(false);

    furi_string_free(type);
    furi_string_free(preset);
    furi_string_free(protocol);
    flipper_format_free(ff);
    furi_record_close(RECORD_STORAGE);
    return ok;
}

bool rg_storage_delete(const char* path) {
    Storage* storage = furi_record_open(RECORD_STORAGE);
    bool ok = storage_simply_remove(storage, path);
    furi_record_close(RECORD_STORAGE);
    return ok;
}

bool rg_storage_exists(const char* path) {
    Storage* storage = furi_record_open(RECORD_STORAGE);
    bool ok = storage_file_exists(storage, path);
    furi_record_close(RECORD_STORAGE);
    return ok;
}

/* Read frequency, preset and RAW samples from one file. */
static bool rg_storage_load_for_compare(
    Storage* storage,
    const char* path,
    uint32_t* frequency,
    FuriString* preset,
    int32_t* buf,
    size_t cap,
    size_t* count) {
    FlipperFormat* ff = flipper_format_file_alloc(storage);
    FuriString* type = furi_string_alloc();
    uint32_t version = 0;
    bool ok = false;
    do {
        if(!flipper_format_file_open_existing(ff, path)) break;
        if(!flipper_format_read_header(ff, type, &version)) break;
        if(furi_string_cmp_str(type, SUBGHZ_RAW_FILE_TYPE) != 0) break;
        flipper_format_read_uint32(ff, "Frequency", frequency, 1);
        flipper_format_read_string(ff, "Preset", preset);
        flipper_format_rewind(ff);
        *count = rg_storage_load_raw(ff, buf, cap);
        ok = (*count > 0);
    } while(false);
    furi_string_free(type);
    flipper_format_free(ff);
    return ok;
}

/* Tolerant match of two durations: same polarity and within +/-25%. */
static bool rg_storage_sample_match(int32_t a, int32_t b) {
    if((a < 0) != (b < 0)) return false;
    int64_t aa = a < 0 ? -(int64_t)a : a;
    int64_t bb = b < 0 ? -(int64_t)b : b;
    int64_t diff = aa > bb ? aa - bb : bb - aa;
    int64_t tol = (aa > bb ? aa : bb) / 4; /* 25% */
    if(tol < 50) tol = 50; /* absolute floor in us */
    return diff <= tol;
}

bool rg_storage_compare(const char* path_a, const char* path_b, FuriString* out, int* out_score) {
    furi_check(out);
    Storage* storage = furi_record_open(RECORD_STORAGE);

    int32_t* buf_a = malloc(sizeof(int32_t) * RG_COMPARE_MAX_SAMPLES);
    int32_t* buf_b = malloc(sizeof(int32_t) * RG_COMPARE_MAX_SAMPLES);
    FuriString* preset_a = furi_string_alloc();
    FuriString* preset_b = furi_string_alloc();
    uint32_t freq_a = 0, freq_b = 0;
    size_t count_a = 0, count_b = 0;
    bool ok = false;
    int score = 0;

    do {
        if(!rg_storage_load_for_compare(
               storage, path_a, &freq_a, preset_a, buf_a, RG_COMPARE_MAX_SAMPLES, &count_a)) {
            furi_string_set(out, "File A is not a readable RAW recording");
            break;
        }
        if(!rg_storage_load_for_compare(
               storage, path_b, &freq_b, preset_b, buf_b, RG_COMPARE_MAX_SAMPLES, &count_b)) {
            furi_string_set(out, "File B is not a readable RAW recording");
            break;
        }

        size_t min_len = count_a < count_b ? count_a : count_b;
        size_t max_len = count_a > count_b ? count_a : count_b;
        size_t matches = 0;
        for(size_t i = 0; i < min_len; i++) {
            if(rg_storage_sample_match(buf_a[i], buf_b[i])) matches++;
        }
        /* Penalise length mismatch: score over the longer sequence. */
        int timing_score = max_len ? (int)((matches * 100) / max_len) : 0;

        bool freq_match = (freq_a == freq_b);
        bool preset_match = (furi_string_cmp(preset_a, preset_b) == 0);

        /* Final score weights timing heavily, with freq/preset as gates. */
        score = timing_score;
        if(!freq_match) score = score > 40 ? score - 40 : 0;
        if(!preset_match) score = score > 15 ? score - 15 : 0;
        if(score > 100) score = 100;

        const char* verdict;
        if(score >= 85) {
            verdict = "MATCH - very likely same signal";
        } else if(score >= 60) {
            verdict = "SIMILAR - possibly related";
        } else {
            verdict = "DIFFERENT";
        }

        furi_string_printf(
            out,
            "Similarity: %d%%\n%s\n\nFreq: %s (%lu vs %lu)\nPreset: %s\nSamples: %u vs %u\nMatched: %u/%u",
            score,
            verdict,
            freq_match ? "same" : "DIFFER",
            freq_a,
            freq_b,
            preset_match ? "same" : "DIFFER",
            (unsigned)count_a,
            (unsigned)count_b,
            (unsigned)matches,
            (unsigned)min_len);
        ok = true;
    } while(false);

    if(out_score) *out_score = score;

    free(buf_a);
    free(buf_b);
    furi_string_free(preset_a);
    furi_string_free(preset_b);
    furi_record_close(RECORD_STORAGE);
    return ok;
}
