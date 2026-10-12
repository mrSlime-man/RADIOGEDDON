#include "rg_actlog.h"
#include "../radiogeddon_edition.h"

#if RG_FEATURE_ACTIVITY_LOG

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

void rg_actlog_init(
    RgActlog* log,
    const uint32_t* frequencies,
    size_t count,
    uint32_t max_bytes,
    uint32_t now_ms) {
    memset(log, 0, sizeof(*log));
    if(count > RG_ACTLOG_MAX_CHANNELS) count = RG_ACTLOG_MAX_CHANNELS;
    log->count = (uint8_t)count;
    if(count) memcpy(log->frequencies, frequencies, count * sizeof(uint32_t));
    for(size_t i = 0; i < RG_ACTLOG_MAX_CHANNELS; i++) {
        log->ch[i].peak_dbm = -127;
        log->ch[i].floor_dbm = -127;
        log->ch[i].i_peak_dbm = -127;
    }
    log->max_bytes = max_bytes < 1024u ? 1024u : max_bytes;
    log->start_ms = now_ms;
    log->interval_start_ms = now_ms;
}

static uint16_t rg_actlog_inc16(uint16_t v) {
    return v == UINT16_MAX ? v : (uint16_t)(v + 1u);
}

void rg_actlog_observe(
    RgActlog* log,
    size_t index,
    bool active,
    bool started,
    int16_t rssi_dbm,
    int16_t floor_dbm,
    uint16_t dwell_ms,
    uint32_t now_ms) {
    if(index >= log->count) return;
    RgActlogChannel* c = &log->ch[index];
    c->visits++;
    c->observed_ms += dwell_ms;
    c->i_visits = rg_actlog_inc16(c->i_visits);
    c->i_observed_ms += dwell_ms;
    if(rssi_dbm > c->peak_dbm) c->peak_dbm = rssi_dbm;
    if(rssi_dbm > c->i_peak_dbm) c->i_peak_dbm = rssi_dbm;
    if(floor_dbm > -127) c->floor_dbm = floor_dbm;
    if(active) {
        c->active_visits++;
        c->active_ms += dwell_ms;
        c->i_active_visits = rg_actlog_inc16(c->i_active_visits);
    }
    if(started) {
        c->events++;
        c->i_events = rg_actlog_inc16(c->i_events);
        if(!c->first_event_ms) c->first_event_ms = now_ms ? now_ms : 1u;
        c->last_event_ms = now_ms ? now_ms : 1u;
    }
    rg_actlog_tick(log, now_ms);
}

void rg_actlog_tick(RgActlog* log, uint32_t now_ms) {
    uint32_t e = now_ms - log->start_ms;
    if(e > log->elapsed_ms && e < 0x80000000u) log->elapsed_ms = e;
}

bool rg_actlog_interval_due(const RgActlog* log, uint32_t interval_ms, uint32_t now_ms) {
    return now_ms - log->interval_start_ms >= interval_ms;
}

/* snprintf that never reports more than it wrote. */
static size_t rg_actlog_fmt(char* out, size_t size, const char* fmt, ...) {
    if(size == 0) return 0;
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(out, size, fmt, ap);
    va_end(ap);
    if(n < 0) {
        out[0] = '\0';
        return 0;
    }
    if((size_t)n >= size) {
        out[0] = '\0';
        return 0; // a cut line is worse than none
    }
    return (size_t)n;
}

size_t rg_actlog_header(
    const RgActlog* log,
    const char* config,
    const char* started,
    char* out,
    size_t size) {
    return rg_actlog_fmt(
        out,
        size,
        "# RadioGeddon RF Activity Log, part %u\n"
        "# Started: %s\n"
        "# Scanner: %s\n"
        "# One frequency is measured at a time (CC1101): a frequency is observed\n"
        "# only during its visits. Empty fields: no measurement in that interval.\n"
        "# Short transmissions between visits are not seen.\n"
        "t_s,freq_hz,visits,observed_ms,active_visits,events,peak_dbm,floor_dbm\n",
        (unsigned)(log->part + 1u),
        started ? started : "?",
        config ? config : "?");
}

size_t rg_actlog_row(const RgActlog* log, size_t index, uint32_t now_ms, char* out, size_t size) {
    if(index >= log->count) return 0;
    const RgActlogChannel* c = &log->ch[index];
    unsigned long t = (unsigned long)((now_ms - log->start_ms) / 1000u);
    unsigned long f = (unsigned long)log->frequencies[index];
    if(c->i_visits == 0) {
        // Nobody listened: no measurement, not "quiet".
        return rg_actlog_fmt(out, size, "%lu,%lu,0,0,,,,\n", t, f);
    }
    char floor[8] = "";
    if(c->floor_dbm > -127) snprintf(floor, sizeof(floor), "%d", (int)c->floor_dbm);
    return rg_actlog_fmt(
        out,
        size,
        "%lu,%lu,%u,%lu,%u,%u,%d,%s\n",
        t,
        f,
        (unsigned)c->i_visits,
        (unsigned long)c->i_observed_ms,
        (unsigned)c->i_active_visits,
        (unsigned)c->i_events,
        (int)c->i_peak_dbm,
        floor);
}

void rg_actlog_end_interval(RgActlog* log, uint32_t now_ms) {
    for(size_t i = 0; i < log->count; i++) {
        RgActlogChannel* c = &log->ch[i];
        c->i_observed_ms = 0;
        c->i_visits = 0;
        c->i_active_visits = 0;
        c->i_events = 0;
        c->i_peak_dbm = -127;
    }
    log->interval_start_ms = now_ms;
    log->intervals++;
}

bool rg_actlog_account(RgActlog* log, uint32_t bytes) {
    log->written += bytes;
    if(log->written < log->max_bytes) return false;
    if(log->part + 1u >= RG_ACTLOG_MAX_PARTS) {
        log->full = true;
        return false;
    }
    log->part++;
    log->written = 0;
    return true;
}

size_t rg_actlog_part_name(const char* base, uint8_t part, char* out, size_t size) {
    if(part == 0) return rg_actlog_fmt(out, size, "%s.csv", base);
    return rg_actlog_fmt(out, size, "%s_%u.csv", base, (unsigned)(part + 1u));
}

size_t rg_actlog_footer(const RgActlog* log, const char* reason, char* out, size_t size) {
    return rg_actlog_fmt(
        out,
        size,
        "# End: %lu s observed, %lu intervals, %s\n",
        (unsigned long)(log->elapsed_ms / 1000u),
        (unsigned long)log->intervals,
        reason ? reason : "stopped");
}

static bool rg_actlog_before(const RgActlog* log, uint8_t a, uint8_t b) {
    const RgActlogChannel* x = &log->ch[a];
    const RgActlogChannel* y = &log->ch[b];
    if(x->events != y->events) return x->events > y->events;
    // Compare active shares without dividing: ax/vx > ay/vy.
    uint64_t l = (uint64_t)x->active_visits * (y->visits ? y->visits : 1u);
    uint64_t r = (uint64_t)y->active_visits * (x->visits ? x->visits : 1u);
    if(l != r) return l > r;
    if(x->peak_dbm != y->peak_dbm) return x->peak_dbm > y->peak_dbm;
    return a < b;
}

size_t rg_actlog_ranking(const RgActlog* log, uint8_t* order, size_t max) {
    uint8_t all[RG_ACTLOG_MAX_CHANNELS];
    size_t n = log->count < RG_ACTLOG_MAX_CHANNELS ? log->count : RG_ACTLOG_MAX_CHANNELS;
    // Insertion sort: channel i goes into the sorted first i entries.
    for(size_t i = 0; i < n; i++) {
        uint8_t v = (uint8_t)i;
        size_t j = i;
        while(j > 0 && rg_actlog_before(log, v, all[j - 1])) {
            all[j] = all[j - 1];
            j--;
        }
        all[j] = v;
    }
    if(n > max) n = max;
    memcpy(order, all, n);
    return n;
}

uint8_t rg_actlog_coverage(const RgActlog* log, size_t index) {
    if(index >= log->count || log->elapsed_ms == 0) return 0;
    uint64_t pct = (uint64_t)log->ch[index].observed_ms * 100u / log->elapsed_ms;
    return (uint8_t)(pct > 100u ? 100u : pct);
}

typedef struct {
    char* out;
    size_t size;
    size_t len;
} RgActText;

static void rg_actlog_cat(RgActText* t, const char* fmt, ...) {
    if(t->len + 1 >= t->size) return;
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(t->out + t->len, t->size - t->len, fmt, ap);
    va_end(ap);
    if(n < 0) return;
    if((size_t)n >= t->size - t->len) {
        // Cut at the last whole line.
        t->out[t->len] = '\0';
        t->len = t->size; // stop further lines
        return;
    }
    t->len += (size_t)n;
}

size_t rg_actlog_summary(const RgActlog* log, char* out, size_t size) {
    if(size == 0) return 0;
    out[0] = '\0';
    RgActText t = {out, size, 0};
    uint32_t s = log->elapsed_ms / 1000u;
    rg_actlog_cat(
        &t,
        "Observed %lu:%02lu:%02lu, %u freq.\n",
        (unsigned long)(s / 3600u),
        (unsigned long)(s / 60u % 60u),
        (unsigned long)(s % 60u),
        (unsigned)log->count);
    uint32_t events = 0;
    for(size_t i = 0; i < log->count; i++)
        events += log->ch[i].events;
    rg_actlog_cat(&t, "[OBSERVED] %lu activity starts\n", (unsigned long)events);
    uint8_t order[RG_ACTLOG_MAX_CHANNELS];
    size_t n = rg_actlog_ranking(log, order, 5);
    bool any = false;
    for(size_t k = 0; k < n; k++) {
        const RgActlogChannel* c = &log->ch[order[k]];
        if(c->events == 0 && c->active_visits == 0) continue;
        if(!any) rg_actlog_cat(&t, "Most active:\n");
        any = true;
        uint32_t f = log->frequencies[order[k]];
        rg_actlog_cat(
            &t,
            "%lu.%02lu: %lu ev, peak %d dBm\n  active %u%% of visits, heard %u%%\n",
            (unsigned long)(f / 1000000u),
            (unsigned long)(f / 10000u % 100u),
            (unsigned long)c->events,
            (int)c->peak_dbm,
            c->visits ? (unsigned)(c->active_visits * 100u / c->visits) : 0u,
            (unsigned)rg_actlog_coverage(log, order[k]));
    }
    if(!any) rg_actlog_cat(&t, "No activity above the threshold\nwhile listening.\n");
    rg_actlog_cat(
        &t,
        "Coverage: each frequency is heard\nonly during its visits (\"heard\").\n"
        "Short bursts between visits\nare missed.\n");
    if(log->full) rg_actlog_cat(&t, "Log stopped: size limit reached.\n");
    return t.len > size - 1 ? strlen(out) : t.len;
}

#endif /* RG_FEATURE_ACTIVITY_LOG */
