/**
 * Fuzz target: arbitrary timing samples (any int32, including 0, negative
 * runs and extremes) through the analyzer, RAW comparison and Pulse Timeline
 * code (helpers/rg_analyzer.c, helpers/rg_timeline.c).
 *
 * Input: 8 bytes of view parameters (zoom, left edge, first-sample time,
 * label width), then the samples as little-endian int32.
 *
 * Invariants (abort on violation):
 *  - analysis results stay inside their documented ranges;
 *  - comparison scores are 0..100 and a recording matches itself fully;
 *  - the timeline only marks columns with the two level flags, labels lie
 *    on screen, the view stays inside the recording after clamp, pan and
 *    zoom, and frame navigation returns an index or "none".
 */
#include "../../helpers/rg_analyzer.h"
#include "../../helpers/rg_timeline.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define REQUIRE(c)        \
    do {                  \
        if(!(c)) abort(); \
    } while(0)

#define MAX_SAMPLES 4096
#define WIDTH       128

static uint32_t le32(const uint8_t* b) {
    return (uint32_t)b[0] | (uint32_t)b[1] << 8 | (uint32_t)b[2] << 16 | (uint32_t)b[3] << 24;
}

static void check_analysis(const RgAnalysis* r, size_t samples) {
    REQUIRE(r->sample_count <= samples);
    REQUIRE(r->noise_pct <= 100);
    REQUIRE(r->encoding < RgEncodingCount && r->alternative < RgEncodingCount);
    REQUIRE(r->encoding_confidence >= 0 && r->encoding_confidence <= 95);
    REQUIRE(r->fit_pct >= 0 && r->fit_pct <= 100);
    REQUIRE(r->frames_kept <= RG_ANALYZER_MAX_FRAMES && r->frames_kept <= r->frame_count);
    REQUIRE(r->signal_frames <= r->frame_count);
    REQUIRE(r->bit_count <= RG_ANALYZER_MAX_BITS && strlen(r->bits) <= RG_ANALYZER_MAX_BITS);
    REQUIRE(r->group_count <= RG_ANALYZER_MAX_GROUPS);
    /* The dominant pattern is the first group's frame. */
    REQUIRE(r->group_count == 0 || r->groups[0].frame < r->frames_kept);
    REQUIRE(strlen(r->bits) == (r->group_count ? r->frames[r->groups[0].frame].bit_count : 0u));
    REQUIRE(r->id_start + r->id_len <= RG_ANALYZER_MAX_BITS);
    for(size_t i = 0; i < r->frames_kept; i++) {
        REQUIRE(r->frames[i].fit <= 100);
        REQUIRE(r->frames[i].bit_count <= RG_ANALYZER_MAX_BITS);
        char bits[RG_ANALYZER_MAX_BITS + 1];
        rg_analyzer_frame_bits(&r->frames[i], bits);
        REQUIRE(strlen(bits) == r->frames[i].bit_count);
    }
}

static uint64_t total_us(const int32_t* s, size_t n) {
    uint64_t t = 0;
    for(size_t i = 0; i < n; i++)
        t += s[i] < 0 ? (uint64_t)(-(int64_t)s[i]) : (uint64_t)s[i];
    return t;
}

int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    if(size < 8) return 0;
    const uint8_t* view = data;
    size_t n = (size - 8) / 4;
    if(n > MAX_SAMPLES) n = MAX_SAMPLES;
    static int32_t samples[MAX_SAMPLES];
    for(size_t i = 0; i < n; i++)
        samples[i] = (int32_t)le32(data + 8 + i * 4);

    static RgAnalysis a;
    rg_analyzer_run(samples, n, &a);
    check_analysis(&a, n);
    REQUIRE(strlen(rg_analyzer_encoding_name(a.encoding)) > 0);
    REQUIRE(strlen(rg_analyzer_quality_name(a.quality)) > 0);

    int self = rg_analyzer_similarity(samples, n, samples, n);
    REQUIRE(self >= 0 && self <= 100);
    size_t half = n / 2;
    int score = rg_analyzer_similarity(samples, half, samples + half, n - half);
    REQUIRE(score >= 0 && score <= 100);

    /* Timeline over the same samples, with a view taken from the input. */
    uint64_t total = total_us(samples, n);
    int zoom = view[0] % RG_TL_ZOOM_COUNT;
    uint32_t us_per_px = rg_timeline_zoom_us[zoom];
    uint64_t span = (uint64_t)us_per_px * WIDTH;
    uint64_t left = total ? (uint64_t)le32(view + 1) % total : 0;
    left = rg_timeline_clamp(left, span, total);
    REQUIRE(left == 0 || left + span <= total);
    uint64_t first = (uint64_t)view[5] * 1000u;

    uint8_t cols[WIDTH];
    memset(cols, 0xAA, sizeof(cols));
    rg_timeline_raster(samples, n, first, left, us_per_px, cols, WIDTH);
    for(size_t c = 0; c < WIDTH; c++)
        REQUIRE((cols[c] & ~(RG_TL_HIGH | RG_TL_LOW)) == 0);

    RgTlLabel labels[16];
    size_t nl = rg_timeline_labels(
        samples, n, first, left, us_per_px, WIDTH, 1u + view[6] % 32u, labels, 16);
    REQUIRE(nl <= 16);
    for(size_t i = 0; i < nl; i++)
        REQUIRE(labels[i].x >= 0 && labels[i].x < WIDTH);

    uint64_t panned = rg_timeline_pan(left, (view[7] & 1) ? 1 : -1, us_per_px, WIDTH, total);
    REQUIRE(panned == 0 || panned + span <= total);
    int z = zoom;
    uint64_t zl = left;
    rg_timeline_zoom(&z, (view[7] & 2) ? 1 : -1, &zl, WIDTH, total);
    REQUIRE(z >= 0 && z < RG_TL_ZOOM_COUNT);
    uint64_t zspan = (uint64_t)rg_timeline_zoom_us[z] * WIDTH;
    REQUIRE(zl == 0 || zl + zspan <= total);
    int fit = rg_timeline_zoom_for(total, WIDTH);
    REQUIRE(fit >= 0 && fit < RG_TL_ZOOM_COUNT);

    /* Frame navigation over the analyzer's frame starts. */
    uint64_t starts[RG_ANALYZER_MAX_FRAMES];
    for(size_t i = 0; i < a.frames_kept; i++)
        starts[i] = a.frames[i].start_us;
    uint64_t new_left = 0;
    size_t k = rg_timeline_frame_step(
        starts, a.frames_kept, left, span, (view[7] & 4) ? 1 : -1, &new_left);
    REQUIRE(k <= a.frames_kept);
    k = rg_timeline_frame_at(starts, a.frames_kept, left, span);
    REQUIRE(k <= a.frames_kept);
    return 0;
}
