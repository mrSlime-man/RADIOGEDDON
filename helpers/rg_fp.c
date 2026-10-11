#include "rg_fp.h"
#include "../radiogeddon_edition.h"

#if RG_FEATURE_FINGERPRINT

#include "rg_crc.h"
#include <string.h>

/* ---- building ---------------------------------------------------------------- */

/* Upper edges of the buckets, in hundredths of Te; the last bucket is open. */
static const uint16_t rg_fp_edges[RG_FP_BINS - 1] =
    {75, 125, 175, 250, 350, 500, 700, 1000, 1450, 2000, 4000};

/* Gaps longer than this many Te are pauses between presses, not a sync. */
#define RG_FP_SYNC_MAX_TE 250u

static size_t rg_fp_strnlen(const char* s, size_t max) {
    size_t n = 0;
    while(n < max && s[n])
        n++;
    return n;
}

static uint32_t rg_fp_abs(int32_t v) {
    return v < 0 ? (uint32_t)(-(int64_t)v) : (uint32_t)v;
}

size_t rg_fp_bin(uint32_t us, uint32_t te) {
    if(te == 0) return RG_FP_BINS - 1;
    uint64_t r = (uint64_t)us * 100u / te;
    for(size_t i = 0; i < RG_FP_BINS - 1; i++)
        if(r < rg_fp_edges[i]) return i;
    return RG_FP_BINS - 1;
}

uint32_t rg_fp_fnv(uint32_t h, const void* data, size_t n) {
    const uint8_t* p = data;
    for(size_t i = 0; i < n; i++) {
        h ^= p[i];
        h *= 16777619u;
    }
    return h;
}

void rg_fp_builder_init(RgFpBuilder* b, const RgAnalysis* analysis) {
    memset(b, 0, sizeof(*b));
    b->te = analysis ? analysis->te_us : 0;
    b->gap_us = analysis ? analysis->gap_us : 0;
    if(b->gap_us == 0) b->gap_us = RG_ANALYZER_MIN_GAP_US;
    b->in_preamble = true;
}

static void rg_fp_builder_end_frame(RgFpBuilder* b, uint32_t gap) {
    if(b->pos >= RG_ANALYZER_MIN_FRAME_SAMPLES && b->frames < sizeof(b->preambles)) {
        uint32_t cells = b->cells >= 8u ? b->cells : 0u; // RG_ANALYZER_PREAMBLE_MIN
        b->preambles[b->frames] = (uint8_t)(cells > 255u ? 255u : cells);
        uint32_t ratio = b->te ? gap / b->te : 0;
        b->syncs[b->frames] = (uint8_t)(ratio <= RG_FP_SYNC_MAX_TE ? ratio : 0u);
        b->frames++;
    }
    b->pos = 0;
    b->cells = 0;
    b->in_preamble = true;
}

static void rg_fp_builder_sample(RgFpBuilder* b, int32_t v) {
    uint32_t d = rg_fp_abs(v);
    if(v < 0 && d >= b->gap_us) {
        rg_fp_builder_end_frame(b, d);
        return;
    }
    if(b->te == 0) return;
    if(b->pos == 0 && v < 0) return; // idle low before the first pulse
    if(v > 0) {
        b->high[rg_fp_bin(d, b->te)]++;
    } else {
        b->low[rg_fp_bin(d, b->te)]++;
    }
    if(b->in_preamble) {
        // One Te cell (0.5..1.5 Te), as the analyzer's preamble test.
        if(d * 2u >= b->te && d * 2u <= b->te * 3u) {
            b->cells++;
        } else {
            b->in_preamble = false;
        }
    }
    b->pos++;
}

void rg_fp_builder_feed(RgFpBuilder* b, const int32_t* samples, size_t count) {
    for(size_t i = 0; i < count; i++) {
        int32_t v = samples[i];
        if(v == 0) continue;
        if(b->pending == 0) {
            b->pending = v;
        } else if((b->pending > 0) == (v > 0)) {
            // Same level twice: one longer pulse or gap (as the analyzer merges).
            int64_t sum = (int64_t)b->pending + v;
            if(sum > INT32_MAX) sum = INT32_MAX;
            if(sum < -INT32_MAX) sum = -INT32_MAX;
            b->pending = (int32_t)sum;
        } else {
            rg_fp_builder_sample(b, b->pending);
            b->pending = v;
        }
    }
}

static uint8_t rg_fp_median8(uint8_t* v, size_t n) {
    if(n == 0) return 0;
    for(size_t i = 1; i < n; i++) {
        uint8_t x = v[i];
        size_t j = i;
        while(j > 0 && v[j - 1] > x) {
            v[j] = v[j - 1];
            j--;
        }
        v[j] = x;
    }
    return v[n / 2];
}

static void rg_fp_shares(const uint32_t* count, uint8_t* out) {
    uint64_t total = 0;
    for(size_t i = 0; i < RG_FP_BINS; i++)
        total += count[i];
    for(size_t i = 0; i < RG_FP_BINS; i++)
        out[i] = total ? (uint8_t)((count[i] * 100u + total / 2u) / total) : 0;
}

void rg_fp_from_analysis(
    RgFingerprint* fp,
    const RgAnalysis* r,
    RgFpBuilder* b,
    uint32_t frequency,
    uint8_t preset) {
    memset(fp, 0, sizeof(*fp));
    fp->flags = RG_FP_FLAG_RAW;
    fp->preset = preset;
    fp->frequency = frequency;
    fp->te_us = (uint16_t)(r->te_us > UINT16_MAX ? UINT16_MAX : r->te_us);
    fp->quality = (uint8_t)r->quality;
    fp->noise_pct = r->noise_pct;
    fp->jitter_pct = r->jitter_pct;
    fp->frames = (uint16_t)(r->frame_count > UINT16_MAX ? UINT16_MAX : r->frame_count);
    fp->groups = (uint8_t)(r->group_count > 255u ? 255u : r->group_count);
    bool structure = r->encoding != RgEncodingUnknown && r->signal_frames > 0 && r->bit_count > 0;
    if(structure) {
        fp->flags |= RG_FP_FLAG_STRUCTURE;
        fp->encoding = (uint8_t)r->encoding;
        fp->alternative = (uint8_t)r->alternative;
        fp->fit = (uint8_t)(r->fit_pct < 0 ? 0 : r->fit_pct > 100 ? 100 : r->fit_pct);
        fp->bit_count = (uint16_t)r->bit_count;
        size_t n = rg_fp_strnlen(r->bits, RG_ANALYZER_MAX_BITS);
        if(n >= 8) fp->pattern_hash = rg_fp_fnv(RG_FP_FNV_INIT, r->bits, n) | 1u;
        for(size_t i = 0; i < r->frames_kept && i < RG_ANALYZER_MAX_FRAMES; i++)
            if(r->frames[i].truncated) fp->flags |= RG_FP_FLAG_TRUNCATED;
        if(r->have_field_diff && r->compared > 0) {
            size_t total = r->const_bits + r->changing_bits;
            fp->const_pct = total ? (uint8_t)(r->const_bits * 100u / total) : 100;
            if(r->changing_bits) fp->flags |= RG_FP_FLAG_CHANGING;
            if(r->const_bits >= 8) {
                uint32_t h = RG_FP_FNV_INIT;
                size_t m = rg_fp_strnlen(r->field_map, RG_ANALYZER_MAX_BITS);
                for(size_t i = 0; i < m && i < n; i++) {
                    if(r->field_map[i] != '.') continue;
                    uint8_t pv[3] = {(uint8_t)i, (uint8_t)(i >> 8), (uint8_t)r->bits[i]};
                    h = rg_fp_fnv(h, pv, sizeof(pv));
                }
                fp->stable_hash = h | 1u;
            }
        } else {
            fp->const_pct = 100;
            fp->stable_hash = fp->pattern_hash;
        }
        RgRepeatTiming rt;
        if(r->group_count > 0 && rg_analyzer_repeat_timing(r, 0, &rt) && rt.median_us > 0) {
            uint32_t ms = (rt.median_us + 500u) / 1000u;
            fp->repeat_ms = (uint16_t)(ms > UINT16_MAX ? UINT16_MAX : (ms ? ms : 1u));
        }
    }
    if(b) {
        rg_fp_builder_feed(b, NULL, 0);
        if(b->pending) {
            rg_fp_builder_sample(b, b->pending);
            b->pending = 0;
        }
        if(b->pos) rg_fp_builder_end_frame(b, 0);
        rg_fp_shares(b->high, fp->high_hist);
        rg_fp_shares(b->low, fp->low_hist);
        fp->preamble = rg_fp_median8(b->preambles, b->frames);
        // The sync: the median gap that ended a frame (pauses left out).
        uint8_t syncs[sizeof(b->syncs)];
        size_t ns = 0;
        for(size_t i = 0; i < b->frames; i++)
            if(b->syncs[i]) syncs[ns++] = b->syncs[i];
        fp->sync_ratio = rg_fp_median8(syncs, ns);
    }
}

void rg_fp_from_decoded(
    RgFingerprint* fp,
    const char* protocol,
    uint16_t bits,
    uint32_t key_hash,
    uint32_t frequency,
    uint8_t preset) {
    memset(fp, 0, sizeof(*fp));
    fp->flags = RG_FP_FLAG_STRUCTURE;
    fp->preset = preset;
    fp->frequency = frequency;
    fp->bit_count = bits;
    fp->const_pct = 100;
    if(protocol && protocol[0])
        fp->proto_hash = rg_fp_fnv(RG_FP_FNV_INIT, protocol, strlen(protocol)) | 1u;
    fp->pattern_hash = key_hash ? (key_hash | 1u) : 0;
    fp->stable_hash = fp->pattern_hash;
}

/* ---- similarity -------------------------------------------------------------- */

typedef struct {
    uint32_t num;
    uint32_t den;
    uint16_t evidence;
    uint16_t contradicts;
} RgFpScore;

static void rg_fp_add(RgFpScore* s, uint32_t score, uint32_t weight, RgFpEvidence ev) {
    s->num += score * weight;
    s->den += 100u * weight;
    if(score >= 70u) s->evidence |= (uint16_t)ev;
    if(score < 40u) s->contradicts |= (uint16_t)ev;
}

/* 100 when within @p good percent, 0 from @p bad percent, linear between. */
static uint32_t rg_fp_ratio_score(uint32_t a, uint32_t b, uint32_t good, uint32_t bad) {
    uint32_t hi = a > b ? a : b;
    uint32_t lo = a > b ? b : a;
    if(hi == 0) return 100;
    uint32_t pct = (uint32_t)((uint64_t)(hi - lo) * 100u / hi);
    if(pct <= good) return 100;
    if(pct >= bad) return 0;
    return 100u * (bad - pct) / (bad - good);
}

/* Overlap of two share histograms after spreading a sixth of each bucket
 * into its neighbours, so a duration on a bucket edge still meets itself
 * while 2 Te and 3 Te stay apart. */
static uint32_t rg_fp_hist_overlap(const uint8_t* a, const uint8_t* b) {
    uint32_t sum = 0, ta = 0, tb = 0;
    for(size_t i = 0; i < RG_FP_BINS; i++) {
        uint32_t sa = a[i] * 4u, sb = b[i] * 4u;
        if(i > 0) {
            sa += a[i - 1];
            sb += b[i - 1];
        }
        if(i + 1 < RG_FP_BINS) {
            sa += a[i + 1];
            sb += b[i + 1];
        }
        sum += sa < sb ? sa : sb;
        ta += sa;
        tb += sb;
    }
    uint32_t t = ta > tb ? ta : tb;
    return t ? sum * 100u / t : 0;
}

static bool rg_fp_has_hist(const uint8_t* h) {
    for(size_t i = 0; i < RG_FP_BINS; i++)
        if(h[i]) return true;
    return false;
}

void rg_fp_compare(const RgFingerprint* a, const RgFingerprint* b, RgFpMatch* out) {
    RgFpScore s = {0, 0, 0, 0};
    uint32_t timing = 100; // pulse/gap distribution overlap, when compared
    if(a->frequency && b->frequency) {
        uint32_t d = a->frequency > b->frequency ? a->frequency - b->frequency :
                                                   b->frequency - a->frequency;
        rg_fp_add(&s, d <= 50000u ? 100u : d <= 200000u ? 50u : 0u, 10, RgFpEvFrequency);
    }
    if(a->preset != 0xFF && b->preset != 0xFF)
        rg_fp_add(&s, a->preset == b->preset ? 100u : 0u, 5, RgFpEvPreset);

    bool raw_a = a->flags & RG_FP_FLAG_RAW, raw_b = b->flags & RG_FP_FLAG_RAW;
    bool st_a = a->flags & RG_FP_FLAG_STRUCTURE, st_b = b->flags & RG_FP_FLAG_STRUCTURE;
    if(!raw_a && !raw_b) {
        // Two decoded files: the firmware named their protocols.
        if(a->proto_hash && b->proto_hash)
            rg_fp_add(&s, a->proto_hash == b->proto_hash ? 100u : 0u, 40, RgFpEvProtocol);
    }
    if(raw_a && raw_b) {
        if(a->te_us && b->te_us) rg_fp_add(&s, rg_fp_ratio_score(a->te_us, b->te_us, 8, 35), 15, RgFpEvTe);
        if(rg_fp_has_hist(a->high_hist) && rg_fp_has_hist(b->high_hist)) {
            uint32_t hi = rg_fp_hist_overlap(a->high_hist, b->high_hist);
            uint32_t lo = rg_fp_has_hist(a->low_hist) && rg_fp_has_hist(b->low_hist) ?
                              rg_fp_hist_overlap(a->low_hist, b->low_hist) :
                              hi;
            timing = (hi + lo) / 2u;
            rg_fp_add(&s, timing, 30, RgFpEvTiming);
        }
        if(st_a && st_b) {
            uint32_t enc = a->encoding == b->encoding                                     ? 100u :
                           (a->encoding == b->alternative || b->encoding == a->alternative) ? 30u :
                                                                                            0u;
            rg_fp_add(&s, enc, 10, RgFpEvEncoding);
        }
        if(a->preamble || b->preamble) {
            uint32_t p = (a->preamble && b->preamble) ?
                             rg_fp_ratio_score(a->preamble, b->preamble, 20, 60) :
                             0u;
            rg_fp_add(&s, p, 5, RgFpEvPreamble);
        }
        if(a->sync_ratio && b->sync_ratio)
            rg_fp_add(&s, rg_fp_ratio_score(a->sync_ratio, b->sync_ratio, 15, 50), 5, RgFpEvSync);
        if(a->repeat_ms && b->repeat_ms)
            rg_fp_add(&s, rg_fp_ratio_score(a->repeat_ms, b->repeat_ms, 15, 50), 5, RgFpEvRepeat);
    }
    if(st_a && st_b && a->bit_count && b->bit_count) {
        uint32_t d = a->bit_count > b->bit_count ? a->bit_count - b->bit_count :
                                                   b->bit_count - a->bit_count;
        // A frame the analyzer cut short only shows a lower bound.
        bool cut = (a->flags | b->flags) & RG_FP_FLAG_TRUNCATED;
        uint32_t fl = d == 0 ? 100u : d <= 1 ? 85u : d <= 4 ? 50u : cut ? 40u : 0u;
        rg_fp_add(&s, fl, 15, RgFpEvFrameLength);
    }
    // Identical content only adds (different buttons of one remote differ),
    // and only read the same way: the same bits from another encoding are a
    // coincidence of the bit strings, not the same frame.
    bool same_reading = raw_a == raw_b && a->encoding == b->encoding;
    if(same_reading && a->pattern_hash && a->pattern_hash == b->pattern_hash) {
        rg_fp_add(&s, 100, 10, RgFpEvPattern);
        s.evidence |= RgFpEvStable;
    } else if(same_reading && a->stable_hash && a->stable_hash == b->stable_hash) {
        rg_fp_add(&s, 100, 5, RgFpEvStable);
    }

    uint32_t score = s.den ? (s.num * 100u + s.den / 2u) / s.den : 0;
    if(!st_a || !st_b) {
        // Nothing decodable on one side: noise agrees with noise too easily.
        if(score > RG_FP_NO_STRUCTURE_CAP) score = RG_FP_NO_STRUCTURE_CAP;
    } else if(raw_a != raw_b && score > 60u) {
        score = 60; // a RAW capture against a decoded file: few features compare
    } else if(timing < 50u && score > 65u) {
        score = 65; // pulses and gaps of other shapes: not the same structure
    }
    out->score = (uint8_t)(score > 100u ? 100u : score);
    out->evidence = s.evidence;
    out->contradicts = s.contradicts;
}

const char* rg_fp_strength(uint8_t score) {
    if(score >= 85) return "Strong";
    if(score >= 70) return "Similar";
    if(score >= 50) return "Weak";
    return "Different";
}

static const char* const rg_fp_ev_names[RG_FP_EVIDENCE_COUNT] = {
    "frequency",
    "preset",
    "Te",
    "timing",
    "encoding",
    "frame length",
    "preamble",
    "sync gap",
    "repeat rate",
    "same frame",
    "same fixed bits",
    "protocol",
};

const char* rg_fp_evidence_name(RgFpEvidence ev) {
    for(size_t i = 0; i < RG_FP_EVIDENCE_COUNT; i++)
        if(ev == (1u << i)) return rg_fp_ev_names[i];
    return "?";
}

size_t rg_fp_evidence_text(uint16_t evidence, size_t max, char* out, size_t size) {
    // Most telling first.
    static const uint8_t order[RG_FP_EVIDENCE_COUNT] = {11, 9, 10, 3, 5, 2, 4, 6, 7, 8, 0, 1};
    size_t len = 0, items = 0;
    if(size == 0) return 0;
    out[0] = '\0';
    for(size_t k = 0; k < RG_FP_EVIDENCE_COUNT && items < max; k++) {
        if(!(evidence & (1u << order[k]))) continue;
        const char* name = rg_fp_ev_names[order[k]];
        size_t n = strlen(name);
        size_t need = n + (items ? 2u : 0u);
        if(len + need + 1 > size) break;
        if(items) {
            out[len++] = ',';
            out[len++] = ' ';
        }
        memcpy(out + len, name, n);
        len += n;
        out[len] = '\0';
        items++;
    }
    return len;
}

/* ---- ranking ----------------------------------------------------------------- */

void rg_fp_rank_init(RgFpRank* r, uint8_t min_score) {
    memset(r, 0, sizeof(*r));
    r->min_score = min_score;
}

void rg_fp_rank_offer(RgFpRank* r, uint32_t id, const RgFpMatch* m) {
    if(m->score < r->min_score) return;
    size_t pos = r->count;
    while(pos > 0 && r->c[pos - 1].match.score < m->score)
        pos--;
    if(pos >= RG_FP_TOP) return;
    size_t last = r->count < RG_FP_TOP ? r->count : RG_FP_TOP - 1;
    for(size_t i = last; i > pos; i--)
        r->c[i] = r->c[i - 1];
    r->c[pos].id = id;
    r->c[pos].match = *m;
    if(r->count < RG_FP_TOP) r->count++;
}

/* ---- serialization ----------------------------------------------------------- */

static void rg_fp_put16(uint8_t* p, uint16_t v) {
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
}
static void rg_fp_put32(uint8_t* p, uint32_t v) {
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}
static uint16_t rg_fp_get16(const uint8_t* p) {
    return (uint16_t)(p[0] | (p[1] << 8));
}
static uint32_t rg_fp_get32(const uint8_t* p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}

void rg_fp_encode(const RgFingerprint* fp, uint8_t out[RG_FP_BYTES]) {
    memset(out, 0, RG_FP_BYTES);
    uint8_t* p = out;
    *p++ = fp->flags;
    *p++ = fp->preset;
    *p++ = fp->encoding;
    *p++ = fp->alternative;
    *p++ = fp->quality;
    *p++ = fp->noise_pct;
    *p++ = fp->jitter_pct;
    *p++ = fp->preamble;
    *p++ = fp->sync_ratio;
    *p++ = fp->const_pct;
    *p++ = fp->groups;
    *p++ = fp->fit;
    rg_fp_put32(p, fp->frequency);
    p += 4;
    rg_fp_put16(p, fp->te_us);
    p += 2;
    rg_fp_put16(p, fp->bit_count);
    p += 2;
    rg_fp_put16(p, fp->frames);
    p += 2;
    rg_fp_put16(p, fp->repeat_ms);
    p += 2;
    memcpy(p, fp->high_hist, RG_FP_BINS);
    p += RG_FP_BINS;
    memcpy(p, fp->low_hist, RG_FP_BINS);
    p += RG_FP_BINS;
    rg_fp_put32(p, fp->pattern_hash);
    p += 4;
    rg_fp_put32(p, fp->stable_hash);
    p += 4;
    rg_fp_put32(p, fp->proto_hash);
}

bool rg_fp_decode(const uint8_t in[RG_FP_BYTES], RgFingerprint* fp) {
    const uint8_t* p = in;
    memset(fp, 0, sizeof(*fp));
    fp->flags = *p++;
    fp->preset = *p++;
    fp->encoding = *p++;
    fp->alternative = *p++;
    fp->quality = *p++;
    fp->noise_pct = *p++;
    fp->jitter_pct = *p++;
    fp->preamble = *p++;
    fp->sync_ratio = *p++;
    fp->const_pct = *p++;
    fp->groups = *p++;
    fp->fit = *p++;
    fp->frequency = rg_fp_get32(p);
    p += 4;
    fp->te_us = rg_fp_get16(p);
    p += 2;
    fp->bit_count = rg_fp_get16(p);
    p += 2;
    fp->frames = rg_fp_get16(p);
    p += 2;
    fp->repeat_ms = rg_fp_get16(p);
    p += 2;
    memcpy(fp->high_hist, p, RG_FP_BINS);
    p += RG_FP_BINS;
    memcpy(fp->low_hist, p, RG_FP_BINS);
    p += RG_FP_BINS;
    fp->pattern_hash = rg_fp_get32(p);
    p += 4;
    fp->stable_hash = rg_fp_get32(p);
    p += 4;
    fp->proto_hash = rg_fp_get32(p);
    if(fp->flags & ~0x0Fu) return false;
    if(fp->encoding >= RgEncodingCount || fp->alternative >= RgEncodingCount) return false;
    if(fp->quality > RgQualityPoor || fp->noise_pct > 100 || fp->jitter_pct > 100) return false;
    if(fp->const_pct > 100 || fp->fit > 100) return false;
    uint32_t hs = 0, ls = 0;
    for(size_t i = 0; i < RG_FP_BINS; i++) {
        hs += fp->high_hist[i];
        ls += fp->low_hist[i];
    }
    // Shares are rounded: a whole histogram sums to about 100, or is empty.
    if(hs > 106 || ls > 106) return false;
    return true;
}

void rg_fp_header_encode(uint32_t count, uint8_t out[RG_FP_HEADER_BYTES]) {
    memset(out, 0, RG_FP_HEADER_BYTES);
    memcpy(out, RG_FP_INDEX_MAGIC, 4);
    rg_fp_put16(out + 4, RG_FP_INDEX_VERSION);
    rg_fp_put16(out + 6, RG_FP_RECORD_BYTES);
    rg_fp_put32(out + 8, count);
    rg_fp_put32(out + 12, rg_crc32(0, out, 12));
}

int32_t rg_fp_header_decode(const uint8_t in[RG_FP_HEADER_BYTES]) {
    if(memcmp(in, RG_FP_INDEX_MAGIC, 4) != 0) return -1;
    if(rg_fp_get16(in + 4) != RG_FP_INDEX_VERSION) return -1;
    if(rg_fp_get16(in + 6) != RG_FP_RECORD_BYTES) return -1;
    if(rg_fp_get32(in + 12) != rg_crc32(0, in, 12)) return -1;
    uint32_t count = rg_fp_get32(in + 8);
    if(count > RG_FP_INDEX_MAX) return -1;
    return (int32_t)count;
}

static bool rg_fp_name_ok(const char* name, size_t n) {
    if(n == 0 || n >= RG_FP_NAME_MAX) return false;
    for(size_t i = 0; i < n; i++) {
        unsigned char c = (unsigned char)name[i];
        if(c < 0x20 || c == '/' || c == 0x7F) return false;
    }
    return true;
}

bool rg_fp_record_encode(const RgFpRecord* rec, uint8_t out[RG_FP_RECORD_BYTES]) {
    size_t n = rg_fp_strnlen(rec->name, RG_FP_NAME_MAX);
    if(!rg_fp_name_ok(rec->name, n)) return false;
    memset(out, 0, RG_FP_RECORD_BYTES);
    memcpy(out, rec->name, n);
    uint8_t* p = out + RG_FP_NAME_MAX;
    rg_fp_put32(p, rec->size);
    rg_fp_put32(p + 4, rec->timestamp);
    rg_fp_encode(&rec->fp, p + 8);
    rg_fp_put32(p + 8 + RG_FP_BYTES, rg_crc32(0, out, RG_FP_RECORD_BYTES - 4));
    return true;
}

bool rg_fp_record_decode(const uint8_t in[RG_FP_RECORD_BYTES], RgFpRecord* rec) {
    const uint8_t* p = in + RG_FP_NAME_MAX;
    if(rg_fp_get32(p + 8 + RG_FP_BYTES) != rg_crc32(0, in, RG_FP_RECORD_BYTES - 4))
        return false;
    memcpy(rec->name, in, RG_FP_NAME_MAX);
    rec->name[RG_FP_NAME_MAX - 1] = '\0';
    size_t n = rg_fp_strnlen(rec->name, RG_FP_NAME_MAX);
    if(!rg_fp_name_ok(rec->name, n)) return false;
    for(size_t i = n; i < RG_FP_NAME_MAX; i++)
        if(in[i]) return false; // padding must be zero
    rec->size = rg_fp_get32(p);
    rec->timestamp = rg_fp_get32(p + 4);
    return rg_fp_decode(p + 8, &rec->fp);
}

bool rg_fp_record_current(const RgFpRecord* rec, uint32_t size, uint32_t timestamp) {
    return rec->size == size && rec->timestamp == timestamp;
}

#endif /* RG_FEATURE_FINGERPRINT */
