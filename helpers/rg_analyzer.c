#include "rg_analyzer.h"

#include <stdlib.h>
#include <string.h>

/* ---- clustering --------------------------------------------------------- */

void rg_analyzer_cluster_add(RgCluster* clusters, size_t* n, size_t max, uint32_t value) {
    for(size_t i = 0; i < *n; i++) {
        uint32_t c = clusters[i].center;
        uint32_t tol = (c * RG_ANALYZER_CLUSTER_TOL) / 100 + 10;
        uint32_t diff = (value > c) ? (value - c) : (c - value);
        if(diff <= tol) {
            clusters[i].count++;
            clusters[i].sum += value;
            clusters[i].center = (uint32_t)(clusters[i].sum / clusters[i].count);
            return;
        }
    }
    if(*n < max) {
        clusters[*n].center = value;
        clusters[*n].count = 1;
        clusters[*n].sum = value;
        (*n)++;
    }
}

void rg_analyzer_sort_clusters(RgCluster* clusters, size_t n) {
    for(size_t i = 1; i < n; i++) {
        RgCluster key = clusters[i];
        size_t j = i;
        while(j > 0 && clusters[j - 1].center > key.center) {
            clusters[j] = clusters[j - 1];
            j--;
        }
        clusters[j] = key;
    }
}

static uint32_t rg_abs32(int32_t v) {
    return (uint32_t)(v < 0 ? -(int64_t)v : v);
}

/* ---- encoding classification ------------------------------------------- */

/* Fraction (0..100) of samples whose magnitude falls within tol% of `center`. */
static int rg_fraction_near(const int32_t* s, size_t n, uint32_t center, uint32_t tol_pct) {
    if(n == 0 || center == 0) return 0;
    uint32_t tol = (center * tol_pct) / 100 + 10;
    size_t hit = 0;
    for(size_t i = 0; i < n; i++) {
        uint32_t a = rg_abs32(s[i]);
        uint32_t d = (a > center) ? (a - center) : (center - a);
        if(d <= tol) hit++;
    }
    return (int)((hit * 100) / n);
}

/*
 * Classify the line encoding from the timing clusters and the raw stream.
 * Heuristic, conservative, and always returned with a confidence score.
 */
static void rg_classify_encoding(const int32_t* s, size_t n, RgAnalysis* out) {
    out->encoding = RgEncodingUnknown;
    out->encoding_confidence = 0;
    if(out->cluster_count == 0 || out->te_us == 0) return;

    uint32_t te = out->te_us;

    /* Coverage of the two smallest timing groups (Te and ~2Te) over all edges. */
    int near_1x = rg_fraction_near(s, n, te, RG_ANALYZER_CLUSTER_TOL);
    int near_2x = rg_fraction_near(s, n, te * 2, RG_ANALYZER_CLUSTER_TOL);

    /* Separate high/low magnitude cluster behaviour. */
    size_t hi_clusters = 0, lo_clusters = 0;
    {
        RgCluster hi[RG_ANALYZER_MAX_CLUSTERS] = {0};
        RgCluster lo[RG_ANALYZER_MAX_CLUSTERS] = {0};
        size_t hn = 0, ln = 0;
        for(size_t i = 0; i < n; i++) {
            uint32_t a = rg_abs32(s[i]);
            if(a == 0) continue;
            if(s[i] > 0)
                rg_analyzer_cluster_add(hi, &hn, RG_ANALYZER_MAX_CLUSTERS, a);
            else
                rg_analyzer_cluster_add(lo, &ln, RG_ANALYZER_MAX_CLUSTERS, a);
        }
        /* Count "significant" clusters (>=10% of their side's edges). */
        size_t hi_total = 0, lo_total = 0;
        for(size_t i = 0; i < hn; i++)
            hi_total += hi[i].count;
        for(size_t i = 0; i < ln; i++)
            lo_total += lo[i].count;
        for(size_t i = 0; i < hn; i++) {
            if(hi_total && hi[i].count * 10 >= hi_total) hi_clusters++;
        }
        for(size_t i = 0; i < ln; i++) {
            if(lo_total && lo[i].count * 10 >= lo_total) lo_clusters++;
        }
    }

    /* PPM: a (near-)constant high pulse, information carried in variable gaps. */
    if(hi_clusters == 1 && lo_clusters >= 2) {
        out->encoding = RgEncodingPPM;
        out->encoding_confidence = 55 + (near_1x > 60 ? 20 : 0);
        if(out->encoding_confidence > 90) out->encoding_confidence = 90;
        return;
    }

    /* PWM/OOK: two dominant pulse widths (short ~Te, long ~2-3Te). */
    if(hi_clusters >= 2 || (hi_clusters == 1 && lo_clusters >= 1 && near_2x >= 15)) {
        int conf = 50 + (near_1x + near_2x) / 4;
        if(conf > 92) conf = 92;
        out->encoding = RgEncodingPWM;
        out->encoding_confidence = conf;
        return;
    }

    /* Manchester: nearly everything is Te or 2Te with dense transitions. */
    if(near_1x + near_2x >= 85 && near_1x >= 25 && near_2x >= 15) {
        out->encoding = RgEncodingManchester;
        out->encoding_confidence = 45 + (near_1x + near_2x - 85);
        if(out->encoding_confidence > 80) out->encoding_confidence = 80;
        return;
    }

    /* Fallback: single dominant width -> weak PWM guess. */
    out->encoding = RgEncodingPWM;
    out->encoding_confidence = 25;
}

/* ---- bit extraction (PWM representative frame) -------------------------- */

/*
 * Decode a (high,low) pair stream into bits: bit = 1 when the high pulse is the
 * longer element of the pair, else 0. Returns number of bits written.
 */
static size_t rg_decode_pwm_bits(const int32_t* s, size_t n, char* bits, size_t max_bits) {
    size_t b = 0;
    size_t i = 0;
    /* Align to a rising edge (a positive/high sample). */
    while(i < n && s[i] <= 0)
        i++;
    for(; i + 1 < n && b < max_bits; i += 2) {
        uint32_t high = rg_abs32(s[i]);
        uint32_t low = rg_abs32(s[i + 1]);
        if(high == 0 && low == 0) break;
        bits[b++] = (high >= low) ? '1' : '0';
    }
    bits[b] = '\0';
    return b;
}

/* ---- main --------------------------------------------------------------- */

void rg_analyzer_run(const int32_t* samples, size_t count, RgAnalysis* out) {
    memset(out, 0, sizeof(*out));
    out->bits[0] = '\0';
    out->field_map[0] = '\0';
    if(!samples || count == 0) return;

    out->sample_count = count;

    /* Stats + clustering over absolute durations. */
    for(size_t i = 0; i < count; i++) {
        uint32_t a = rg_abs32(samples[i]);
        if(a == 0) continue;
        if(out->min_us == 0 || a < out->min_us) out->min_us = a;
        if(a > out->max_us) out->max_us = a;
        rg_analyzer_cluster_add(out->clusters, &out->cluster_count, RG_ANALYZER_MAX_CLUSTERS, a);
    }
    rg_analyzer_sort_clusters(out->clusters, out->cluster_count);

    /* Te = smallest cluster center carrying a meaningful share of edges. */
    if(out->cluster_count > 0) {
        size_t total = 0;
        for(size_t i = 0; i < out->cluster_count; i++)
            total += out->clusters[i].count;
        out->te_us = out->clusters[0].center;
        for(size_t i = 0; i < out->cluster_count; i++) {
            if(total && out->clusters[i].count * 20 >= total) { /* >=5% */
                out->te_us = out->clusters[i].center;
                break;
            }
        }
    }

    /* Frame segmentation on long low gaps (>= GAP_FACTOR * Te). */
    uint32_t gap_threshold = out->te_us * RG_ANALYZER_GAP_FACTOR;
    size_t frame_starts[64];
    size_t frame_lens[64];
    size_t nframes = 0;
    if(out->te_us > 0) {
        size_t start = 0;
        for(size_t i = 0; i < count; i++) {
            bool is_gap = (samples[i] < 0) && (rg_abs32(samples[i]) >= gap_threshold);
            if(is_gap) {
                size_t len = i - start; /* samples before the gap */
                if(len > 0 && nframes < 64) {
                    frame_starts[nframes] = start;
                    frame_lens[nframes] = len;
                    nframes++;
                }
                start = i + 1;
            }
        }
        if(start < count && nframes < 64) {
            frame_starts[nframes] = start;
            frame_lens[nframes] = count - start;
            nframes++;
        }
    }
    out->frame_count = nframes;

    /* Repeated-frame detection: most frames share the modal length (+-1). */
    size_t rep_index = 0;
    if(nframes >= 2) {
        size_t best_count = 0, best_len = 0;
        for(size_t i = 0; i < nframes; i++) {
            size_t c = 0;
            for(size_t j = 0; j < nframes; j++) {
                size_t d = (frame_lens[i] > frame_lens[j]) ? frame_lens[i] - frame_lens[j] :
                                                             frame_lens[j] - frame_lens[i];
                if(d <= 1) c++;
            }
            if(c > best_count) {
                best_count = c;
                best_len = frame_lens[i];
            }
        }
        if(best_count >= 2) {
            out->frames_repeat = true;
            out->repeat_count = best_count;
            for(size_t i = 0; i < nframes; i++) {
                size_t d = (frame_lens[i] > best_len) ? frame_lens[i] - best_len :
                                                        best_len - frame_lens[i];
                if(d <= 1) {
                    rep_index = i;
                    break;
                }
            }
        }
    }

    /* Representative frame = the repeated one, else the longest. */
    if(!out->frames_repeat && nframes > 0) {
        size_t longest = 0;
        for(size_t i = 1; i < nframes; i++)
            if(frame_lens[i] > frame_lens[longest]) longest = i;
        rep_index = longest;
    }

    const int32_t* frame = samples;
    size_t frame_len = count;
    if(nframes > 0) {
        frame = samples + frame_starts[rep_index];
        frame_len = frame_lens[rep_index];
    }
    out->frame_len_samples = frame_len;

    /* Encoding hypothesis from the representative frame. */
    rg_classify_encoding(frame, frame_len, out);

    /* Best-effort bit extraction for PWM/OOK. */
    if(out->encoding == RgEncodingPWM) {
        out->bit_count = rg_decode_pwm_bits(frame, frame_len, out->bits, RG_ANALYZER_MAX_BITS);
    }

    /*
     * Constant vs changing fields: decode each repeated equal-length frame and
     * compare bit positions. Constant bits are a likely fixed device-ID portion;
     * changing bits indicate a rolling counter / dynamic payload. Purely
     * descriptive — no attempt to predict or recover anything.
     */
    if(out->frames_repeat && out->encoding == RgEncodingPWM && out->bit_count > 0 &&
       nframes >= 2) {
        char ref[RG_ANALYZER_MAX_BITS + 1];
        size_t ref_bits = out->bit_count;
        memcpy(ref, out->bits, ref_bits + 1);
        for(size_t k = 0; k <= ref_bits; k++)
            out->field_map[k] = '.';
        out->field_map[ref_bits] = '\0';

        size_t compared = 0;
        for(size_t i = 0; i < nframes; i++) {
            size_t d = (frame_lens[i] > frame_len) ? frame_lens[i] - frame_len :
                                                     frame_len - frame_lens[i];
            if(d > 1 || i == rep_index) continue;
            char other[RG_ANALYZER_MAX_BITS + 1];
            size_t ob = rg_decode_pwm_bits(
                samples + frame_starts[i], frame_lens[i], other, RG_ANALYZER_MAX_BITS);
            size_t m = ob < ref_bits ? ob : ref_bits;
            for(size_t k = 0; k < m; k++) {
                if(other[k] != ref[k]) out->field_map[k] = 'X';
            }
            compared++;
        }
        if(compared > 0) {
            out->have_field_diff = true;
            for(size_t k = 0; k < ref_bits; k++) {
                if(out->field_map[k] == 'X')
                    out->changing_bits++;
                else
                    out->const_bits++;
            }
        }
    }
}

const char* rg_analyzer_encoding_name(RgEncoding e) {
    switch(e) {
    case RgEncodingPWM:
        return "PWM / OOK";
    case RgEncodingPPM:
        return "PPM (gap-coded)";
    case RgEncodingManchester:
        return "Manchester";
    case RgEncodingUnknown:
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

int rg_analyzer_similarity(const int32_t* a, size_t na, const int32_t* b, size_t nb) {
    if(na == 0 || nb == 0) return 0;
    size_t min_len = na < nb ? na : nb;
    size_t max_len = na > nb ? na : nb;
    size_t matches = 0;
    for(size_t i = 0; i < min_len; i++) {
        if(rg_pair_match(a[i], b[i])) matches++;
    }
    /* Score over the longer sequence so a length mismatch is penalised. */
    return (int)((matches * 100) / max_len);
}
