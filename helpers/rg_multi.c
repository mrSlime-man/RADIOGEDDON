#include "rg_multi.h"
#include "../radiogeddon_edition.h"

#if RG_FEATURE_MULTI_COMPARE

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

/* ---- text ------------------------------------------------------------------ */

void rg_text_init(RgText* t, char* buf, size_t size) {
    t->buf = buf;
    t->size = size;
    t->len = 0;
    t->truncated = false;
    if(size) buf[0] = '\0';
}

void rg_text_printf(RgText* t, const char* fmt, ...) {
    if(t->size == 0 || t->truncated) return;
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(t->buf + t->len, t->size - t->len, fmt, ap);
    va_end(ap);
    if(n < 0) return;
    if((size_t)n >= t->size - t->len) {
        // Cut at the last whole line and say so.
        t->buf[t->len] = '\0';
        t->truncated = true;
        const char* more = "(report cut short)\n";
        if(t->size - t->len > strlen(more)) {
            memcpy(t->buf + t->len, more, strlen(more) + 1);
            t->len += strlen(more);
        }
        return;
    }
    t->len += (size_t)n;
}

/* ---- captures ---------------------------------------------------------------- */

static void rg_multi_copy(char* dst, size_t size, const char* src) {
    if(!src) src = "";
    size_t n = strlen(src);
    if(n >= size) n = size - 1;
    memcpy(dst, src, n);
    dst[n] = '\0';
}

void rg_multi_capture_init(RgMultiCapture* cap, const char* name) {
    memset(cap, 0, sizeof(*cap));
    rg_multi_copy(cap->name, sizeof(cap->name), name);
}

void rg_multi_capture_from_analysis(RgMultiCapture* cap, const RgAnalysis* r) {
    cap->source = RgMultiSourceRaw;
    cap->encoding = r->encoding;
    cap->confidence = (int8_t)r->encoding_confidence;
    cap->noise_pct = r->noise_pct;
    cap->te_us = r->te_us;
    cap->frames = r->frame_count > UINT16_MAX ? UINT16_MAX : (uint16_t)r->frame_count;
    cap->signal_frames = r->signal_frames > UINT16_MAX ? UINT16_MAX : (uint16_t)r->signal_frames;
    cap->bit_count = (uint16_t)r->bit_count;
    cap->patterns = 0;
    if(r->bit_count == 0) return;
    // Distinct patterns of the modal length, by how many frames match exactly.
    for(size_t g = 0; g < r->group_count; g++) {
        const RgGroup* grp = &r->groups[g];
        if(grp->bit_count != r->bit_count || grp->frame >= r->frames_kept) continue;
        RgMultiPattern p;
        memcpy(p.bits, r->frames[grp->frame].bits, sizeof(p.bits));
        p.count = grp->exact;
        size_t at = cap->patterns;
        while(at > 0 && cap->pattern[at - 1].count < p.count)
            at--;
        if(at >= RG_MULTI_PATTERNS) continue;
        size_t last = cap->patterns < RG_MULTI_PATTERNS ? cap->patterns : RG_MULTI_PATTERNS - 1;
        for(size_t k = last; k > at; k--)
            cap->pattern[k] = cap->pattern[k - 1];
        cap->pattern[at] = p;
        if(cap->patterns < RG_MULTI_PATTERNS) cap->patterns++;
    }
}

void rg_multi_capture_from_key(
    RgMultiCapture* cap,
    const char* protocol,
    uint32_t bit_count,
    uint64_t key) {
    cap->source = RgMultiSourceDecoded;
    rg_multi_copy(cap->protocol, sizeof(cap->protocol), protocol);
    if(bit_count > 64) bit_count = 64;
    cap->bit_count = (uint16_t)bit_count;
    cap->frames = 1;
    cap->signal_frames = bit_count ? 1 : 0;
    if(bit_count == 0) return;
    RgMultiPattern* p = &cap->pattern[0];
    memset(p, 0, sizeof(*p));
    for(uint32_t i = 0; i < bit_count; i++)
        if((key >> (bit_count - 1u - i)) & 1u) p->bits[i / 8] |= (uint8_t)(0x80u >> (i % 8));
    p->count = 1;
    cap->patterns = 1;
}

int rg_multi_bit(const RgMultiCapture* cap, size_t p, size_t i) {
    if(p >= cap->patterns || i >= cap->bit_count || i >= RG_ANALYZER_MAX_BITS) return -1;
    return (cap->pattern[p].bits[i / 8] & (0x80u >> (i % 8))) ? 1 : 0;
}

static void rg_multi_string(const RgMultiCapture* cap, char* out) {
    size_t n = cap->bit_count > RG_ANALYZER_MAX_BITS ? RG_ANALYZER_MAX_BITS : cap->bit_count;
    for(size_t i = 0; i < n; i++)
        out[i] = rg_multi_bit(cap, 0, i) ? '1' : '0';
    out[n] = '\0';
}

static bool rg_multi_usable(const RgMultiCapture* cap) {
    return cap->source != RgMultiSourceNone && cap->patterns > 0 && cap->bit_count > 0;
}

/* ---- comparison -------------------------------------------------------------- */

static bool rg_multi_near_te(uint32_t a, uint32_t b) {
    uint32_t hi = a > b ? a : b, lo = a > b ? b : a;
    return (uint64_t)(hi - lo) * 100u <= (uint64_t)hi * RG_MULTI_TE_TOL_PCT;
}

/* Value of bits [start, start + len) of a capture's dominant frame. */
static uint64_t rg_multi_value(const RgMultiCapture* cap, size_t start, size_t len) {
    uint64_t v = 0;
    for(size_t k = 0; k < len; k++)
        v = (v << 1) | (uint64_t)(rg_multi_bit(cap, 0, start + k) == 1);
    return v;
}

static void
    rg_multi_classify(const RgMultiCapture* caps, const RgMultiResult* res, RgMultiRun* run) {
    if(run->kind == RgMultiRunConstant) {
        run->values = 1;
        return;
    }
    if(run->length > 64) {
        run->kind = RgMultiRunVaries;
        run->values = (uint8_t)res->compared;
        return;
    }
    uint64_t v[RG_MULTI_MAX_CAPTURES];
    size_t distinct = 0;
    for(size_t k = 0; k < res->compared; k++) {
        v[k] = rg_multi_value(&caps[res->compared_index[k]], run->start, run->length);
        bool seen = false;
        for(size_t j = 0; j < k; j++)
            if(v[j] == v[k]) seen = true;
        if(!seen) distinct++;
    }
    run->values = (uint8_t)distinct;
    // One-hot values (a single bit set) are the classic button field, whatever
    // order the captures come in.
    bool one_hot = true;
    for(size_t k = 0; k < res->compared; k++) {
        unsigned ones = (unsigned)__builtin_popcountll(v[k]);
        if(ones != 1u) one_hot = false;
    }
    // Counter-like: three or more captures, in the order given, each value
    // a small step (at most 4) on from the last, always in one direction.
    bool counter = res->compared >= 3 && !(one_hot && run->length > 1);
    int dir = 0;
    for(size_t k = 1; k < res->compared && counter; k++) {
        if(v[k] == v[k - 1]) {
            counter = false;
            break;
        }
        int d = v[k] > v[k - 1] ? 1 : -1;
        uint64_t step = v[k] > v[k - 1] ? v[k] - v[k - 1] : v[k - 1] - v[k];
        if((dir && d != dir) || step > 4u) counter = false;
        dir = d;
    }
    if(counter) {
        run->kind = RgMultiRunCounter;
    } else if(run->length <= RG_MULTI_MAX_BUTTON_BITS) {
        run->kind = RgMultiRunButton;
    } else {
        run->kind = RgMultiRunVaries;
    }
}

void rg_multi_compare(const RgMultiCapture* caps, size_t count, RgMultiResult* out) {
    memset(out, 0, sizeof(*out));
    if(count > RG_MULTI_MAX_CAPTURES) count = RG_MULTI_MAX_CAPTURES;
    out->captures = count;
    out->similarity = -1;
    if(count == 0) return;

    out->same_frequency = true;
    out->same_preset = true;
    out->same_encoding = true;
    out->same_te = true;
    out->same_length = true;
    const RgMultiCapture* first = NULL;
    for(size_t i = 0; i < count; i++) {
        const RgMultiCapture* c = &caps[i];
        out->same_as[i] = '-';
        if(!rg_multi_usable(c)) continue;
        out->usable++;
        // A decoder's frame is confirmed; a RAW frame seen once is not backed
        // by a repeat.
        if(c->source == RgMultiSourceRaw && c->pattern[0].count < 2) out->single_frame_captures++;
        if(!first) {
            first = c;
            continue;
        }
        uint32_t df = c->frequency > first->frequency ? c->frequency - first->frequency :
                                                        first->frequency - c->frequency;
        if(df > RG_MULTI_FREQ_TOL_HZ) out->same_frequency = false;
        if(strcmp(c->preset, first->preset) != 0) out->same_preset = false;
        if(c->source != first->source || c->encoding != first->encoding ||
           strcmp(c->protocol, first->protocol) != 0)
            out->same_encoding = false;
        if(c->source == RgMultiSourceRaw && first->source == RgMultiSourceRaw &&
           !rg_multi_near_te(c->te_us, first->te_us))
            out->same_te = false;
        if(c->bit_count != first->bit_count) out->same_length = false;
    }
    if(out->usable == 0) {
        out->same_frequency = out->same_preset = out->same_encoding = out->same_te =
            out->same_length = false;
        return;
    }

    // The frame length most captures share (ties: the longer).
    size_t best_n = 0;
    for(size_t i = 0; i < count; i++) {
        if(!rg_multi_usable(&caps[i])) continue;
        size_t n = 0;
        for(size_t j = 0; j < count; j++)
            if(rg_multi_usable(&caps[j]) && caps[j].bit_count == caps[i].bit_count) n++;
        if(n > best_n || (n == best_n && caps[i].bit_count > out->length)) {
            best_n = n;
            out->length = caps[i].bit_count;
        }
    }
    for(size_t i = 0; i < count; i++)
        if(rg_multi_usable(&caps[i]) && caps[i].bit_count == out->length)
            out->compared_index[out->compared++] = (uint8_t)i;

    // Identical dominant frames share a letter.
    char next = 'A';
    for(size_t i = 0; i < count; i++) {
        if(!rg_multi_usable(&caps[i]) || out->same_as[i] != '-') continue;
        out->same_as[i] = next;
        for(size_t j = i + 1; j < count; j++) {
            if(rg_multi_usable(&caps[j]) && caps[j].bit_count == caps[i].bit_count &&
               memcmp(
                   caps[j].pattern[0].bits,
                   caps[i].pattern[0].bits,
                   sizeof(caps[i].pattern[0].bits)) == 0)
                out->same_as[j] = next;
        }
        out->distinct_frames++;
        next = next < 'Z' ? (char)(next + 1) : next;
    }

    // Mean pairwise similarity of the dominant frames (best alignment).
    char a[RG_ANALYZER_MAX_BITS + 1], b[RG_ANALYZER_MAX_BITS + 1];
    uint32_t sum = 0, pairs = 0;
    for(size_t i = 0; i < count; i++) {
        if(!rg_multi_usable(&caps[i])) continue;
        rg_multi_string(&caps[i], a);
        for(size_t j = i + 1; j < count; j++) {
            if(!rg_multi_usable(&caps[j])) continue;
            rg_multi_string(&caps[j], b);
            size_t na = strlen(a), nb = strlen(b);
            int shift = 0;
            size_t ov = 0;
            size_t m = rg_analyzer_align(a, na, b, nb, RG_ANALYZER_MAX_SHIFT, &shift, &ov);
            size_t longer = na > nb ? na : nb;
            sum += (uint32_t)(m * 100u / longer);
            pairs++;
        }
    }
    if(pairs) out->similarity = (int)(sum / pairs);

    // Bit positions across the compared captures' dominant frames.
    if(out->compared < 2) return;
    const RgMultiCapture* ref = &caps[out->compared_index[0]];
    int prev = -1;
    for(size_t k = 0; k < out->length; k++) {
        bool changes = false;
        for(size_t c = 1; c < out->compared; c++)
            if(rg_multi_bit(&caps[out->compared_index[c]], 0, k) != rg_multi_bit(ref, 0, k))
                changes = true;
        if(changes)
            out->changing_bits++;
        else
            out->const_bits++;
        int kind = changes ? RgMultiRunVaries : RgMultiRunConstant;
        if(kind != prev) {
            if(out->runs == RG_MULTI_MAX_RUNS) {
                out->runs_truncated = true;
                break;
            }
            RgMultiRun* r = &out->run[out->runs++];
            r->start = (uint16_t)k;
            r->length = 0;
            r->kind = (uint8_t)kind;
            prev = kind;
        }
        out->run[out->runs - 1].length++;
    }
    for(size_t i = 0; i < out->runs; i++)
        rg_multi_classify(caps, out, &out->run[i]);
}

/* ---- report ------------------------------------------------------------------ */

static void rg_multi_freq(RgText* t, uint32_t hz) {
    rg_text_printf(
        t, "%lu.%03lu", (unsigned long)(hz / 1000000u), (unsigned long)(hz % 1000000u / 1000u));
}

static void rg_multi_bits_line(RgText* t, const RgMultiCapture* cap) {
    size_t n = cap->bit_count > RG_ANALYZER_MAX_BITS ? RG_ANALYZER_MAX_BITS : cap->bit_count;
    char s[RG_ANALYZER_MAX_BITS + 1];
    for(size_t i = 0; i < n; i++)
        s[i] = rg_multi_bit(cap, 0, i) ? '1' : '0';
    s[n] = '\0';
    rg_text_printf(t, "%s\n", s);
}

void rg_multi_report(const RgMultiCapture* caps, size_t count, const RgMultiResult* res, RgText* t) {
    rg_text_printf(
        t,
        "OBSERVED = in the files\nor their timing.\nHEURISTIC = rule of thumb.\n"
        "HYPOTHESIS = reading of\ninferred bits; may be\nwrong. Nothing here is\n"
        "decrypted or verified.\n----------------\n");
    rg_text_printf(
        t, "%u captures, %u with\nframes\n", (unsigned)res->captures, (unsigned)res->usable);
    for(size_t i = 0; i < count && i < RG_MULTI_MAX_CAPTURES; i++) {
        const RgMultiCapture* c = &caps[i];
        rg_text_printf(t, "%u %s\n  ", (unsigned)(i + 1), c->name);
        rg_multi_freq(t, c->frequency);
        if(c->source == RgMultiSourceDecoded) {
            rg_text_printf(
                t, " %s %ub\n  [CONFIRMED] decoder\n", c->protocol, (unsigned)c->bit_count);
        } else if(c->source == RgMultiSourceRaw && c->bit_count) {
            rg_text_printf(
                t,
                " %s %ub x%u\n",
                rg_analyzer_encoding_name(c->encoding),
                (unsigned)c->bit_count,
                (unsigned)c->pattern[0].count);
        } else if(c->source == RgMultiSourceRaw) {
            rg_text_printf(t, " no clean frame\n");
        } else {
            rg_text_printf(t, " not readable\n");
        }
    }
    rg_text_printf(t, "----------------\n");
    if(res->usable == 0) {
        rg_text_printf(
            t, "No capture has a frame\nto compare. Record RAW\nwith a few presses each.\n");
        return;
    }
    const RgMultiCapture* first = NULL;
    for(size_t i = 0; i < count && !first; i++)
        if(caps[i].patterns) first = &caps[i];
    if(!first) return; // usable > 0 means one exists

    rg_text_printf(t, "[OBSERVED] Frequency:\n %s", res->same_frequency ? "same, " : "differs\n");
    if(res->same_frequency) {
        rg_multi_freq(t, first->frequency);
        rg_text_printf(t, " MHz\n");
    }
    rg_text_printf(
        t,
        "[OBSERVED] Preset:\n %s%s\n",
        res->same_preset ? "same, " : "differs",
        res->same_preset ? first->preset : "");
    rg_text_printf(t, "[OBSERVED] Timing Te:\n %s", res->same_te ? "same" : "differs");
    if(res->same_te && first->source == RgMultiSourceRaw)
        rg_text_printf(t, " ~%lu us", (unsigned long)first->te_us);
    rg_text_printf(
        t,
        "\n[HYPOTHESIS] Encoding:\n %s\n",
        res->same_encoding ? "the same in all" : "differs between files");
    rg_text_printf(t, "[HYPOTHESIS] Frame length:\n ");
    if(res->same_length) {
        rg_text_printf(t, "%u bits in all\n", (unsigned)res->length);
    } else {
        for(size_t i = 0; i < count; i++)
            if(caps[i].patterns)
                rg_text_printf(t, "%u:%ub ", (unsigned)(i + 1), (unsigned)caps[i].bit_count);
        rg_text_printf(t, "\n");
    }
    if(res->similarity >= 0)
        rg_text_printf(t, "[HEURISTIC] Similarity:\n %d%% (best alignment)\n", res->similarity);

    // Which captures carry the same frame.
    rg_text_printf(
        t,
        "[HYPOTHESIS] Frames:\n %u different of %u\n",
        (unsigned)res->distinct_frames,
        (unsigned)res->usable);
    for(size_t k = 0; k < res->distinct_frames && k < 26; k++) {
        char g = (char)('A' + k);
        rg_text_printf(t, " %c:", g);
        for(size_t i = 0; i < count; i++)
            if(res->same_as[i] == g) rg_text_printf(t, " %u", (unsigned)(i + 1));
        rg_text_printf(t, "\n");
    }
    for(size_t i = 0; i < count; i++) {
        if(caps[i].patterns > 1) {
            rg_text_printf(
                t,
                "%u holds %u patterns\n(several presses or\nbuttons in one file)\n",
                (unsigned)(i + 1),
                (unsigned)caps[i].patterns);
        }
    }

    rg_text_printf(t, "----------------\n");
    if(res->compared < 2) {
        rg_text_printf(
            t, "Fewer than two captures\nshare a frame length:\nno bit-by-bit compare.\n");
        return;
    }
    rg_text_printf(
        t,
        "Bits of %u captures\n(%u bits; . same,\nX changes):\n",
        (unsigned)res->compared,
        (unsigned)res->length);
    char map[RG_ANALYZER_MAX_BITS + 1];
    size_t n = 0;
    for(size_t r = 0; r < res->runs; r++)
        for(size_t k = 0; k < res->run[r].length && n < RG_ANALYZER_MAX_BITS; k++)
            map[n++] = res->run[r].kind == RgMultiRunConstant ? '.' : 'X';
    map[n] = '\0';
    rg_text_printf(t, "%s\n", map);
    for(size_t k = 0; k < res->compared; k++) {
        rg_text_printf(t, "%u ", (unsigned)(res->compared_index[k] + 1));
        rg_multi_bits_line(t, &caps[res->compared_index[k]]);
    }
    rg_text_printf(
        t,
        "[OBSERVED] %u constant,\n%u changing bits\n",
        (unsigned)res->const_bits,
        (unsigned)res->changing_bits);
    if(res->changing_bits == 0) {
        rg_text_printf(
            t,
            "[HEURISTIC] Every capture\nsends the same frame:\nsame button of a fixed-\ncode remote, or a repeat.\n");
    }
    for(size_t r = 0; r < res->runs; r++) {
        const RgMultiRun* run = &res->run[r];
        unsigned a = run->start, b = (unsigned)(run->start + run->length - 1u);
        switch(run->kind) {
        case RgMultiRunConstant:
            if(res->changing_bits && run->length >= RG_MULTI_MIN_ID_BITS) {
                rg_text_printf(
                    t,
                    "Bits %u-%u constant\n[HYPOTHESIS] could be an\nID, sync or fixed field;\nnot verified as a serial\n",
                    a,
                    b);
            } else {
                rg_text_printf(t, "Bits %u-%u constant\n", a, b);
            }
            break;
        case RgMultiRunButton:
            rg_text_printf(
                t,
                "Bits %u-%u change, %u\nvalues [HYPOTHESIS]\nbutton/command-like\n",
                a,
                b,
                (unsigned)run->values);
            break;
        case RgMultiRunCounter:
            rg_text_printf(
                t,
                "Bits %u-%u step in order\n[HYPOTHESIS] counter-like\n(order as selected)\n",
                a,
                b);
            break;
        default:
            rg_text_printf(
                t,
                "Bits %u-%u change, %u\nvalues, no simple rule\n[HYPOTHESIS] data, rolling\nor encrypted part\n",
                a,
                b,
                (unsigned)run->values);
            break;
        }
    }
    if(res->runs_truncated) rg_text_printf(t, "(more runs not listed)\n");
    rg_text_printf(t, "----------------\n[HEURISTIC] Noise check:\n");
    if(res->single_frame_captures == 0) {
        rg_text_printf(
            t,
            "every capture repeats\nits frame, so the\ndifferences are steady,\nunlikely to be noise.\n");
    } else {
        rg_text_printf(
            t,
            "%u capture(s) hold their\nframe only once: a\ndifference there may be\nnoise. Record longer\npresses to confirm.\n",
            (unsigned)res->single_frame_captures);
    }
}

#endif
