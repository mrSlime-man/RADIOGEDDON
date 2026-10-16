#include "rg_heatmap.h"

#include <stdio.h>
#include <string.h>

/* ---- CSV rows ---------------------------------------------------------- */

typedef struct {
    uint32_t t_s;
    uint32_t freq;
    uint32_t visits;
    uint32_t observed_ms;
    uint32_t active;
    uint32_t events;
    int32_t peak;
    int32_t floor;
    bool has_peak;
    bool has_floor;
} HeatRow;

/* One field: an unsigned or (if @p sign) signed decimal, or empty. Advances
 * *p past the field and its comma. False on anything else. */
static bool heat_field(const char** p, bool sign, bool* empty, int64_t* value) {
    const char* s = *p;
    bool neg = false;
    if(sign && *s == '-') {
        neg = true;
        s++;
    }
    int64_t v = 0;
    int digits = 0;
    while(*s >= '0' && *s <= '9') {
        if(++digits > 10) return false;
        v = v * 10 + (*s - '0');
        s++;
    }
    if(digits == 0 && neg) return false;
    if(*s != ',' && *s != '\0') return false;
    *empty = digits == 0;
    *value = neg ? -v : v;
    *p = *s == ',' ? s + 1 : s;
    return true;
}

static bool heat_parse(const char* line, HeatRow* r) {
    const char* p = line;
    int64_t v[8];
    bool empty[8];
    for(int i = 0; i < 8; i++) {
        bool last = i == 7;
        if(!heat_field(&p, i >= 6, &empty[i], &v[i])) return false;
        if(!last && p[-1] != ',') return false; // too few fields
    }
    if(*p != '\0') return false; // too many
    if(empty[0] || empty[1] || empty[2]) return false;
    for(int i = 0; i < 6; i++)
        if(v[i] < 0 || v[i] > 0xFFFFFFFFll) return false;
    r->t_s = (uint32_t)v[0];
    r->freq = (uint32_t)v[1];
    r->visits = (uint32_t)v[2];
    r->observed_ms = empty[3] ? 0 : (uint32_t)v[3];
    r->active = empty[4] ? 0 : (uint32_t)v[4];
    r->events = empty[5] ? 0 : (uint32_t)v[5];
    r->has_peak = !empty[6] && v[6] >= -128 && v[6] <= 127;
    r->has_floor = !empty[7] && v[7] >= -128 && v[7] <= 127;
    r->peak = r->has_peak ? (int32_t)v[6] : 0;
    r->floor = r->has_floor ? (int32_t)v[7] : 0;
    return true;
}

static int heat_channel(const RgHeatmap* h, uint32_t freq) {
    for(int i = 0; i < h->channels; i++)
        if(h->freq[i] == freq) return i;
    return -1;
}

/* ---- probe ---------------------------------------------------------------- */

void rg_heatmap_probe_begin(RgHeatmap* h) {
    memset(h, 0, sizeof(*h));
    h->probing = true;
}

bool rg_heatmap_probe_done(const RgHeatmap* h) {
    return h->probed;
}

size_t rg_heatmap_cells_bytes(const RgHeatmap* h) {
    return (size_t)h->channels * RG_HEATMAP_COLS * sizeof(RgHeatCell);
}

static void heat_probe_row(RgHeatmap* h, const HeatRow* r) {
    if(h->probed) return;
    if(h->channels > 0 && r->t_s != h->probe_t) {
        h->probed = true; // the second interval began
        return;
    }
    h->probe_t = r->t_s;
    if(heat_channel(h, r->freq) < 0 && h->channels < RG_HEATMAP_MAX_CH)
        h->freq[h->channels++] = r->freq;
}

/* ---- binning ---------------------------------------------------------------- */

static void heat_merge(RgHeatCell* a, const RgHeatCell* b) {
    uint32_t obs = (uint32_t)a->observed_ds + b->observed_ds;
    a->observed_ds = obs > 0xFFFFu ? 0xFFFFu : (uint16_t)obs;
    uint32_t ev = (uint32_t)a->events + b->events;
    a->events = ev > 255u ? 255u : (uint8_t)ev;
    a->flags |= b->flags;
    if(b->peak > a->peak) a->peak = b->peak;
    if(b->margin != 0xFF && (a->margin == 0xFF || b->margin > a->margin)) a->margin = b->margin;
}

static void heat_clear(RgHeatCell* c) {
    memset(c, 0, sizeof(*c));
    c->peak = -128;
    c->margin = 0xFF;
}

/* Halve the time resolution: column j <- columns 2j and 2j+1. */
static void heat_halve(RgHeatmap* h) {
    for(size_t ch = 0; ch < h->channels; ch++) {
        RgHeatCell* row = &h->cells[ch * RG_HEATMAP_COLS];
        for(size_t j = 0; j < RG_HEATMAP_COLS / 2; j++) {
            RgHeatCell c = row[2 * j];
            heat_merge(&c, &row[2 * j + 1]);
            row[j] = c;
        }
        for(size_t j = RG_HEATMAP_COLS / 2; j < RG_HEATMAP_COLS; j++)
            heat_clear(&row[j]);
    }
    h->span_s *= 2u;
    h->cols_used = (uint16_t)((h->cols_used + 1u) / 2u);
}

static void heat_recount_max(RgHeatmap* h) {
    uint8_t m = 0;
    size_t n = (size_t)h->channels * RG_HEATMAP_COLS;
    for(size_t i = 0; i < n; i++)
        if(h->cells[i].events > m) m = h->cells[i].events;
    h->max_events = m;
}

static void heat_bin_row(RgHeatmap* h, const HeatRow* r) {
    int ch = heat_channel(h, r->freq);
    if(ch < 0) {
        h->skipped++;
        return;
    }
    // A row's time is the end of its interval: it belongs just before t.
    uint32_t t = r->t_s ? r->t_s - 1u : 0u;
    if(h->span_s == 0) h->span_s = r->t_s ? r->t_s : 1u; // first interval's length
    bool merged = false;
    while(t / h->span_s >= RG_HEATMAP_COLS && h->span_s < 0x80000000u) {
        heat_halve(h);
        merged = true;
    }
    uint32_t col = t / h->span_s;
    if(col >= RG_HEATMAP_COLS) {
        h->skipped++;
        return;
    }
    if(merged) heat_recount_max(h);
    RgHeatCell add;
    heat_clear(&add);
    add.flags = RG_HEAT_ROW;
    if(r->visits > 0) {
        add.flags |= RG_HEAT_VISITED;
        uint32_t ds = r->observed_ms / 100u;
        add.observed_ds = ds > 0xFFFFu ? 0xFFFFu : (uint16_t)ds;
        add.events = r->events > 255u ? 255u : (uint8_t)r->events;
        if(r->active > 0 || r->events > 0) add.flags |= RG_HEAT_ACTIVE;
        if(r->has_peak) add.peak = (int8_t)r->peak;
        if(r->has_peak && r->has_floor) {
            int32_t m = r->peak - r->floor;
            add.margin = m < 0 ? 0 : (m > 127 ? 127 : (uint8_t)m);
        }
    }
    RgHeatCell* c = &h->cells[(size_t)ch * RG_HEATMAP_COLS + col];
    heat_merge(c, &add);
    if(c->events > h->max_events) h->max_events = c->events;
    if(col + 1u > h->cols_used) h->cols_used = (uint16_t)(col + 1u);
    if(r->t_s > h->end_s) h->end_s = r->t_s;
    h->rows++;
}

void rg_heatmap_begin(RgHeatmap* h, RgHeatCell* cells) {
    h->cells = cells;
    h->probing = false;
    h->span_s = 0;
    h->end_s = 0;
    h->cols_used = 0;
    h->max_events = 0;
    h->line_len = 0;
    h->overlong = false;
    h->rows = h->skipped = 0;
    h->ended = false;
    size_t n = (size_t)h->channels * RG_HEATMAP_COLS;
    for(size_t i = 0; i < n; i++)
        heat_clear(&cells[i]);
    // Display order: by frequency.
    for(uint8_t i = 0; i < h->channels; i++) {
        uint8_t j = i;
        while(j > 0 && h->freq[h->order[j - 1]] > h->freq[i]) {
            h->order[j] = h->order[j - 1];
            j--;
        }
        h->order[j] = i;
    }
}

/* ---- lines ------------------------------------------------------------------ */

static void heat_line(RgHeatmap* h) {
    char* s = h->line;
    size_t n = h->line_len;
    if(n && s[n - 1] == '\r') s[--n] = '\0';
    if(n == 0 || h->overlong) return;
    if(s[0] == '#') {
        if(!h->probing && strncmp(s, "# End:", 6) == 0) h->ended = true;
        return;
    }
    if(s[0] < '0' || s[0] > '9') return; // the column names
    HeatRow r;
    if(!heat_parse(s, &r)) {
        if(!h->probing) h->skipped++;
        return;
    }
    if(h->probing) {
        heat_probe_row(h, &r);
    } else if(h->cells) {
        heat_bin_row(h, &r);
    }
}

void rg_heatmap_feed(RgHeatmap* h, const char* data, size_t len) {
    for(size_t i = 0; i < len; i++) {
        if(h->probing && h->probed) return;
        char c = data[i];
        if(c == '\n') {
            h->line[h->line_len] = '\0';
            heat_line(h);
            h->line_len = 0;
            h->overlong = false;
        } else if(h->line_len + 1u < RG_HEATMAP_LINE_MAX) {
            h->line[h->line_len++] = c;
        } else {
            h->overlong = true;
        }
    }
}

void rg_heatmap_end(RgHeatmap* h) {
    if(h->line_len) {
        h->line[h->line_len] = '\0';
        heat_line(h);
        h->line_len = 0;
    }
    h->overlong = false;
    // A one-interval log is fully probed by its end.
    if(h->probing && h->channels > 0) h->probed = true;
}

/* ---- reading the grid ---------------------------------------------------------- */

const RgHeatCell* rg_heatmap_cell(const RgHeatmap* h, size_t row, size_t col) {
    if(!h->cells || row >= h->channels || col >= RG_HEATMAP_COLS) return NULL;
    return &h->cells[(size_t)h->order[row] * RG_HEATMAP_COLS + col];
}

uint32_t rg_heatmap_row_freq(const RgHeatmap* h, size_t row) {
    return row < h->channels ? h->freq[h->order[row]] : 0;
}

RgHeatLevel rg_heatmap_level(const RgHeatmap* h, const RgHeatCell* c, RgHeatMetric metric) {
    if(!c || !(c->flags & RG_HEAT_VISITED)) return RgHeatNotObserved;
    if(!(c->flags & RG_HEAT_ACTIVE)) return RgHeatQuiet;
    if(metric == RgHeatMetricStrength) {
        if(c->margin == 0xFF || c->margin < 10) return RgHeatLow;
        return c->margin < 20 ? RgHeatMid : RgHeatHigh;
    }
    uint32_t m = h->max_events ? h->max_events : 1u;
    if((uint32_t)c->events * 3u >= 2u * m) return RgHeatHigh;
    if((uint32_t)c->events * 3u >= m) return RgHeatMid;
    return RgHeatLow;
}

uint8_t rg_heatmap_coverage(const RgHeatmap* h, const RgHeatCell* c) {
    if(!c || h->span_s == 0) return 0;
    uint64_t pct = (uint64_t)c->observed_ds * 10u / h->span_s;
    return pct > 100u ? 100u : (uint8_t)pct;
}

static void heat_time(char* out, size_t size, uint32_t s) {
    if(s >= 3600u)
        snprintf(out, size, "%luh%02lu", (unsigned long)(s / 3600u), (unsigned long)(s / 60u % 60u));
    else
        snprintf(out, size, "%lu:%02lu", (unsigned long)(s / 60u), (unsigned long)(s % 60u));
}

size_t rg_heatmap_col_time(const RgHeatmap* h, size_t col, char* out, size_t size) {
    if(size == 0) return 0;
    char a[12], b[12];
    heat_time(a, sizeof(a), (uint32_t)col * h->span_s);
    heat_time(b, sizeof(b), (uint32_t)(col + 1u) * h->span_s);
    int n = snprintf(out, size, "%s-%s", a, b);
    if(n < 0) return 0;
    return (size_t)n < size ? (size_t)n : size - 1;
}

size_t rg_heatmap_cell_text(const RgHeatmap* h, size_t row, size_t col, char* out, size_t size) {
    const RgHeatCell* c = rg_heatmap_cell(h, row, col);
    if(!c || size == 0) return 0;
    uint32_t f = rg_heatmap_row_freq(h, row);
    char what[32];
    if(!(c->flags & RG_HEAT_VISITED)) {
        snprintf(what, sizeof(what), "%s", (c->flags & RG_HEAT_ROW) ? "not observed" : "no data");
    } else if(!(c->flags & RG_HEAT_ACTIVE)) {
        snprintf(what, sizeof(what), "quiet, cov %u%%", (unsigned)rg_heatmap_coverage(h, c));
    } else {
        // Capped so the line fits the screen: "99+" is "many" there.
        unsigned ev = c->events > 99 ? 99u : c->events;
        snprintf(
            what,
            sizeof(what),
            "%u%s burst%s cov %u%%",
            ev,
            c->events > 99 ? "+" : "",
            ev == 1 ? "" : "s",
            (unsigned)rg_heatmap_coverage(h, c));
    }
    int n = snprintf(
        out,
        size,
        "%lu.%02lu %s",
        (unsigned long)(f / 1000000u),
        (unsigned long)(f / 10000u % 100u),
        what);
    if(n < 0) return 0;
    return (size_t)n < size ? (size_t)n : size - 1;
}
