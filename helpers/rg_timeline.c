#include "rg_timeline.h"

const uint32_t rg_timeline_zoom_us[RG_TL_ZOOM_COUNT] =
    {5, 10, 20, 50, 100, 200, 500, 1000, 2000, 5000};

static uint32_t rg_tl_abs(int32_t v) {
    return (uint32_t)(v < 0 ? -(int64_t)v : v);
}

void rg_timeline_raster(
    const int32_t* samples,
    size_t count,
    uint64_t first_us,
    uint64_t left_us,
    uint32_t us_per_px,
    uint8_t* cols,
    size_t width) {
    for(size_t c = 0; c < width; c++)
        cols[c] = 0;
    if(us_per_px == 0 || width == 0) return;
    uint64_t right_us = left_us + (uint64_t)us_per_px * width;
    uint64_t t = first_us;
    for(size_t i = 0; i < count && t < right_us; i++) {
        uint32_t d = rg_tl_abs(samples[i]);
        uint64_t end = t + d;
        if(d > 0 && end > left_us) {
            uint64_t from = t > left_us ? t : left_us;
            size_t c0 = (size_t)((from - left_us) / us_per_px);
            size_t c1 = (size_t)((end - 1 - left_us) / us_per_px);
            if(c1 >= width) c1 = width - 1;
            uint8_t flag = samples[i] > 0 ? RG_TL_HIGH : RG_TL_LOW;
            for(size_t c = c0; c <= c1; c++)
                cols[c] |= flag;
        }
        t = end;
    }
}

size_t rg_timeline_labels(
    const int32_t* samples,
    size_t count,
    uint64_t first_us,
    uint64_t left_us,
    uint32_t us_per_px,
    size_t width,
    uint32_t min_px,
    RgTlLabel* out,
    size_t max) {
    size_t n = 0;
    if(us_per_px == 0) return 0;
    uint64_t right_us = left_us + (uint64_t)us_per_px * width;
    uint64_t t = first_us;
    for(size_t i = 0; i < count && t < right_us && n < max; i++) {
        uint32_t d = rg_tl_abs(samples[i]);
        if(t >= left_us && t + d <= right_us && d / us_per_px >= min_px) {
            out[n].x = (int16_t)((t + d / 2 - left_us) / us_per_px);
            out[n].high = samples[i] > 0;
            out[n].us = d;
            n++;
        }
        t += d;
    }
    return n;
}

uint64_t rg_timeline_clamp(uint64_t left_us, uint64_t span_us, uint64_t total_us) {
    if(total_us <= span_us) return 0;
    return left_us > total_us - span_us ? total_us - span_us : left_us;
}

uint64_t
    rg_timeline_pan(uint64_t left_us, int dir, uint32_t us_per_px, size_t width, uint64_t total_us) {
    uint64_t span = (uint64_t)us_per_px * width;
    uint64_t step = span / 4;
    if(dir < 0) {
        left_us = left_us > step ? left_us - step : 0;
    } else {
        left_us += step;
    }
    return rg_timeline_clamp(left_us, span, total_us);
}

bool rg_timeline_zoom(int* zoom, int dir, uint64_t* left_us, size_t width, uint64_t total_us) {
    int next = *zoom + dir;
    if(next < 0 || next >= RG_TL_ZOOM_COUNT) return false;
    uint64_t old_span = (uint64_t)rg_timeline_zoom_us[*zoom] * width;
    uint64_t new_span = (uint64_t)rg_timeline_zoom_us[next] * width;
    uint64_t centre = *left_us + old_span / 2;
    uint64_t left = centre > new_span / 2 ? centre - new_span / 2 : 0;
    *zoom = next;
    *left_us = rg_timeline_clamp(left, new_span, total_us);
    return true;
}

int rg_timeline_zoom_for(uint64_t detail_us, size_t width) {
    for(int z = 0; z < RG_TL_ZOOM_COUNT; z++) {
        if((uint64_t)rg_timeline_zoom_us[z] * width >= detail_us) return z;
    }
    return RG_TL_ZOOM_COUNT - 1;
}

bool rg_timeline_covered(
    uint64_t win_first_us,
    uint64_t win_end_us,
    bool at_end,
    uint64_t left_us,
    uint64_t span_us) {
    bool start_ok = win_first_us <= left_us;
    bool end_ok = at_end || left_us + span_us <= win_end_us;
    return start_ok && end_ok;
}

size_t
    rg_timeline_frame_at(const uint64_t* starts, size_t count, uint64_t left_us, uint64_t span_us) {
    uint64_t marker = left_us + span_us / 8;
    size_t at = count;
    for(size_t i = 0; i < count; i++) {
        if(starts[i] <= marker) at = i;
    }
    return at;
}

size_t rg_timeline_frame_step(
    const uint64_t* starts,
    size_t count,
    uint64_t left_us,
    uint64_t span_us,
    int dir,
    uint64_t* new_left) {
    uint64_t marker = left_us + span_us / 8;
    size_t found = count;
    if(dir > 0) {
        for(size_t i = 0; i < count; i++) {
            if(starts[i] > marker) {
                found = i;
                break;
            }
        }
    } else {
        for(size_t i = count; i > 0; i--) {
            if(starts[i - 1] < marker) {
                found = i - 1;
                break;
            }
        }
    }
    if(found < count) {
        uint64_t s = starts[found];
        *new_left = s > span_us / 8 ? s - span_us / 8 : 0;
    }
    return found;
}
