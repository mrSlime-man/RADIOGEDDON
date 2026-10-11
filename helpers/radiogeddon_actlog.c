#include "radiogeddon_actlog.h"

#if RG_FEATURE_ACTIVITY_LOG

#include <furi_hal_rtc.h>

#define TAG "RadioGeddonActlog"

struct RadioGeddonActlog {
    Storage* storage;
    File* file; // the open part, NULL between parts
    RadioGeddonActlogConfig config;
    char base[48];
    char started[32];
    uint32_t rows;
    uint32_t bytes;
    uint8_t state; // RadioGeddonActlogState
    RgActlog log; // last: its size depends on RG_ACTLOG_MAX_CHANNELS only
};

size_t radiogeddon_actlog_memory(size_t count) {
    UNUSED(count);
    // The log, a row buffer the scene lends, the open file.
    return sizeof(RadioGeddonActlog) + RG_ACTLOG_MAX_CHANNELS * RG_ACTLOG_LINE_MAX + 1024u;
}

static void actlog_path(RadioGeddonActlog* l, uint8_t part, FuriString* out) {
    char name[64];
    rg_actlog_part_name(l->base, part, name, sizeof(name));
    furi_string_printf(out, "%s/%s", RADIOGEDDON_ACTLOG_FOLDER, name);
}

/* Open the current part (never replacing a file) and write its header. */
static bool actlog_open_part(RadioGeddonActlog* l) {
    FuriString* path = furi_string_alloc();
    actlog_path(l, l->log.part, path);
    l->file = storage_file_alloc(l->storage);
    bool ok = storage_file_open(l->file, furi_string_get_cstr(path), FSAM_WRITE, FSOM_CREATE_NEW);
    furi_string_free(path);
    if(ok) {
        char head[512];
        size_t n = rg_actlog_header(&l->log, l->config.scanner, l->started, head, sizeof(head));
        ok = n > 0 && storage_file_write(l->file, head, n) == n;
        if(ok) {
            l->bytes += n;
            l->log.written += (uint32_t)n;
        }
    }
    if(!ok) {
        storage_file_close(l->file);
        storage_file_free(l->file);
        l->file = NULL;
    }
    return ok;
}

static void actlog_close_part(RadioGeddonActlog* l) {
    if(!l->file) return;
    storage_file_close(l->file);
    storage_file_free(l->file);
    l->file = NULL;
}

RadioGeddonActlog* radiogeddon_actlog_start(
    Storage* storage,
    const uint32_t* frequencies,
    size_t count,
    const RadioGeddonActlogConfig* config,
    uint32_t now) {
    RadioGeddonActlog* l = malloc(sizeof(RadioGeddonActlog));
    memset(l, 0, sizeof(*l));
    l->storage = storage;
    l->config = *config;
    rg_actlog_init(&l->log, frequencies, count, config->max_bytes, now);
    DateTime dt;
    furi_hal_rtc_get_datetime(&dt);
    snprintf(
        l->base,
        sizeof(l->base),
        "activity_%04u%02u%02u_%02u%02u%02u",
        dt.year,
        dt.month,
        dt.day,
        dt.hour,
        dt.minute,
        dt.second);
    snprintf(
        l->started,
        sizeof(l->started),
        "%04u-%02u-%02u %02u:%02u:%02u",
        dt.year,
        dt.month,
        dt.day,
        dt.hour,
        dt.minute,
        dt.second);
    storage_common_mkdir(storage, RADIOGEDDON_ACTLOG_FOLDER);
    if(!actlog_open_part(l)) {
        free(l);
        return NULL;
    }
    return l;
}

void radiogeddon_actlog_observe(
    void* context,
    size_t index,
    bool active,
    bool started,
    int16_t rssi_dbm,
    int16_t floor_dbm,
    uint16_t dwell_ms,
    uint32_t now_ms) {
    RadioGeddonActlog* l = context;
    rg_actlog_observe(&l->log, index, active, started, rssi_dbm, floor_dbm, dwell_ms, now_ms);
}

static bool actlog_time_up(RadioGeddonActlog* l) {
    return l->config.minutes && l->log.elapsed_ms >= (uint32_t)l->config.minutes * 60000u;
}

size_t radiogeddon_actlog_collect(RadioGeddonActlog* l, uint32_t now, char* buf, size_t size) {
    rg_actlog_tick(&l->log, now);
    if(l->state != RadioGeddonActlogRunning) return 0;
    if(actlog_time_up(l)) l->state = RadioGeddonActlogDone;
    bool due = rg_actlog_interval_due(&l->log, (uint32_t)l->config.interval_s * 1000u, now);
    // The last, partial interval is written when the time is up.
    if(!due && l->state == RadioGeddonActlogRunning) return 0;
    size_t len = 0;
    for(size_t i = 0; i < l->log.count; i++) {
        size_t n = rg_actlog_row(&l->log, i, now, buf + len, size - len);
        if(n == 0) break; // the buffer is sized for every row; never cut one
        len += n;
    }
    l->rows += l->log.count;
    rg_actlog_end_interval(&l->log, now);
    return len;
}

bool radiogeddon_actlog_write(RadioGeddonActlog* l, const char* buf, size_t len) {
    bool alive = l->state == RadioGeddonActlogRunning || l->state == RadioGeddonActlogDone;
    if(len == 0) return alive;
    if(l->state == RadioGeddonActlogFull || l->state == RadioGeddonActlogFailed) return false;
    if(!l->file && !actlog_open_part(l)) {
        l->state = RadioGeddonActlogFailed;
        return false;
    }
    if(storage_file_write(l->file, buf, len) != len) {
        FURI_LOG_W(TAG, "write failed");
        l->state = RadioGeddonActlogFailed;
        actlog_close_part(l);
        return false;
    }
    l->bytes += (uint32_t)len;
    if(rg_actlog_account(&l->log, (uint32_t)len)) {
        actlog_close_part(l); // the next rows go to the next part
    } else if(l->log.full) {
        l->state = RadioGeddonActlogFull;
        actlog_close_part(l);
        return false;
    }
    return true;
}

void radiogeddon_actlog_status(RadioGeddonActlog* l, uint32_t now, RadioGeddonActlogStatus* out) {
    memset(out, 0, sizeof(*out));
    rg_actlog_tick(&l->log, now);
    out->state = l->state;
    out->elapsed_s = l->log.elapsed_ms / 1000u;
    out->limit_s = (uint32_t)l->config.minutes * 60u;
    for(size_t i = 0; i < l->log.count; i++)
        out->events += l->log.ch[i].events;
    out->rows = l->rows;
    out->bytes = l->bytes;
    out->part = l->log.part;
    uint8_t order[2];
    size_t n = rg_actlog_ranking(&l->log, order, 2);
    for(size_t k = 0; k < 2; k++) {
        out->top[k] = 0xFF;
        if(k < n && l->log.ch[order[k]].events) {
            out->top[k] = order[k];
            out->top_hz[k] = l->log.frequencies[order[k]];
            out->top_events[k] = l->log.ch[order[k]].events;
            out->top_peak[k] = l->log.ch[order[k]].peak_dbm;
        }
    }
    rg_actlog_part_name(l->base, l->log.part, out->file, sizeof(out->file));
}

size_t radiogeddon_actlog_finish(
    RadioGeddonActlog* l,
    const char* reason,
    uint32_t now,
    char* summary,
    size_t size) {
    rg_actlog_tick(&l->log, now);
    char* rows = malloc(RG_ACTLOG_MAX_CHANNELS * RG_ACTLOG_LINE_MAX);
    // The interval in progress, if anything was measured in it.
    bool measured = false;
    for(size_t i = 0; i < l->log.count; i++)
        if(l->log.ch[i].i_visits) measured = true;
    if(measured && (l->state == RadioGeddonActlogRunning || l->state == RadioGeddonActlogDone)) {
        size_t len = 0;
        for(size_t i = 0; i < l->log.count; i++)
            len += rg_actlog_row(
                &l->log,
                i,
                now,
                rows + len,
                RG_ACTLOG_MAX_CHANNELS * RG_ACTLOG_LINE_MAX - len);
        rg_actlog_end_interval(&l->log, now);
        l->rows += l->log.count;
        uint8_t st = l->state;
        l->state = RadioGeddonActlogRunning; // let the last rows through
        radiogeddon_actlog_write(l, rows, len);
        if(l->state == RadioGeddonActlogRunning) l->state = st;
    }
    if(l->file || (l->state != RadioGeddonActlogFailed && l->state != RadioGeddonActlogFull &&
                   actlog_open_part(l))) {
        size_t n = rg_actlog_footer(&l->log, reason, rows, RG_ACTLOG_LINE_MAX);
        if(n && storage_file_write(l->file, rows, n) == n) l->bytes += (uint32_t)n;
    }
    actlog_close_part(l);
    free(rows);

    size_t len = rg_actlog_summary(&l->log, summary, size);
    // Where it went and how it ended.
    int extra = snprintf(
        summary + len,
        size > len ? size - len : 0,
        "Ended: %s.\nLog: activity/%s.csv%s\n%lu rows, %lu bytes\n",
        reason ? reason : "stopped",
        l->base,
        l->log.part ? " (+parts)" : "",
        (unsigned long)l->rows,
        (unsigned long)l->bytes);
    if(extra > 0) len += (size_t)extra < size - len ? (size_t)extra : size - len - 1;
    // The summary next to the log.
    FuriString* path = furi_string_alloc_printf(
        "%s/%s_summary.txt", RADIOGEDDON_ACTLOG_FOLDER, l->base);
    File* file = storage_file_alloc(l->storage);
    if(storage_file_open(file, furi_string_get_cstr(path), FSAM_WRITE, FSOM_CREATE_NEW)) {
        storage_file_write(file, summary, len);
    }
    storage_file_close(file);
    storage_file_free(file);
    furi_string_free(path);
    free(l);
    return len;
}

#endif
