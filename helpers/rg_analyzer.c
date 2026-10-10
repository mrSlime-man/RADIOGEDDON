#include "rg_analyzer.h"

#include <stdlib.h>
#include <string.h>

/* ---- small helpers ------------------------------------------------------ */

static uint32_t rg_abs32(int32_t v) {
    return (uint32_t)(v < 0 ? -(int64_t)v : v);
}

/* |value - center| <= pct% of center (plus 10 us so tiny pulses can match). */
static bool rg_near(uint32_t value, uint32_t center, uint32_t pct) {
    if(center == 0) return false;
    uint32_t diff = value > center ? value - center : center - value;
    return diff <= (uint32_t)(((uint64_t)center * pct) / 100u) + 10u;
}

static void rg_pack_bits(const char* bits, size_t n, uint8_t* out) {
    memset(out, 0, RG_ANALYZER_MAX_BITS / 8);
    for(size_t i = 0; i < n && i < RG_ANALYZER_MAX_BITS; i++) {
        if(bits[i] == '1') out[i / 8] |= (uint8_t)(0x80u >> (i % 8));
    }
}

void rg_analyzer_frame_bits(const RgFrame* frame, char* out) {
    size_t n = frame->bit_count;
    if(n > RG_ANALYZER_MAX_BITS) n = RG_ANALYZER_MAX_BITS;
    for(size_t i = 0; i < n; i++) {
        out[i] = (frame->bits[i / 8] & (0x80u >> (i % 8))) ? '1' : '0';
    }
    out[n] = '\0';
}

/* ---- log-spaced histogram ----------------------------------------------- */

/* Lower edge of each bin: 20 us, then +10% per bin (about 137 ms at the top). */
static uint32_t rg_bin_edges[RG_ANALYZER_HIST_BINS];
static bool rg_bin_edges_ready = false;

static void rg_bins_init(void) {
    if(rg_bin_edges_ready) return;
    uint32_t e = 20;
    for(size_t i = 0; i < RG_ANALYZER_HIST_BINS; i++) {
        rg_bin_edges[i] = e;
        uint32_t step = e / 10u;
        e += step ? step : 1u;
    }
    rg_bin_edges_ready = true;
}

static size_t rg_bin_of(uint32_t us) {
    /* Last bin whose lower edge is <= us (binary search). */
    size_t lo = 0, hi = RG_ANALYZER_HIST_BINS;
    while(hi - lo > 1) {
        size_t mid = (lo + hi) / 2;
        if(rg_bin_edges[mid] <= us)
            lo = mid;
        else
            hi = mid;
    }
    return lo;
}

static void rg_hist_add(RgHistogram* h, uint32_t us) {
    size_t b = rg_bin_of(us);
    h->count[b]++;
    h->sum[b] += us;
    h->total++;
}

/*
 * Peak detection. Counts are smoothed with a [1 2 1] kernel; each local
 * maximum (tallest first) claims the bins around it while the smoothed height
 * keeps falling and stays above a quarter of the peak. A peak must drop to
 * half its height on both sides (prominence) so ripples inside a broad noise
 * hump are not mistaken for timing clusters, must hold at least 2% of the
 * side's edges, and must span at most RG_PEAK_MAX_WIDTH bins (about +-45%),
 * otherwise its edges count as noise.
 */
#define RG_PEAK_MAX_WIDTH 8

typedef struct {
    RgPeak peak;
    uint64_t deviation; /* sum over bins of count * |bin mean - centre| */
} RgPeakWork;

static size_t rg_find_peaks(const RgHistogram* h, RgPeakWork* out, size_t max) {
    uint32_t smooth[RG_ANALYZER_HIST_BINS];
    bool claimed[RG_ANALYZER_HIST_BINS];
    const size_t bins = RG_ANALYZER_HIST_BINS;
    uint32_t total = 0;
    for(size_t i = 0; i < bins; i++) {
        uint32_t left = i > 0 ? h->count[i - 1] : 0;
        uint32_t right = (i + 1 < bins) ? h->count[i + 1] : 0;
        smooth[i] = left + 2u * h->count[i] + right;
        claimed[i] = false;
        total += h->count[i];
    }
    if(total == 0) return 0;

    size_t n = 0;
    while(n < max) {
        /* Tallest unclaimed bin. */
        size_t top = bins;
        for(size_t i = 0; i < bins; i++) {
            if(claimed[i] || smooth[i] == 0) continue;
            if(top == bins || smooth[i] > smooth[top]) top = i;
        }
        if(top == bins) break;
        uint32_t height = smooth[top];
        uint32_t floor = height / 4u;

        size_t lo = top, hi = top;
        while(lo > 0 && !claimed[lo - 1] && smooth[lo - 1] <= smooth[lo] && smooth[lo - 1] > floor)
            lo--;
        while(hi + 1 < bins && !claimed[hi + 1] && smooth[hi + 1] <= smooth[hi] &&
              smooth[hi + 1] > floor)
            hi++;
        for(size_t i = lo; i <= hi; i++)
            claimed[i] = true;

        /* Prominence: both flanks must fall to half height (or the edge). */
        uint32_t left_floor = lo > 0 ? smooth[lo - 1] : 0;
        uint32_t right_floor = hi + 1 < bins ? smooth[hi + 1] : 0;
        bool prominent = left_floor * 2u <= height && right_floor * 2u <= height;

        uint32_t count = 0;
        uint64_t sum = 0;
        for(size_t i = lo; i <= hi; i++) {
            count += h->count[i];
            sum += h->sum[i];
        }
        size_t width = hi - lo + 1;
        if(!prominent || count < 3 || (uint64_t)count * 50u < total || width > RG_PEAK_MAX_WIDTH)
            continue;

        RgPeakWork* w = &out[n++];
        w->peak.center_us = (uint32_t)(sum / count);
        w->peak.count = count;
        w->peak.width_bins = (uint8_t)width;
        w->deviation = 0;
        for(size_t i = lo; i <= hi; i++) {
            if(h->count[i] == 0) continue;
            uint32_t mean = (uint32_t)(h->sum[i] / h->count[i]);
            uint32_t d = mean > w->peak.center_us ? mean - w->peak.center_us :
                                                    w->peak.center_us - mean;
            w->deviation += (uint64_t)d * h->count[i];
        }
    }

    /* Ascending by duration (insertion sort; n is tiny). */
    for(size_t i = 1; i < n; i++) {
        RgPeakWork key = out[i];
        size_t j = i;
        while(j > 0 && out[j - 1].peak.center_us > key.peak.center_us) {
            out[j] = out[j - 1];
            j--;
        }
        out[j] = key;
    }
    return n;
}

/* The two most populous peaks at or above RG_ANALYZER_MIN_TE_US, returned as
 * (shorter, longer); 0 if missing. */
static void rg_top_two(const RgPeak* p, size_t n, uint32_t* shorter, uint32_t* longer) {
    size_t a = n, b = n;
    for(size_t i = 0; i < n; i++) {
        if(p[i].center_us < RG_ANALYZER_MIN_TE_US) continue;
        if(a == n || p[i].count > p[a].count) {
            b = a;
            a = i;
        } else if(b == n || p[i].count > p[b].count) {
            b = i;
        }
    }
    *shorter = 0;
    *longer = 0;
    if(a == n || b == n) return;
    uint32_t x = p[a].center_us, y = p[b].center_us;
    if(x > y) {
        uint32_t t = x;
        x = y;
        y = t;
    }
    /* Two widths must be clearly distinct to carry a bit. */
    if((uint64_t)y * 10u < (uint64_t)x * 14u) return;
    *shorter = x;
    *longer = y;
}

/* Shortest peak holding at least a quarter of the side's largest peak: a
 * sparse cluster of glitches below the real timing must not become Te. Peaks
 * below RG_ANALYZER_MIN_TE_US are receiver glitches however many there are. */
static uint32_t rg_shortest_major(const RgPeak* p, size_t n) {
    uint32_t most = 0;
    for(size_t i = 0; i < n; i++)
        if(p[i].center_us >= RG_ANALYZER_MIN_TE_US && p[i].count > most) most = p[i].count;
    for(size_t i = 0; i < n; i++)
        if(p[i].center_us >= RG_ANALYZER_MIN_TE_US && p[i].count * 4u >= most)
            return p[i].center_us;
    return 0;
}

/* ---- trial decoders ----------------------------------------------------- */

/*
 * PWM and PPM: one bit per high/low pair. Phase 0 pairs each high with the
 * low after it; phase 1 skips the frame's first high (a start pulse) and pairs
 * each low with the high after it, as protocols that send the low first do.
 */
static size_t rg_decode_pairs_phase(
    RgEncoding enc,
    const RgDecodeParams* p,
    const int32_t* s,
    size_t n,
    int phase,
    char* bits,
    size_t max_bits,
    int* fit) {
    size_t b = 0, pairs = 0, good = 0;
    uint32_t split, period = 0;
    if(enc == RgEncodingPWM) {
        split = (p->pwm_short_us + p->pwm_long_us) / 2u;
        period = p->pwm_short_us + p->pwm_long_us;
    } else {
        split = (p->ppm_short_us + p->ppm_long_us) / 2u;
    }

    size_t i = 0;
    while(i < n && s[i] <= 0)
        i++; /* a frame may open with the tail of the idle low */
    if(phase == 1) i++;
    for(; i + 1 < n; i += 2) {
        pairs++;
        int32_t hs = phase == 1 ? s[i + 1] : s[i];
        int32_t ls = phase == 1 ? s[i] : s[i + 1];
        if(hs <= 0 || ls >= 0) continue; /* not a high/low pair */
        uint32_t h = rg_abs32(hs);
        uint32_t l = rg_abs32(ls);
        bool ok;
        char bit;
        if(enc == RgEncodingPWM) {
            ok = (rg_near(h, p->pwm_short_us, 30) || rg_near(h, p->pwm_long_us, 30)) &&
                 rg_near(h + l, period, 25);
            bit = h > split ? '1' : '0';
        } else {
            ok = rg_near(h, p->ppm_high_us, 30) &&
                 (rg_near(l, p->ppm_short_us, 30) || rg_near(l, p->ppm_long_us, 30));
            bit = l > split ? '1' : '0';
        }
        if(ok) good++;
        if(bits && b < max_bits) bits[b] = bit;
        if(b < max_bits) b++;
    }
    if(bits) bits[b] = '\0';
    *fit = pairs ? (int)((good * 100u) / pairs) : 0;
    return b;
}

static size_t rg_decode_pairs(
    RgEncoding enc,
    const RgDecodeParams* p,
    const int32_t* s,
    size_t n,
    char* bits,
    size_t max_bits,
    int* fit) {
    int fit0 = 0, fit1 = 0;
    rg_decode_pairs_phase(enc, p, s, n, 0, NULL, max_bits, &fit0);
    rg_decode_pairs_phase(enc, p, s, n, 1, NULL, max_bits, &fit1);
    return rg_decode_pairs_phase(enc, p, s, n, fit1 > fit0 ? 1 : 0, bits, max_bits, fit);
}

/*
 * Manchester: every duration is one or two half-bit cells of its level. Cells
 * are paired; a pair must change level (a violation resynchronises on the
 * second cell). Phase 1 assumes the frame's first half-cell was swallowed by
 * the idle low before it.
 */
static size_t rg_decode_manchester_phase(
    const RgDecodeParams* p,
    const int32_t* s,
    size_t n,
    int phase,
    char* bits,
    size_t max_bits,
    size_t* valid,
    size_t* bad) {
    uint32_t te = p->te_us;
    bool have_half = phase == 1;
    bool half = false; /* level of the pending first half-cell */
    size_t b = 0;
    *valid = 0;
    *bad = 0;
    for(size_t i = 0; i < n; i++) {
        bool level = s[i] > 0;
        uint32_t d = rg_abs32(s[i]);
        size_t cells;
        if(d * 2u >= te && d * 2u <= te * 3u) {
            cells = 1;
        } else if(d * 2u > te * 3u && d * 10u <= te * 26u) {
            cells = 2;
        } else {
            (*bad)++; /* off the 1/2 Te grid: break the bit stream */
            have_half = false;
            continue;
        }
        for(size_t c = 0; c < cells; c++) {
            if(!have_half) {
                have_half = true;
                half = level;
            } else if(half != level) {
                (*valid)++;
                if(bits && b < max_bits) bits[b] = half ? '0' : '1';
                if(b < max_bits) b++;
                have_half = false;
            } else {
                (*bad)++;
                half = level; /* resync: this cell starts the next bit */
            }
        }
    }
    /* A final high half-cell completes against the idle low after the frame. */
    if(have_half && half) {
        (*valid)++;
        if(bits && b < max_bits) bits[b] = '0';
        if(b < max_bits) b++;
    }
    if(bits) bits[b] = '\0';
    return b;
}

static size_t rg_decode_manchester(
    const RgDecodeParams* p,
    const int32_t* s,
    size_t n,
    char* bits,
    size_t max_bits,
    int* fit) {
    size_t v0, b0, v1, b1;
    rg_decode_manchester_phase(p, s, n, 0, NULL, max_bits, &v0, &b0);
    rg_decode_manchester_phase(p, s, n, 1, NULL, max_bits, &v1, &b1);
    int phase = ((long)v1 - (long)b1 > (long)v0 - (long)b0) ? 1 : 0;
    size_t valid, bad;
    size_t nb = rg_decode_manchester_phase(p, s, n, phase, bits, max_bits, &valid, &bad);
    *fit = (valid + bad) ? (int)((valid * 100u) / (valid + bad)) : 0;
    return nb;
}

size_t rg_analyzer_decode(
    RgEncoding enc,
    const RgDecodeParams* params,
    const int32_t* samples,
    size_t count,
    char* bits,
    size_t max_bits,
    int* fit_pct) {
    int fit = 0;
    size_t n = 0;
    if(bits) bits[0] = '\0';
    if(samples && count > 0) {
        switch(enc) {
        case RgEncodingPWM:
            if(params->pwm_short_us && params->pwm_long_us)
                n = rg_decode_pairs(enc, params, samples, count, bits, max_bits, &fit);
            break;
        case RgEncodingPPM:
            if(params->ppm_high_us && params->ppm_short_us && params->ppm_long_us)
                n = rg_decode_pairs(enc, params, samples, count, bits, max_bits, &fit);
            break;
        case RgEncodingManchester:
            if(params->te_us)
                n = rg_decode_manchester(params, samples, count, bits, max_bits, &fit);
            break;
        default:
            break;
        }
    }
    if(fit_pct) *fit_pct = fit;
    return n;
}

/* ---- alignment ---------------------------------------------------------- */

size_t rg_analyzer_align(
    const char* a,
    size_t na,
    const char* b,
    size_t nb,
    int max_shift,
    int* shift,
    size_t* overlap) {
    long best_score = -1;
    size_t best_m = 0, best_ov = 0;
    int best_s = 0;
    for(int d = 0; d <= 2 * max_shift; d++) {
        /* Try 0, +1, -1, +2, -2 ... so ties prefer the smallest shift. */
        int s = (d % 2) ? (d + 1) / 2 : -(d / 2);
        size_t m = 0, ov = 0;
        size_t j = s < 0 ? (size_t)(-s) : 0;
        for(; j < nb; j++) {
            long ai = (long)j + s;
            if(ai < 0) continue;
            if((size_t)ai >= na) break;
            ov++;
            if(a[ai] == b[j]) m++;
        }
        long score = 2 * (long)m - (long)ov;
        if(ov > 0 && score > best_score) {
            best_score = score;
            best_m = m;
            best_ov = ov;
            best_s = s;
        }
    }
    if(shift) *shift = best_s;
    if(overlap) *overlap = best_ov;
    return best_m;
}

/* ---- streaming passes ---------------------------------------------------- */

static bool rg_repeats_at(const char* bits, size_t n, size_t p) {
    size_t match = 0;
    for(size_t i = 0; i + p < n; i++) {
        if(bits[i] == bits[i + p]) match++;
    }
    return match * 100u >= (n - p) * RG_ANALYZER_REPEAT_MATCH_PCT;
}

size_t rg_analyzer_repeat_period(const char* bits, size_t n) {
    // A stream that repeats at a few bits (a square wave, a preamble) is not
    // made of frames.
    for(size_t q = 1; q < RG_ANALYZER_MIN_REPEAT_BITS && q * 2 <= n; q++) {
        if(rg_repeats_at(bits, n, q)) return 0;
    }
    for(size_t p = RG_ANALYZER_MIN_REPEAT_BITS; p * 2 <= n; p++) {
        size_t ones = 0, changes = 0;
        for(size_t i = 0; i < p; i++) {
            if(bits[i] == '1') ones++;
            if(i > 0 && bits[i] != bits[i - 1]) changes++;
        }
        if(ones < 2 || p - ones < 2 || changes < 3) continue;
        if(rg_repeats_at(bits, n, p)) return p;
    }
    return 0;
}

static void rg_frame_reset(RgAnalyzer* a) {
    a->frame_n = 0;
    a->frame_samples = 0;
    a->frame_duration_us = 0;
}

static void rg_frame_end(RgAnalyzer* a) {
    RgAnalysis* r = &a->result;
    if(a->frame_samples == 0) return;
    if(a->frame_samples < RG_ANALYZER_MIN_FRAME_SAMPLES) {
        if(a->pass == 2) r->burst_count++;
        rg_frame_reset(a);
        return;
    }

    char bits[RG_ANALYZER_MAX_BITS + 1];
    if(a->pass == 2) {
        r->frame_count++;
        /* A frame of single Te cells only (a preamble or wake-up run) carries
         * no information about the encoding: every grammar that allows a
         * square wave fits it, Manchester best. It does not vote. */
        bool any_long = false;
        for(size_t i = 0; i < a->frame_n && !any_long; i++)
            any_long = (uint64_t)rg_abs32(a->frame[i]) * 2u > (uint64_t)r->params.te_us * 3u;
        if(!any_long) {
            rg_frame_reset(a);
            return;
        }
        int fits[RgEncodingCount] = {0};
        int best = 0;
        for(int e = RgEncodingPWM; e < RgEncodingCount; e++) {
            size_t nb = rg_analyzer_decode(
                (RgEncoding)e,
                &r->params,
                a->frame,
                a->frame_n,
                NULL,
                RG_ANALYZER_MAX_BITS,
                &fits[e]);
            if(nb < 4) fits[e] = 0; /* too short to carry information */
            if(fits[e] > best) best = fits[e];
        }
        if(best >= RG_ANALYZER_GOOD_FIT) {
            /* Weighted by length: a long frame is more evidence than a short
             * burst of noise that happens to fit a grammar. */
            uint32_t weight = (uint32_t)a->frame_n;
            a->candidates += weight;
            a->candidate_frames++;
            for(int e = RgEncodingPWM; e < RgEncodingCount; e++)
                a->enc_fit_sum[e] += (uint64_t)fits[e] * weight;
        }
    } else if(a->pass == 3) {
        int fit = 0;
        size_t nb = rg_analyzer_decode(
            r->encoding, &r->params, a->frame, a->frame_n, bits, RG_ANALYZER_MAX_BITS, &fit);
        if(nb < 4) fit = 0;
        if(fit >= RG_ANALYZER_GOOD_FIT) {
            r->signal_frames++;
            a->chosen_fit_sum += (uint32_t)fit;
        }
        /* A frame that filled the bit buffer or was cut off ran into the
         * next one(s): keep one repeat when the bits repeat themselves. */
        size_t decoded = nb;
        bool cut_off = a->frame_samples > a->frame_n;
        size_t period = 0;
        if(fit >= RG_ANALYZER_GOOD_FIT && (nb >= RG_ANALYZER_MAX_BITS || cut_off)) {
            period = rg_analyzer_repeat_period(bits, nb);
            if(period) nb = period;
        }
        if(r->frames_kept < RG_ANALYZER_MAX_FRAMES) {
            RgFrame* f = &r->frames[r->frames_kept++];
            f->start_us = a->frame_start_us;
            f->start_index = (uint32_t)a->frame_start_index;
            f->duration_us = a->frame_duration_us > UINT32_MAX ? UINT32_MAX :
                                                                 (uint32_t)a->frame_duration_us;
            f->samples = a->frame_samples > UINT16_MAX ? UINT16_MAX : (uint16_t)a->frame_samples;
            f->bit_count = (uint16_t)nb;
            f->fit = (uint8_t)fit;
            f->group = RG_ANALYZER_NO_GROUP;
            f->shift = 0;
            f->truncated = cut_off;
            f->repeat_bits = period ? (uint16_t)decoded : 0;
            rg_pack_bits(bits, nb, f->bits);
        }
    }
    rg_frame_reset(a);
}

static void rg_process(RgAnalyzer* a, int32_t v) {
    RgAnalysis* r = &a->result;
    uint32_t d = rg_abs32(v);
    if(a->pass == 1) {
        r->sample_count++;
        r->duration_us += d;
        if(r->min_us == 0 || d < r->min_us) r->min_us = d;
        if(d > r->max_us) r->max_us = d;
        rg_hist_add(v > 0 ? &a->hist_high : &a->hist_low, d);
    } else if(
        v < 0 ? d >= r->gap_us :
                (r->te_us && (uint64_t)d >= (uint64_t)r->te_us * RG_ANALYZER_HIGH_GAP_FACTOR)) {
        /* A gap ends a frame; so does a carrier held far longer than any
         * header pulse (a separator some remotes send between repeats). */
        rg_frame_end(a);
    } else {
        if(a->frame_samples == 0) {
            a->frame_start_us = a->time_us;
            a->frame_start_index = a->index;
        }
        if(a->frame_n < RG_ANALYZER_FRAME_SAMPLES) a->frame[a->frame_n++] = v;
        a->frame_samples++;
        a->frame_duration_us += d;
    }
    a->time_us += d;
    a->index++;
}

void rg_analyzer_begin(RgAnalyzer* a) {
    rg_bins_init();
    memset(a, 0, sizeof(*a));
    a->pass = 1;
}

void rg_analyzer_feed(RgAnalyzer* a, const int32_t* samples, size_t count) {
    if(a->pass == 0 || !samples) return;
    for(size_t i = 0; i < count; i++) {
        int32_t v = samples[i];
        if(v == 0) continue;
        if(a->pending == 0) {
            a->pending = v;
        } else if((v > 0) == (a->pending > 0)) {
            /* Same level twice in a row: one longer pulse (saturating). */
            int64_t sum = (int64_t)a->pending + v;
            if(sum > INT32_MAX) sum = INT32_MAX;
            if(sum < -INT32_MAX) sum = -INT32_MAX;
            a->pending = (int32_t)sum;
        } else {
            rg_process(a, a->pending);
            a->pending = v;
        }
    }
}

/* Pass 1 done: derive the timing model. */
static void rg_finish_timing(RgAnalyzer* a) {
    RgAnalysis* r = &a->result;
    RgPeakWork work[RG_ANALYZER_MAX_PEAKS];
    uint64_t dev = 0, weight = 0;
    uint32_t in_peaks = 0;

    size_t nh = rg_find_peaks(&a->hist_high, work, RG_ANALYZER_MAX_PEAKS);
    for(size_t i = 0; i < nh; i++) {
        r->high_peaks[i] = work[i].peak;
        dev += work[i].deviation;
        weight += (uint64_t)work[i].peak.center_us * work[i].peak.count;
        in_peaks += work[i].peak.count;
    }
    r->high_peak_count = nh;
    size_t nl = rg_find_peaks(&a->hist_low, work, RG_ANALYZER_MAX_PEAKS);
    for(size_t i = 0; i < nl; i++) {
        r->low_peaks[i] = work[i].peak;
        dev += work[i].deviation;
        weight += (uint64_t)work[i].peak.center_us * work[i].peak.count;
        in_peaks += work[i].peak.count;
    }
    r->low_peak_count = nl;

    uint32_t total = a->hist_high.total + a->hist_low.total;
    r->noise_pct = total ? (uint8_t)(((uint64_t)(total - in_peaks) * 100u) / total) : 0;
    r->jitter_pct = weight ? (uint8_t)((dev * 100u) / weight) : 0;

    /* Te: the shortest well-populated high and low peaks. Receivers widen
     * highs and shorten lows by about the same amount, so averaging the two
     * cancels that bias. */
    uint32_t th = rg_shortest_major(r->high_peaks, nh);
    uint32_t tl = rg_shortest_major(r->low_peaks, nl);
    uint32_t te = 0;
    if(th && tl) {
        uint32_t lo = th < tl ? th : tl, hi = th < tl ? tl : th;
        te = ((uint64_t)hi * 10u <= (uint64_t)lo * 16u) ? (th + tl) / 2u : lo;
    } else {
        te = th ? th : tl;
    }
    r->params.te_us = te;
    r->te_us = te;
    r->gap_us = te ? te * RG_ANALYZER_GAP_FACTOR : 3000u;
    if(r->gap_us < RG_ANALYZER_MIN_GAP_US) r->gap_us = RG_ANALYZER_MIN_GAP_US;

    /* Frames sent back to back can be separated by less than GAP_FACTOR x Te.
     * A low peak well above the two most common lows below the gap, and much
     * rarer than them, is taken as that separator. */
    size_t c1 = nl, c2 = nl;
    for(size_t i = 0; i < nl; i++) {
        const RgPeak* pk = &r->low_peaks[i];
        if(pk->center_us < RG_ANALYZER_MIN_TE_US || pk->center_us >= r->gap_us) continue;
        if(c1 == nl || pk->count > r->low_peaks[c1].count) {
            c2 = c1;
            c1 = i;
        } else if(c2 == nl || pk->count > r->low_peaks[c2].count) {
            c2 = i;
        }
    }
    if(c1 < nl && c2 < nl) {
        uint32_t d1 = r->low_peaks[c1].center_us, d2 = r->low_peaks[c2].center_us;
        uint32_t longest = d1 > d2 ? d1 : d2;
        uint32_t rarest = r->low_peaks[c2].count;
        for(size_t i = 0; i < nl; i++) {
            const RgPeak* pk = &r->low_peaks[i];
            if(pk->center_us >= r->gap_us) break;
            if((uint64_t)pk->center_us * 2u >= (uint64_t)longest * 3u &&
               pk->count * 2u <= rarest) {
                r->gap_us = (longest + pk->center_us) / 2u;
                break;
            }
        }
    }

    /* Decoder parameters: two high widths (PWM), dominant high plus two
     * in-frame low widths (PPM). Gap-sized lows are not data. */
    rg_top_two(r->high_peaks, nh, &r->params.pwm_short_us, &r->params.pwm_long_us);
    size_t top = nh;
    for(size_t i = 0; i < nh; i++) {
        if(r->high_peaks[i].center_us < RG_ANALYZER_MIN_TE_US) continue;
        if(top == nh || r->high_peaks[i].count > r->high_peaks[top].count) top = i;
    }
    r->params.ppm_high_us = top < nh ? r->high_peaks[top].center_us : 0;
    size_t nl_in = 0;
    while(nl_in < nl && r->low_peaks[nl_in].center_us < r->gap_us)
        nl_in++;
    rg_top_two(r->low_peaks, nl_in, &r->params.ppm_short_us, &r->params.ppm_long_us);

    if(nh + nl == 0 || r->noise_pct > 50 || r->jitter_pct > 25)
        r->quality = RgQualityPoor;
    else if(r->noise_pct <= 15 && r->jitter_pct <= 10)
        r->quality = RgQualityGood;
    else
        r->quality = RgQualityFair;
}

/* Pass 2 done: pick the encoding whose grammar fits the signal frames best. */
static void rg_finish_encoding(RgAnalyzer* a) {
    RgAnalysis* r = &a->result;
    r->encoding = RgEncodingUnknown;
    r->alternative = RgEncodingUnknown;
    if(a->candidates == 0) return;

    int score[RgEncodingCount] = {0};
    int best = RgEncodingUnknown, second = RgEncodingUnknown;
    for(int e = RgEncodingPWM; e < RgEncodingCount; e++) {
        score[e] = (int)(a->enc_fit_sum[e] / a->candidates);
        if(best == RgEncodingUnknown || score[e] > score[best]) {
            second = best;
            best = e;
        } else if(second == RgEncodingUnknown || score[e] > score[second]) {
            second = e;
        }
    }
    if(score[best] < RG_ANALYZER_GOOD_FIT / 2) return;
    r->encoding = (RgEncoding)best;
    int conf = score[best];
    if(second != RgEncodingUnknown && score[second] >= RG_ANALYZER_GOOD_FIT / 2) {
        r->alternative = (RgEncoding)second;
        r->alternative_fit = score[second];
        if(score[second] >= score[best] - 10)
            conf -= 20;
        else if(score[second] >= score[best] - 25)
            conf -= 10;
    }
    if(a->candidate_frames < 2) conf -= 10;
    r->encoding_confidence = conf;
}

/* Pass 3 done: group, align and compare the kept frames. */
static void rg_finish_frames(RgAnalyzer* a) {
    RgAnalysis* r = &a->result;
    char cur[RG_ANALYZER_MAX_BITS + 1];
    char pat[RG_ANALYZER_MAX_BITS + 1];

    r->fit_pct = r->signal_frames ? (int)(a->chosen_fit_sum / r->signal_frames) : 0;

    /* Bit-length estimate: the most common length among kept signal frames
     * (ties go to the longer length: cut-off frames are the usual outliers). */
    size_t modal = 0, modal_n = 0;
    for(size_t i = 0; i < r->frames_kept; i++) {
        const RgFrame* f = &r->frames[i];
        if(f->fit < RG_ANALYZER_GOOD_FIT) continue;
        size_t c = 0;
        for(size_t j = 0; j < r->frames_kept; j++) {
            if(r->frames[j].fit >= RG_ANALYZER_GOOD_FIT && r->frames[j].bit_count == f->bit_count)
                c++;
        }
        if(c > modal_n || (c == modal_n && f->bit_count > modal)) {
            modal_n = c;
            modal = f->bit_count;
        }
    }
    r->bit_count = modal;
    r->bit_count_frames = modal_n;

    /* Group identical patterns. Modal-length frames go first so a cut-off
     * first frame does not become the reference pattern. */
    for(int round = 0; round < 2; round++) {
        for(size_t i = 0; i < r->frames_kept; i++) {
            RgFrame* f = &r->frames[i];
            if(f->fit < RG_ANALYZER_GOOD_FIT) continue;
            if((round == 0) != (f->bit_count == modal)) continue;
            rg_analyzer_frame_bits(f, cur);
            size_t g;
            for(g = 0; g < r->group_count; g++) {
                const RgFrame* ref = &r->frames[r->groups[g].frame];
                if(ref->bit_count == f->bit_count &&
                   memcmp(ref->bits, f->bits, sizeof(f->bits)) == 0)
                    break;
            }
            if(g < r->group_count) {
                r->groups[g].exact++;
                f->group = (uint8_t)g;
                continue;
            }
            for(g = 0; g < r->group_count; g++) {
                const RgFrame* ref = &r->frames[r->groups[g].frame];
                rg_analyzer_frame_bits(ref, pat);
                int shift = 0;
                size_t ov = 0;
                size_t m = rg_analyzer_align(
                    pat, ref->bit_count, cur, f->bit_count, RG_ANALYZER_MAX_SHIFT, &shift, &ov);
                size_t shorter = ref->bit_count < f->bit_count ? ref->bit_count : f->bit_count;
                if(m == ov && ov >= 8 && ov * 4 >= shorter * 3 &&
                   (shift != 0 || ref->bit_count != f->bit_count)) {
                    r->groups[g].aligned++;
                    f->group = (uint8_t)g;
                    f->shift = (int8_t)shift;
                    break;
                }
            }
            if(g < r->group_count) continue;
            if(r->group_count < RG_ANALYZER_MAX_GROUPS) {
                RgGroup* ng = &r->groups[r->group_count];
                ng->frame = (uint8_t)i;
                ng->bit_count = f->bit_count;
                ng->exact = 1;
                ng->aligned = 0;
                f->group = (uint8_t)r->group_count;
                r->group_count++;
            } else {
                r->ungrouped++;
            }
        }
    }

    /* Largest group first; remap the frames' group indices to match. */
    uint8_t order[RG_ANALYZER_MAX_GROUPS];
    for(size_t g = 0; g < r->group_count; g++)
        order[g] = (uint8_t)g;
    for(size_t i = 1; i < r->group_count; i++) {
        uint8_t key = order[i];
        size_t j = i;
        while(j > 0 && (r->groups[order[j - 1]].exact + r->groups[order[j - 1]].aligned) <
                           (r->groups[key].exact + r->groups[key].aligned)) {
            order[j] = order[j - 1];
            j--;
        }
        order[j] = key;
    }
    RgGroup sorted[RG_ANALYZER_MAX_GROUPS];
    uint8_t remap[RG_ANALYZER_MAX_GROUPS];
    for(size_t g = 0; g < r->group_count; g++) {
        sorted[g] = r->groups[order[g]];
        remap[order[g]] = (uint8_t)g;
    }
    memcpy(r->groups, sorted, sizeof(RgGroup) * r->group_count);
    for(size_t i = 0; i < r->frames_kept; i++) {
        if(r->frames[i].group != RG_ANALYZER_NO_GROUP)
            r->frames[i].group = remap[r->frames[i].group];
    }
    if(r->group_count > 0) rg_analyzer_frame_bits(&r->frames[r->groups[0].frame], r->bits);

    /* Constant vs changing bit positions across modal-length frames. */
    size_t ref = r->frames_kept;
    for(size_t i = 0; i < r->frames_kept; i++) {
        const RgFrame* f = &r->frames[i];
        if(f->fit < RG_ANALYZER_GOOD_FIT || f->bit_count != modal || modal == 0) continue;
        if(ref == r->frames_kept || (f->group == 0 && r->frames[ref].group != 0)) ref = i;
    }
    if(ref < r->frames_kept) {
        rg_analyzer_frame_bits(&r->frames[ref], pat);
        memset(r->field_map, '.', modal);
        r->field_map[modal] = '\0';
        for(size_t i = 0; i < r->frames_kept; i++) {
            const RgFrame* f = &r->frames[i];
            if(i == ref || f->fit < RG_ANALYZER_GOOD_FIT || f->bit_count != modal) continue;
            rg_analyzer_frame_bits(f, cur);
            for(size_t k = 0; k < modal; k++)
                if(cur[k] != pat[k]) r->field_map[k] = 'X';
            r->compared++;
        }
        if(r->compared > 0) {
            r->have_field_diff = true;
            for(size_t k = 0; k < modal; k++) {
                if(r->field_map[k] == 'X')
                    r->changing_bits++;
                else
                    r->const_bits++;
            }
        } else {
            r->field_map[0] = '\0';
        }
    }

    /* ID candidate: the longest constant run, only when other bits change. */
    if(r->changing_bits > 0) {
        size_t run = 0;
        for(size_t k = 0; k <= modal; k++) {
            if(k < modal && r->field_map[k] == '.') {
                run++;
            } else {
                if(run > r->id_len) {
                    r->id_len = run;
                    r->id_start = k - run;
                }
                run = 0;
            }
        }
        if(r->id_len < 8) {
            r->id_len = 0;
            r->id_start = 0;
        }
    }

    int conf = r->encoding_confidence;
    if(r->group_count > 0 && r->groups[0].exact >= 2) conf += 5;
    if(modal < 8 && conf > 40) conf = 40;
    if(conf < 0) conf = 0;
    if(conf > 95) conf = 95;
    r->encoding_confidence = conf;
}

bool rg_analyzer_next_pass(RgAnalyzer* a) {
    if(a->pass == 0) return false;
    if(a->pending != 0) {
        rg_process(a, a->pending);
        a->pending = 0;
    }
    if(a->pass >= 2) rg_frame_end(a);

    bool again = false;
    if(a->pass == 1) {
        if(a->result.sample_count > 0) {
            rg_finish_timing(a);
            a->pass = 2;
            again = true;
        }
    } else if(a->pass == 2) {
        rg_finish_encoding(a);
        if(a->result.encoding != RgEncodingUnknown) {
            a->pass = 3;
            again = true;
        }
    } else {
        rg_finish_frames(a);
    }
    if(!again) a->pass = 0;
    a->time_us = 0;
    a->index = 0;
    rg_frame_reset(a);
    return again;
}

void rg_analyzer_run(const int32_t* samples, size_t count, RgAnalysis* out) {
    RgAnalyzer* a = malloc(sizeof(RgAnalyzer));
    if(!a) {
        memset(out, 0, sizeof(*out));
        return;
    }
    rg_analyzer_begin(a);
    do {
        rg_analyzer_feed(a, samples, count);
    } while(rg_analyzer_next_pass(a));
    memcpy(out, &a->result, sizeof(*out));
    free(a);
}

const char* rg_analyzer_encoding_name(RgEncoding e) {
    switch(e) {
    case RgEncodingPWM:
        return "PWM";
    case RgEncodingPPM:
        return "PPM";
    case RgEncodingManchester:
        return "Manchester";
    default:
        return "unknown";
    }
}

const char* rg_analyzer_quality_name(RgQuality q) {
    switch(q) {
    case RgQualityGood:
        return "good";
    case RgQualityFair:
        return "fair";
    case RgQualityPoor:
        return "poor";
    default:
        return "unknown";
    }
}

/* ---- similarity --------------------------------------------------------- */

static bool rg_pair_match(int32_t a, int32_t b) {
    if((a < 0) != (b < 0)) return false;
    uint32_t aa = rg_abs32(a), bb = rg_abs32(b);
    uint32_t hi = aa > bb ? aa : bb;
    uint32_t diff = aa > bb ? aa - bb : bb - aa;
    uint32_t tol = hi / 4; /* 25% */
    if(tol < 60) tol = 60; /* absolute floor, us */
    return diff <= tol;
}

void rg_similarity_init(RgSimilarity* s) {
    memset(s, 0, sizeof(*s));
}

void rg_similarity_feed(RgSimilarity* s, const int32_t* a, const int32_t* b, size_t n) {
    for(size_t i = 0; i < n; i++)
        if(rg_pair_match(a[i], b[i])) s->matches++;
    s->na += n;
    s->nb += n;
}

void rg_similarity_tail(RgSimilarity* s, size_t extra_a, size_t extra_b) {
    s->na += extra_a;
    s->nb += extra_b;
}

int rg_similarity_score(const RgSimilarity* s) {
    if(s->na == 0 || s->nb == 0) return 0;
    size_t max_len = s->na > s->nb ? s->na : s->nb;
    return (int)((s->matches * 100) / max_len);
}

int rg_analyzer_similarity(const int32_t* a, size_t na, const int32_t* b, size_t nb) {
    RgSimilarity s;
    rg_similarity_init(&s);
    size_t common = na < nb ? na : nb;
    rg_similarity_feed(&s, a, b, common);
    rg_similarity_tail(&s, na - common, nb - common);
    return rg_similarity_score(&s);
}
