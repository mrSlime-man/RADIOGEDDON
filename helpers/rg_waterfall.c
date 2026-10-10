#include "rg_waterfall.h"
#include "../radiogeddon_edition.h"

#if RG_FEATURE_WATERFALL

#include <string.h>

size_t rg_waterfall_bytes(uint32_t points, uint16_t rows) {
    uint32_t columns = points < RG_WF_MAX_COLUMNS ? points : RG_WF_MAX_COLUMNS;
    // Timestamps first (4-byte aligned when the buffer is), then the cells.
    return (size_t)rows * (columns + RG_WF_ROW_EXTRA) + 3u;
}

uint16_t rg_waterfall_rows_for(uint32_t points, size_t bytes) {
    if(points == 0 || bytes <= 3u) return 0;
    uint32_t columns = points < RG_WF_MAX_COLUMNS ? points : RG_WF_MAX_COLUMNS;
    size_t rows = (bytes - 3u) / (columns + RG_WF_ROW_EXTRA);
    return rows > RG_WF_MAX_ROWS ? RG_WF_MAX_ROWS : (uint16_t)rows;
}

bool rg_waterfall_init(RgWaterfall* wf, void* buf, size_t bytes, uint32_t points) {
    memset(wf, 0, sizeof(*wf));
    uint16_t rows = rg_waterfall_rows_for(points, bytes);
    if(!buf || rows == 0) return false;
    uintptr_t base = (uintptr_t)buf;
    uintptr_t aligned = (base + 3u) & ~(uintptr_t)3u;
    wf->points = points;
    wf->columns = (uint16_t)(points < RG_WF_MAX_COLUMNS ? points : RG_WF_MAX_COLUMNS);
    wf->rows = rows;
    wf->stamp = (uint32_t*)aligned;
    wf->cells = (uint8_t*)(aligned + (uintptr_t)rows * RG_WF_ROW_EXTRA);
    rg_waterfall_clear(wf);
    return true;
}

void rg_waterfall_clear(RgWaterfall* wf) {
    if(wf->cells) memset(wf->cells, 0, (size_t)wf->rows * wf->columns);
    if(wf->stamp) memset(wf->stamp, 0, (size_t)wf->rows * sizeof(uint32_t));
    wf->head = 0;
    wf->filled = 0;
    wf->sweeps = 0;
    wf->missing = 0;
    rg_waterfall_discard(wf);
}

uint16_t rg_waterfall_column_of(const RgWaterfall* wf, uint32_t index) {
    if(wf->points == 0) return 0;
    if(index >= wf->points) index = wf->points - 1;
    return (uint16_t)((uint64_t)index * wf->columns / wf->points);
}

void rg_waterfall_points_of(
    const RgWaterfall* wf,
    uint16_t column,
    uint32_t* first,
    uint32_t* last) {
    *first = 0;
    *last = 0;
    if(wf->columns == 0) return;
    if(column >= wf->columns) column = wf->columns - 1;
    // Inverse of rg_waterfall_column_of: the points i with i * C / P == column.
    uint32_t lo = (uint32_t)(((uint64_t)column * wf->points + wf->columns - 1) / wf->columns);
    uint32_t hi =
        (uint32_t)(((uint64_t)(column + 1) * wf->points + wf->columns - 1) / wf->columns);
    *first = lo;
    *last = hi > lo ? hi - 1 : lo;
}

static uint8_t rg_waterfall_encode(float dbm) {
    float r = dbm + (float)RG_WF_CELL_BIAS;
    r += r < 0 ? -0.5f : 0.5f;
    if(r < 1.0f) return 1; // weakest storable: never confused with "missing"
    if(r > 255.0f) return 255;
    return (uint8_t)r;
}

void rg_waterfall_add(RgWaterfall* wf, uint32_t index, float dbm) {
    if(wf->columns == 0 || index >= wf->points) return;
    uint16_t c = rg_waterfall_column_of(wf, index);
    uint8_t v = rg_waterfall_encode(dbm);
    if(v > wf->cur[c]) wf->cur[c] = v;
    wf->cur_any = true;
}

void rg_waterfall_discard(RgWaterfall* wf) {
    memset(wf->cur, 0, sizeof(wf->cur));
    wf->cur_any = false;
}

bool rg_waterfall_commit(RgWaterfall* wf, uint32_t now_ms) {
    if(wf->rows == 0 || !wf->cur_any) {
        rg_waterfall_discard(wf);
        return false;
    }
    uint8_t* row = &wf->cells[(size_t)wf->head * wf->columns];
    memcpy(row, wf->cur, wf->columns);
    for(uint16_t c = 0; c < wf->columns; c++)
        if(row[c] == 0) wf->missing++;
    wf->stamp[wf->head] = now_ms;
    wf->head = (uint16_t)((wf->head + 1u) % wf->rows);
    if(wf->filled < wf->rows) wf->filled++;
    if(wf->sweeps < UINT32_MAX) wf->sweeps++;
    rg_waterfall_discard(wf);
    return true;
}

static const uint8_t* rg_waterfall_row(const RgWaterfall* wf, uint16_t age) {
    if(age >= wf->filled) return NULL;
    uint16_t idx = (uint16_t)((wf->head + wf->rows - 1u - age) % wf->rows);
    return &wf->cells[(size_t)idx * wf->columns];
}

int16_t rg_waterfall_cell(const RgWaterfall* wf, uint16_t age, uint16_t column) {
    const uint8_t* row = rg_waterfall_row(wf, age);
    if(!row || column >= wf->columns || row[column] == 0) return RG_WF_NO_DATA;
    return (int16_t)((int16_t)row[column] - RG_WF_CELL_BIAS);
}

uint32_t rg_waterfall_stamp(const RgWaterfall* wf, uint16_t age) {
    if(age >= wf->filled) return 0;
    return wf->stamp[(wf->head + wf->rows - 1u - age) % wf->rows];
}

bool rg_waterfall_peak(const RgWaterfall* wf, uint16_t* column, uint16_t* age, int16_t* dbm) {
    uint8_t best = 0;
    for(uint16_t a = 0; a < wf->filled; a++) {
        const uint8_t* row = rg_waterfall_row(wf, a);
        for(uint16_t c = 0; c < wf->columns; c++) {
            if(row[c] > best) {
                best = row[c];
                if(column) *column = c;
                if(age) *age = a;
            }
        }
    }
    if(best == 0) return false;
    if(dbm) *dbm = (int16_t)((int16_t)best - RG_WF_CELL_BIAS);
    return true;
}

uint8_t rg_waterfall_level(int16_t dbm, int8_t floor, const RgWaterfallStyle* style) {
    if(dbm == RG_WF_NO_DATA) return 0;
    int32_t ref = RG_WF_ABS_REF_DBM;
    if(style->noise_comp) {
        if(floor != RG_WF_NO_FLOOR) {
            ref = floor;
        } else if(style->floor_all != RG_WF_NO_FLOOR) {
            ref = style->floor_all;
        }
    }
    int32_t excess = (int32_t)dbm - ref;
    if(excess <= 0) return 0;
    if(style->strong_db && excess >= style->strong_db) return RG_WF_LEVEL_STRONG;
    uint32_t span = style->span_db ? style->span_db : 1u;
    uint32_t level = ((uint32_t)excess * RG_WF_LEVEL_MAX + span - 1u) / span;
    return (uint8_t)(level > RG_WF_LEVEL_MAX ? RG_WF_LEVEL_MAX : level);
}

/* 4x4 Bayer matrix: a pixel is set when its threshold is below the level. */
static const uint8_t rg_waterfall_bayer[4][4] = {
    {0, 8, 2, 10},
    {12, 4, 14, 6},
    {3, 11, 1, 9},
    {15, 7, 13, 5},
};

bool rg_waterfall_dither(uint8_t level, uint16_t x, uint16_t y) {
    if(level >= RG_WF_LEVEL_STRONG) return true;
    return rg_waterfall_bayer[y & 3u][x & 3u] < level;
}

bool rg_waterfall_missing_dot(uint16_t x, uint16_t y) {
    // A vertical dotted line every 8 pixels: unlike any dither level (which
    // starts with isolated dots on a 4x4 grid), so "not measured" never reads
    // as "quiet" or "weak".
    return (x & 7u) == 4u && (y & 1u) == 0u;
}

uint16_t rg_waterfall_column_at(const RgWaterfall* wf, uint16_t x, uint16_t width) {
    if(wf->columns == 0 || width == 0) return 0;
    if(x >= width) x = (uint16_t)(width - 1u);
    return (uint16_t)((uint32_t)x * wf->columns / width);
}

void rg_waterfall_column_span(
    const RgWaterfall* wf,
    uint16_t column,
    uint16_t width,
    uint16_t* x,
    uint16_t* w) {
    *x = 0;
    *w = 0;
    if(wf->columns == 0 || width == 0) return;
    if(column >= wf->columns) column = (uint16_t)(wf->columns - 1u);
    // Pixels whose rg_waterfall_column_at() is this column.
    uint32_t lo = ((uint32_t)column * width + wf->columns - 1u) / wf->columns;
    uint32_t hi = ((uint32_t)(column + 1u) * width + wf->columns - 1u) / wf->columns;
    if(hi > width) hi = width;
    *x = (uint16_t)lo;
    *w = (uint16_t)(hi > lo ? hi - lo : 0);
}

uint16_t rg_waterfall_max_scroll(const RgWaterfall* wf, uint16_t height) {
    return wf->filled > height ? (uint16_t)(wf->filled - height) : 0;
}

uint16_t rg_waterfall_render(
    const RgWaterfall* wf,
    const RgWaterfallStyle* style,
    uint16_t scroll,
    uint8_t* xbm,
    uint16_t width,
    uint16_t height) {
    size_t stride = ((size_t)width + 7u) / 8u;
    memset(xbm, 0, stride * height);
    if(wf->columns == 0 || width == 0) return 0;
    uint16_t drawn = 0;
    for(uint16_t y = 0; y < height; y++) {
        uint32_t age = (uint32_t)scroll + y;
        if(age >= wf->filled) break;
        const uint8_t* row = rg_waterfall_row(wf, (uint16_t)age);
        uint8_t* out = &xbm[(size_t)y * stride];
        drawn++;
        for(uint16_t x = 0; x < width; x++) {
            uint16_t c = rg_waterfall_column_at(wf, x, width);
            bool on;
            if(row[c] == 0) {
                on = rg_waterfall_missing_dot(x, (uint16_t)age);
            } else {
                int8_t floor = style->floors ? style->floors[c] : RG_WF_NO_FLOOR;
                int16_t dbm = (int16_t)((int16_t)row[c] - RG_WF_CELL_BIAS);
                // Dither on the row's age, so a pattern scrolls with its sweep.
                on = rg_waterfall_dither(rg_waterfall_level(dbm, floor, style), x, (uint16_t)age);
            }
            if(on) out[x / 8u] |= (uint8_t)(1u << (x % 8u));
        }
    }
    return drawn;
}

#endif /* RG_FEATURE_WATERFALL */
