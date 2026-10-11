#include "rg_studio.h"
#include "../radiogeddon_edition.h"

#if RG_FEATURE_PULSE_STUDIO

#include <string.h>

static uint32_t rg_studio_abs(int32_t v) {
    return v < 0 ? (uint32_t)(-(int64_t)v) : (uint32_t)v;
}

void rg_studio_load(
    RgStudioFrame* f,
    const int32_t* samples,
    size_t n,
    RgEncoding enc,
    const RgDecodeParams* params) {
    memset(f, 0, sizeof(*f));
    f->te_us = params ? params->te_us : 0;
    f->encoding = (uint8_t)enc;
    for(size_t i = 0; i < n; i++) {
        int32_t v = samples[i];
        if(v == 0) continue;
        if(f->n > 0 && (f->s[f->n - 1] > 0) == (v > 0)) {
            int64_t sum = (int64_t)f->s[f->n - 1] + v;
            if(sum > INT32_MAX) sum = INT32_MAX;
            if(sum < -INT32_MAX) sum = -INT32_MAX;
            f->s[f->n - 1] = (int32_t)sum;
        } else if(f->n < RG_STUDIO_MAX_SAMPLES) {
            f->s[f->n++] = v;
        } else {
            break;
        }
    }
    for(uint16_t i = 0; i < f->n; i++)
        f->span_us += rg_studio_abs(f->s[i]);
    // Preamble: a leading run of single-Te cells, as the analyzer counts it.
    if(f->te_us) {
        uint16_t run = 0;
        while(run < f->n) {
            uint32_t d = rg_studio_abs(f->s[run]);
            if(d * 2u < f->te_us || d * 2u > f->te_us * 3u) break;
            run++;
        }
        f->preamble = run >= 8u ? run : 0u;
    }
    if(params && enc != RgEncodingUnknown) {
        f->nbits = (uint16_t)rg_analyzer_decode_map(
            enc, params, f->s, f->n, f->bits, RG_ANALYZER_MAX_BITS, &f->fit, f->map);
    }
    f->bits[f->nbits] = '\0';
}

bool rg_studio_bit_span(const RgStudioFrame* f, uint16_t bit, uint16_t* first, uint16_t* end) {
    if(bit >= f->nbits) return false;
    uint16_t a = f->map[bit] & RG_BITMAP_INDEX;
    if(a >= f->n) return false;
    // A symbol is one pulse and one gap (PWM, PPM) or two half-cells
    // (Manchester): at most two samples, and never into the next bit's.
    uint16_t b = (uint16_t)(a + 2u);
    if(bit + 1u < f->nbits) {
        uint16_t next = f->map[bit + 1] & RG_BITMAP_INDEX;
        if(next < b) b = next;
    }
    if(b > f->n) b = f->n;
    if(b <= a) b = (uint16_t)(a + 1u);
    *first = a;
    *end = b;
    return true;
}

uint16_t rg_studio_bit_of(const RgStudioFrame* f, uint16_t i, bool* unsure) {
    if(unsure) *unsure = false;
    if(i >= f->n || f->nbits == 0) return RG_STUDIO_NO_BIT;
    // The last bit whose symbol starts at or before i (map is ascending).
    uint16_t lo = 0, hi = f->nbits;
    while(hi - lo > 1) {
        uint16_t mid = (uint16_t)((lo + hi) / 2u);
        if((f->map[mid] & RG_BITMAP_INDEX) <= i)
            lo = mid;
        else
            hi = mid;
    }
    if((f->map[lo] & RG_BITMAP_INDEX) > i) return RG_STUDIO_NO_BIT; // before the data
    uint16_t first, end;
    if(!rg_studio_bit_span(f, lo, &first, &end) || i >= end) return RG_STUDIO_NO_BIT;
    if(unsure) *unsure = (f->map[lo] & RG_BITMAP_UNSURE) != 0;
    return lo;
}

uint64_t rg_studio_time_at(const RgStudioFrame* f, uint16_t i) {
    uint64_t t = 0;
    for(uint16_t k = 0; k < i && k < f->n; k++)
        t += rg_studio_abs(f->s[k]);
    return t;
}

uint16_t rg_studio_sample_at(const RgStudioFrame* f, uint64_t t) {
    uint64_t at = 0;
    for(uint16_t k = 0; k < f->n; k++) {
        at += rg_studio_abs(f->s[k]);
        if(t < at) return k;
    }
    return f->n ? (uint16_t)(f->n - 1u) : 0;
}

bool rg_studio_glitch(const RgStudioFrame* f, uint16_t i) {
    return i < f->n && f->te_us && rg_studio_abs(f->s[i]) * 2u < f->te_us;
}

static const uint32_t rg_studio_zooms[RG_STUDIO_ZOOMS] = {10, 20, 50, 100, 200, 500, 1000, 2000};

uint32_t rg_studio_zoom_us(uint8_t z) {
    return rg_studio_zooms[z < RG_STUDIO_ZOOMS ? z : RG_STUDIO_ZOOMS - 1];
}

uint8_t rg_studio_zoom_fit(const RgStudioFrame* f, uint16_t cols) {
    for(uint8_t z = 0; z < RG_STUDIO_ZOOMS; z++)
        if((uint64_t)rg_studio_zooms[z] * cols >= f->span_us) return z;
    return RG_STUDIO_ZOOMS - 1;
}

uint64_t rg_studio_pan(
    const RgStudioFrame* f,
    uint64_t start_us,
    uint8_t z,
    uint16_t cols,
    uint16_t cursor) {
    uint64_t width = (uint64_t)rg_studio_zoom_us(z) * cols;
    uint64_t a = rg_studio_time_at(f, cursor);
    uint64_t b = a + (cursor < f->n ? rg_studio_abs(f->s[cursor]) : 0);
    if(b - a > width) b = a + width; // a pulse wider than the screen: show its start
    if(a < start_us) start_us = a;
    if(b > start_us + width) start_us = b - width;
    // Never past the end of the frame.
    if(f->span_us > width && start_us > f->span_us - width) start_us = f->span_us - width;
    if(f->span_us <= width) start_us = 0;
    return start_us;
}

void rg_studio_hist(
    const RgStudioFrame* f,
    uint8_t high[RG_STUDIO_HIST],
    uint8_t low[RG_STUDIO_HIST]) {
    memset(high, 0, RG_STUDIO_HIST);
    memset(low, 0, RG_STUDIO_HIST);
    if(!f->te_us) return;
    for(uint16_t i = 0; i < f->n; i++) {
        uint32_t d = rg_studio_abs(f->s[i]);
        // Bucket k holds (k + 0.75 .. k + 1.25) half-Te steps, rounded.
        uint32_t halves = (d * 4u + f->te_us) / (2u * f->te_us);
        size_t k = halves == 0 ? 0 : halves - 1u;
        if(k >= RG_STUDIO_HIST) k = RG_STUDIO_HIST - 1;
        uint8_t* h = f->s[i] > 0 ? high : low;
        if(h[k] < 255) h[k]++;
    }
}

size_t rg_studio_diff(const RgStudioFrame* f, const char* ref, size_t ref_n, uint8_t* diff_mask) {
    size_t n = f->nbits, d = 0;
    memset(diff_mask, 0, (RG_ANALYZER_MAX_BITS + 7) / 8);
    for(size_t i = 0; i < n; i++) {
        bool differs = i >= ref_n || ref[i] != f->bits[i];
        if(differs) {
            diff_mask[i / 8] |= (uint8_t)(1u << (i % 8));
            d++;
        }
    }
    return d;
}

#endif /* RG_FEATURE_PULSE_STUDIO */
