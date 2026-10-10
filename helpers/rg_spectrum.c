#include "rg_spectrum.h"
#include "../radiogeddon_edition.h"

#if RG_FEATURE_RANGE_SCAN

#include <string.h>

/* Floors span roughly -130..-30 dBm; the histogram covers every int8 value. */
#define RG_SPECTRUM_BINS 256

static int8_t rg_spectrum_round(float dbm) {
    if(dbm <= RG_SCAN_RSSI_NONE) return RG_SPECTRUM_NONE + 1; // clamp, keep NONE reserved
    float r = dbm + (dbm < 0 ? -0.5f : 0.5f);
    if(r <= -127.0f) return -127;
    if(r >= 127.0f) return 127;
    return (int8_t)r;
}

void rg_spectrum_from_channel(RgSpectrumPoint* out, const RgScanChannel* ch) {
    out->flags = 0;
    out->hits = ch->hits > 255u ? 255u : (uint8_t)ch->hits;
    if(ch->samples == 0) {
        out->last = RG_SPECTRUM_NONE;
        out->peak = RG_SPECTRUM_NONE;
        out->floor = RG_SPECTRUM_NONE;
        return;
    }
    out->flags |= RgSpectrumMeasured;
    out->last = rg_spectrum_round(ch->last);
    out->peak = rg_spectrum_round(ch->peak);
    out->floor = rg_spectrum_round(ch->floor);
    if(ch->samples >= RG_SCAN_WARMUP_SAMPLES) out->flags |= RgSpectrumWarm;
    if(ch->active) out->flags |= RgSpectrumActive;
}

int8_t rg_spectrum_median_floor(const RgSpectrumPoint* points, size_t count) {
    uint16_t hist[RG_SPECTRUM_BINS];
    memset(hist, 0, sizeof(hist));
    size_t n = 0;
    for(size_t i = 0; i < count; i++) {
        if(!(points[i].flags & RgSpectrumWarm)) continue;
        hist[(uint8_t)(points[i].floor + 128)]++;
        n++;
    }
    if(n == 0) return RG_SPECTRUM_NONE;
    // Lower median: the (n - 1) / 2-th value in ascending order.
    size_t want = (n - 1) / 2;
    size_t seen = 0;
    for(int b = 0; b < RG_SPECTRUM_BINS; b++) {
        seen += hist[b];
        if(seen > want) return (int8_t)(b - 128);
    }
    return RG_SPECTRUM_NONE;
}

size_t rg_spectrum_strongest(const RgSpectrumPoint* points, size_t count) {
    size_t best = count;
    for(size_t i = 0; i < count; i++) {
        if(!(points[i].flags & RgSpectrumMeasured)) continue;
        if(best == count || points[i].peak > points[best].peak) best = i;
    }
    return best;
}

void rg_spectrum_columns(
    const RgSpectrumPoint* points,
    size_t count,
    size_t width,
    int8_t* col_last,
    int8_t* col_peak,
    uint8_t* col_flags) {
    for(size_t c = 0; c < width; c++) {
        col_last[c] = RG_SPECTRUM_NONE;
        col_peak[c] = RG_SPECTRUM_NONE;
        col_flags[c] = 0;
        if(count == 0) continue;
        size_t from = c * count / width;
        size_t to = (c + 1) * count / width;
        if(to <= from) to = from + 1;
        if(to > count) to = count;
        for(size_t i = from; i < to; i++) {
            const RgSpectrumPoint* p = &points[i];
            col_flags[c] |= p->flags;
            if(!(p->flags & RgSpectrumMeasured)) continue;
            if(col_last[c] == RG_SPECTRUM_NONE || p->last > col_last[c]) col_last[c] = p->last;
            if(col_peak[c] == RG_SPECTRUM_NONE || p->peak > col_peak[c]) col_peak[c] = p->peak;
        }
    }
}

size_t rg_spectrum_column_of(size_t index, size_t count, size_t width) {
    if(count == 0 || width == 0) return 0;
    if(index >= count) index = count - 1;
    // The columns rg_spectrum_columns() draws the point in, by the same rule.
    size_t first = width, last = 0;
    for(size_t c = 0; c < width; c++) {
        size_t from = c * count / width;
        size_t to = (c + 1) * count / width;
        if(to <= from) to = from + 1;
        if(index >= from && index < to) {
            if(first == width) first = c;
            last = c;
        }
    }
    return first == width ? width - 1 : (first + last) / 2;
}

#endif /* RG_FEATURE_RANGE_SCAN */
