#include "rg_range.h"
#include "../radiogeddon_edition.h"

#if RG_EDITION_FULL

const RgBand rg_range_cc1101_bands[3] = {
    {281000000UL, 361000000UL},
    {378000000UL, 481000000UL},
    {749000000UL, 962000000UL},
};

#define RG_RANGE_ANCHORS 16u

static uint32_t rg_range_khz_up(uint32_t hz) {
    uint64_t v = ((uint64_t)hz + 999u) / 1000u * 1000u;
    return v > UINT32_MAX ? hz : (uint32_t)v;
}

static uint32_t rg_range_khz_down(uint32_t hz) {
    return hz / 1000u * 1000u;
}

static void rg_range_sort(RgBandSet* set) {
    // Insertion sort (at most RG_RANGE_MAX_BANDS entries), then merge overlaps.
    for(size_t i = 1; i < set->count; i++) {
        RgBand b = set->band[i];
        size_t j = i;
        while(j > 0 && set->band[j - 1].lo_hz > b.lo_hz) {
            set->band[j] = set->band[j - 1];
            j--;
        }
        set->band[j] = b;
    }
    size_t out = 0;
    for(size_t i = 0; i < set->count; i++) {
        if(out > 0 && set->band[i].lo_hz <= set->band[out - 1].hi_hz) {
            if(set->band[i].hi_hz > set->band[out - 1].hi_hz) {
                set->band[out - 1].hi_hz = set->band[i].hi_hz;
            }
        } else {
            set->band[out++] = set->band[i];
        }
    }
    set->count = out;
}

void rg_range_probe_bands(
    const RgBand* candidates,
    size_t count,
    RgRangeValidFn valid,
    void* context,
    RgBandSet* out) {
    out->count = 0;
    for(size_t c = 0; c < count && out->count < RG_RANGE_MAX_BANDS; c++) {
        uint32_t lo = candidates[c].lo_hz;
        uint32_t hi = candidates[c].hi_hz;
        if(lo > hi) continue;

        // A point the radio accepts: the middle first, then evenly spaced ones.
        uint32_t anchor = 0;
        bool found = false;
        uint64_t span = (uint64_t)hi - lo;
        uint32_t mid = lo + (uint32_t)(span / 2u);
        if(valid(mid, context)) {
            anchor = mid;
            found = true;
        }
        for(uint32_t i = 0; !found && i < RG_RANGE_ANCHORS; i++) {
            uint32_t f = lo + (uint32_t)(span * i / (RG_RANGE_ANCHORS - 1u));
            if(f != mid && valid(f, context)) {
                anchor = f;
                found = true;
            }
        }
        if(!found) continue;

        // Lowest accepted frequency: valid(edge_hi) holds, valid(edge_lo) not.
        uint32_t bottom = lo;
        if(!valid(lo, context)) {
            uint32_t a = lo, b = anchor;
            while(b - a > 1u) {
                uint32_t m = a + (b - a) / 2u;
                if(valid(m, context)) {
                    b = m;
                } else {
                    a = m;
                }
            }
            bottom = b;
        }
        uint32_t top = hi;
        if(!valid(hi, context)) {
            uint32_t a = anchor, b = hi;
            while(b - a > 1u) {
                uint32_t m = a + (b - a) / 2u;
                if(valid(m, context)) {
                    a = m;
                } else {
                    b = m;
                }
            }
            top = a;
        }
        out->band[out->count].lo_hz = bottom;
        out->band[out->count].hi_hz = top;
        out->count++;
    }
    rg_range_sort(out);
}

bool rg_range_in_bands(const RgBandSet* bands, uint32_t hz) {
    for(size_t i = 0; i < bands->count; i++) {
        if(hz >= bands->band[i].lo_hz && hz <= bands->band[i].hi_hz) return true;
    }
    return false;
}

const char* rg_range_result_text(RgRangeResult result) {
    switch(result) {
    case RgRangeOk:
        return "OK";
    case RgRangeErrorOrder:
        return "Start above end";
    case RgRangeErrorStep:
        return "Bad step";
    case RgRangeErrorNoPoints:
        return "No tunable points";
    case RgRangeErrorTooMany:
        return "Too many points";
    }
    return "Error";
}

RgRangeResult rg_range_plan(
    RgRange* range,
    uint32_t start_hz,
    uint32_t end_hz,
    uint32_t step_hz,
    const RgBandSet* bands,
    uint32_t max_points) {
    range->start_hz = start_hz;
    range->end_hz = end_hz;
    range->step_hz = step_hz;
    range->segments = 0;
    range->points = 0;
    range->grid_points = 0;

    if(step_hz < RG_RANGE_MIN_STEP_HZ || step_hz > RG_RANGE_MAX_STEP_HZ) return RgRangeErrorStep;
    if(start_hz > end_hz) return RgRangeErrorOrder;

    range->grid_points = (end_hz - start_hz) / step_hz + 1u;

    uint64_t points = 0;
    for(size_t i = 0; i < bands->count && range->segments < RG_RANGE_MAX_BANDS; i++) {
        uint32_t a = bands->band[i].lo_hz > start_hz ? bands->band[i].lo_hz : start_hz;
        uint32_t b = bands->band[i].hi_hz < end_hz ? bands->band[i].hi_hz : end_hz;
        if(a > b) continue;
        uint32_t k_first = (uint32_t)(((uint64_t)a - start_hz + step_hz - 1u) / step_hz);
        uint32_t k_last = (b - start_hz) / step_hz;
        if(k_first > k_last) continue; // the band is narrower than one step here
        RgRangeSegment* seg = &range->segment[range->segments++];
        seg->first_k = k_first;
        seg->count = k_last - k_first + 1u;
        points += seg->count;
    }
    range->points = points > UINT32_MAX ? UINT32_MAX : (uint32_t)points;

    if(range->points == 0) return RgRangeErrorNoPoints;
    if(range->points > max_points) return RgRangeErrorTooMany;
    return RgRangeOk;
}

uint32_t rg_range_frequency(const RgRange* range, uint32_t index) {
    for(size_t i = 0; i < range->segments; i++) {
        const RgRangeSegment* seg = &range->segment[i];
        if(index < seg->count) {
            return range->start_hz + (seg->first_k + index) * range->step_hz;
        }
        index -= seg->count;
    }
    return 0;
}

uint32_t rg_range_nearest_index(const RgRange* range, uint32_t hz) {
    uint32_t best = 0;
    uint32_t best_dist = UINT32_MAX;
    uint32_t base = 0;
    for(size_t i = 0; i < range->segments; i++) {
        const RgRangeSegment* seg = &range->segment[i];
        uint32_t k = seg->first_k;
        if(hz > range->start_hz) {
            uint32_t want = (uint32_t)(((uint64_t)hz - range->start_hz + range->step_hz / 2u) /
                                       range->step_hz);
            if(want > k) k = want;
        }
        if(k > seg->first_k + seg->count - 1u) k = seg->first_k + seg->count - 1u;
        uint32_t f = range->start_hz + k * range->step_hz;
        uint32_t dist = f > hz ? f - hz : hz - f;
        if(dist < best_dist) {
            best_dist = dist;
            best = base + (k - seg->first_k);
        }
        base += seg->count;
    }
    return best;
}

uint32_t rg_range_sweep_ms(uint32_t points, uint32_t dwell_ms) {
    uint64_t ms = (uint64_t)points * ((uint64_t)dwell_ms + RG_RANGE_PROBE_OVERHEAD_MS);
    return ms > UINT32_MAX ? UINT32_MAX : (uint32_t)ms;
}

uint32_t rg_range_step(const RgBandSet* bands, uint32_t hz, int32_t delta_hz) {
    if(bands->count == 0) return hz;

    if(!rg_range_in_bands(bands, hz)) {
        // Snap to the nearest band edge first.
        uint32_t best = hz;
        uint32_t best_dist = UINT32_MAX;
        for(size_t i = 0; i < bands->count; i++) {
            uint32_t lo = rg_range_khz_up(bands->band[i].lo_hz);
            uint32_t hi = rg_range_khz_down(bands->band[i].hi_hz);
            uint32_t d_lo = lo > hz ? lo - hz : hz - lo;
            uint32_t d_hi = hi > hz ? hi - hz : hz - hi;
            if(d_lo < best_dist) {
                best_dist = d_lo;
                best = lo;
            }
            if(d_hi < best_dist) {
                best_dist = d_hi;
                best = hi;
            }
        }
        hz = best;
    }
    if(delta_hz == 0) return hz;

    int64_t target = (int64_t)hz + delta_hz;
    if(target >= 0 && target <= (int64_t)UINT32_MAX &&
       rg_range_in_bands(bands, (uint32_t)target)) {
        return (uint32_t)target;
    }
    if(delta_hz > 0) {
        for(size_t i = 0; i < bands->count; i++) {
            if((int64_t)bands->band[i].lo_hz > target)
                return rg_range_khz_up(bands->band[i].lo_hz);
        }
        return rg_range_khz_down(bands->band[bands->count - 1].hi_hz);
    }
    for(size_t i = bands->count; i > 0; i--) {
        if((int64_t)bands->band[i - 1].hi_hz < target) {
            return rg_range_khz_down(bands->band[i - 1].hi_hz);
        }
    }
    return rg_range_khz_up(bands->band[0].lo_hz);
}

#endif /* RG_EDITION_FULL */
