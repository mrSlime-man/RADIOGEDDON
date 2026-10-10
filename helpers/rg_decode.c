#include "rg_decode.h"

#include <string.h>

#define RG_DECODE_CHUNK 64u

void rg_decode_log_init(RgDecodeLog* log) {
    memset(log, 0, sizeof(*log));
}

bool rg_decode_level(int32_t sample, bool* level, uint32_t* duration) {
    if(sample == 0) return false;
    *level = sample > 0;
    /* -INT32_MIN does not fit an int32_t; the magnitude does fit a uint32_t. */
    *duration = sample > 0 ? (uint32_t)sample : 0u - (uint32_t)sample;
    return true;
}

/* Copy @p text without '\r' into @p out (size RG_DECODE_TEXT); true if cut. */
static bool rg_decode_copy_text(char* out, const char* text) {
    size_t n = 0;
    for(; *text; text++) {
        if(*text == '\r') continue;
        if(n == RG_DECODE_TEXT - 1) {
            out[n] = '\0';
            return true;
        }
        out[n++] = *text;
    }
    out[n] = '\0';
    return false;
}

void rg_decode_log_add(RgDecodeLog* log, const char* protocol, const char* text, uint8_t hash) {
    char name[RG_DECODE_NAME];
    char clean[RG_DECODE_TEXT];
    if(!protocol) protocol = "";
    strncpy(name, protocol, sizeof(name) - 1);
    name[sizeof(name) - 1] = '\0';
    bool truncated = rg_decode_copy_text(clean, text ? text : "");

    log->decodes++;
    for(size_t i = 0; i < log->hit_count; i++) {
        RgDecodeHit* h = &log->hit[i];
        if(h->hash == hash && strcmp(h->protocol, name) == 0 && strcmp(h->text, clean) == 0) {
            h->count++;
            h->last_us = log->time_us;
            return;
        }
    }
    if(log->hit_count == RG_DECODE_MAX_HITS) {
        log->dropped++;
        return;
    }
    RgDecodeHit* h = &log->hit[log->hit_count++];
    memcpy(h->protocol, name, sizeof(name));
    memcpy(h->text, clean, sizeof(clean));
    h->truncated = truncated;
    h->hash = hash;
    h->count = 1;
    h->first_us = log->time_us;
    h->last_us = log->time_us;
}

uint32_t rg_decode_run(
    RgRawReader* reader,
    RgDecodeLog* log,
    RgDecodeFeed feed,
    void* feed_context,
    RgDecodeProgress progress,
    void* progress_context) {
    int32_t chunk[RG_DECODE_CHUNK];
    uint32_t fed = 0;
    size_t n;
    while((n = rg_raw_reader_read(reader, chunk, RG_DECODE_CHUNK)) > 0) {
        for(size_t i = 0; i < n; i++) {
            bool level;
            uint32_t duration;
            if(!rg_decode_level(chunk[i], &level, &duration)) continue;
            /* A decode reported now is timed at this sample's start. */
            feed(feed_context, level, duration);
            log->time_us += duration;
            log->samples++;
            if(++fed % RG_DECODE_PROGRESS_SAMPLES == 0 && progress) {
                progress(progress_context, (uint32_t)(reader->buf_offset + reader->buf_pos));
            }
        }
    }
    return fed;
}
