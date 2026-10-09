#include "radiogeddon_analysis.h"
#include "radiogeddon_dsp.h"
#include "rg_analyzer.h"

#include <lib/flipper_format/flipper_format.h>
#include <lib/subghz/subghz_protocol_registry.h>
#include <lib/subghz/registry.h>
#include <lib/subghz/types.h>
#include <stdlib.h>

static void radiogeddon_format_freq(FuriString* out, uint32_t hz) {
    uint32_t mhz = hz / 1000000;
    uint32_t khz = (hz % 1000000) / 1000;
    furi_string_cat_printf(out, "%lu.%03lu MHz", (unsigned long)mhz, (unsigned long)khz);
}

void radiogeddon_analysis_describe(const RadioGeddonLoadedSignal* sig, FuriString* out) {
    furi_string_cat_printf(out, "Name: %s\n", furi_string_get_cstr(sig->name));
    furi_string_cat_printf(out, "Protocol: %s\n", furi_string_get_cstr(sig->protocol));
    furi_string_cat_str(out, "Freq: ");
    radiogeddon_format_freq(out, sig->frequency);
    furi_string_cat_str(out, "\n");
    if(furi_string_size(sig->preset)) {
        furi_string_cat_printf(out, "Preset: %s\n", furi_string_get_cstr(sig->preset));
    }
    if(sig->kind == RadioGeddonSignalKindRaw) {
        furi_string_cat_printf(out, "Type: RAW capture\n");
        furi_string_cat_printf(out, "Samples: %u\n", (unsigned)sig->raw_sample_count);
        furi_string_cat_printf(
            out,
            "Pulse: %lu-%lu us\n",
            (unsigned long)sig->raw_min_us,
            (unsigned long)sig->raw_max_us);
    } else if(sig->kind == RadioGeddonSignalKindProtocol) {
        furi_string_cat_printf(out, "Type: Decoded protocol\n");
        if(sig->bit_count)
            furi_string_cat_printf(out, "Bits: %lu\n", (unsigned long)sig->bit_count);
        if(sig->key) {
            furi_string_cat_printf(
                out,
                "Key: %08lX%08lX\n",
                (unsigned long)(sig->key >> 32),
                (unsigned long)(sig->key & 0xFFFFFFFF));
        }
    } else {
        furi_string_cat_printf(out, "Type: Unknown\n");
    }
}

/* ---- RAW pulse-timing clustering --------------------------------------- */

static void radiogeddon_analysis_analyze_raw(Storage* storage, const char* path, FuriString* out) {
    FlipperFormat* ff = flipper_format_file_alloc(storage);
    FuriString* value = furi_string_alloc();
    FuriString* type = furi_string_alloc();
    uint32_t version = 0;

    RadioGeddonCluster clusters[RADIOGEDDON_MAX_CLUSTERS] = {0};
    size_t cluster_n = 0;
    size_t edges = 0;

    do {
        if(!flipper_format_file_open_existing(ff, path)) break;
        if(!flipper_format_read_header(ff, type, &version)) break;
        uint32_t dummy_min = 0, dummy_max = 0;
        while(flipper_format_read_string(ff, "RAW_Data", value)) {
            radiogeddon_dsp_parse_line(
                furi_string_get_cstr(value),
                &edges,
                &dummy_min,
                &dummy_max,
                clusters,
                &cluster_n,
                RADIOGEDDON_MAX_CLUSTERS);
        }
    } while(false);

    furi_string_cat_printf(out, "[HEURISTIC] RAW timing\n");
    furi_string_cat_printf(out, "Edges: %u\n", (unsigned)edges);
    furi_string_cat_printf(out, "Timing groups: %u\n", (unsigned)cluster_n);

    radiogeddon_dsp_sort_clusters(clusters, cluster_n);
    for(size_t i = 0; i < cluster_n; i++) {
        furi_string_cat_printf(
            out,
            " %lu us x%lu\n",
            (unsigned long)clusters[i].center,
            (unsigned long)clusters[i].count);
    }
    if(cluster_n > 0) {
        furi_string_cat_printf(out, "Est. base Te: ~%lu us\n", (unsigned long)clusters[0].center);
        furi_string_cat_str(
            out, "Note: groups suggest OOK\nsymbol widths; verify with\ndecoder.\n");
    }

    furi_string_free(value);
    furi_string_free(type);
    flipper_format_free(ff);
}

void radiogeddon_analysis_analyze(
    Storage* storage,
    const char* path,
    const RadioGeddonLoadedSignal* sig,
    FuriString* out) {
    if(sig->kind == RadioGeddonSignalKindRaw) {
        radiogeddon_analysis_analyze_raw(storage, path, out);
    } else if(sig->kind == RadioGeddonSignalKindProtocol) {
        furi_string_cat_printf(out, "[CONFIRMED] %s\n", furi_string_get_cstr(sig->protocol));
        furi_string_cat_printf(out, "Decoder matched this\nsignal structure.\n");
        if(sig->bit_count) {
            furi_string_cat_printf(out, "Bit length: %lu\n", (unsigned long)sig->bit_count);
            furi_string_cat_printf(
                out,
                "Bytes: %lu (+%lu bits)\n",
                (unsigned long)(sig->bit_count / 8),
                (unsigned long)(sig->bit_count % 8));
        }
        if(sig->key) {
            furi_string_cat_printf(
                out,
                "Key: %08lX%08lX\n",
                (unsigned long)(sig->key >> 32),
                (unsigned long)(sig->key & 0xFFFFFFFF));
            // Serial/button heuristic on the low bytes.
            uint8_t btn = (uint8_t)(sig->key & 0x0F);
            furi_string_cat_printf(out, "Low nibble: 0x%X\n", btn);
            furi_string_cat_str(out, "[HEURISTIC] low bits often\nencode button/command.\n");
        }
    } else {
        furi_string_cat_str(
            out,
            "No decoder matched.\nOnly RAW timing analysis\nis available for this\nsignal.\n");
    }
}

/* ---- Crypto characteristics -------------------------------------------- */

void radiogeddon_analysis_crypto(const RadioGeddonLoadedSignal* sig, FuriString* out) {
    if(sig->kind != RadioGeddonSignalKindProtocol) {
        furi_string_cat_str(
            out,
            "[HEURISTIC] No protocol\ndecoded. Crypto class\nunknown for RAW signals.\n"
            "Decode first, or compare\nmultiple captures to spot\nchanging fields.\n");
        return;
    }

    const SubGhzProtocol* proto = subghz_protocol_registry_get_by_name(
        &subghz_protocol_registry, furi_string_get_cstr(sig->protocol));

    if(!proto) {
        furi_string_cat_str(out, "[HEURISTIC] Protocol not in\nregistry; cannot classify.\n");
        return;
    }

    switch(proto->type) {
    case SubGhzProtocolTypeStatic:
        furi_string_cat_printf(out, "[CONFIRMED] Static code\n");
        furi_string_cat_str(
            out, "Fixed payload: same code\nevery press. No rolling\ncounter, no encryption.\n");
        break;
    case SubGhzProtocolTypeDynamic:
        furi_string_cat_printf(out, "[CONFIRMED] Dynamic code\n");
        furi_string_cat_str(
            out,
            "Rolling counter present.\nPayload changes each\npress (e.g. KeeLoq-style).\n"
            "Likely encrypted hop\ncode. Key recovery is\nNOT performed.\n");
        break;
    case SubGhzProtocolWeatherStation:
        furi_string_cat_str(
            out, "[CONFIRMED] Telemetry\n(weather/sensor). Usually\nunencrypted data frame.\n");
        break;
    default:
        furi_string_cat_str(out, "[HEURISTIC] Unclassified\nprotocol type.\n");
        break;
    }

    if(sig->key) {
        // Simple byte-variety heuristic over the key value.
        int nonzero = 0, distinct = 0;
        radiogeddon_dsp_key_stats(sig->key, &nonzero, &distinct);
        furi_string_cat_printf(
            out, "[HEURISTIC] key bytes:\n %d non-zero, %d distinct\n", nonzero, distinct);
        if(distinct >= 6) {
            furi_string_cat_str(out, "High byte variety ->\npossibly encrypted.\n");
        }
    }
}

/* ---- Comparison -------------------------------------------------------- */

static void radiogeddon_compare_line(
    FuriString* out,
    const char* label,
    bool equal,
    const char* a,
    const char* b) {
    if(equal) {
        furi_string_cat_printf(out, "= %s: %s\n", label, a);
    } else {
        furi_string_cat_printf(out, "~ %s:\n  A:%s\n  B:%s\n", label, a, b);
    }
}

void radiogeddon_analysis_compare(
    const RadioGeddonLoadedSignal* a,
    const RadioGeddonLoadedSignal* b,
    FuriString* out) {
    furi_string_cat_str(out, "= constant  ~ changed\n");

    radiogeddon_compare_line(
        out,
        "Proto",
        furi_string_equal(a->protocol, b->protocol),
        furi_string_get_cstr(a->protocol),
        furi_string_get_cstr(b->protocol));

    FuriString* fa = furi_string_alloc();
    FuriString* fb = furi_string_alloc();
    radiogeddon_format_freq(fa, a->frequency);
    radiogeddon_format_freq(fb, b->frequency);
    radiogeddon_compare_line(
        out,
        "Freq",
        a->frequency == b->frequency,
        furi_string_get_cstr(fa),
        furi_string_get_cstr(fb));
    furi_string_free(fa);
    furi_string_free(fb);

    if(a->kind == RadioGeddonSignalKindProtocol || b->kind == RadioGeddonSignalKindProtocol) {
        FuriString* ka = furi_string_alloc_printf(
            "%08lX%08lX", (unsigned long)(a->key >> 32), (unsigned long)(a->key & 0xFFFFFFFF));
        FuriString* kb = furi_string_alloc_printf(
            "%08lX%08lX", (unsigned long)(b->key >> 32), (unsigned long)(b->key & 0xFFFFFFFF));
        radiogeddon_compare_line(
            out, "Key", a->key == b->key, furi_string_get_cstr(ka), furi_string_get_cstr(kb));

        if(a->key != b->key && a->key && b->key && furi_string_equal(a->protocol, b->protocol)) {
            uint64_t diff = (a->key > b->key) ? (a->key - b->key) : (b->key - a->key);
            furi_string_cat_printf(
                out, "[HEURISTIC] key delta:\n %lu\n", (unsigned long)(diff & 0xFFFFFFFF));
            furi_string_cat_str(out, "Small delta across presses\nsuggests rolling counter.\n");
        }
        furi_string_free(ka);
        furi_string_free(kb);
    }

    if(a->kind == RadioGeddonSignalKindRaw && b->kind == RadioGeddonSignalKindRaw) {
        furi_string_cat_printf(
            out,
            "RAW A:%u smp  B:%u smp\n",
            (unsigned)a->raw_sample_count,
            (unsigned)b->raw_sample_count);
    }
}

/* ---- Unknown / RAW structural analysis (signal engine) ----------------- */

#define RG_ANALYSIS_CHUNK      64u
/* Free heap kept back beyond the analyzer itself, for the GUI and storage. */
#define RG_ANALYSIS_HEAP_SPARE (6u * 1024u)

RgAnalyzer*
    radiogeddon_analysis_run_raw(RadioGeddonRawFile* file, RadioGeddonAnalysisStatus* status) {
    *status = RadioGeddonAnalysisOk;
    if(memmgr_heap_get_max_free_block() < sizeof(RgAnalyzer) + RG_ANALYSIS_HEAP_SPARE) {
        *status = RadioGeddonAnalysisNoMemory;
        return NULL;
    }
    RgAnalyzer* a = malloc(sizeof(RgAnalyzer));
    int32_t chunk[RG_ANALYSIS_CHUNK];
    rg_analyzer_begin(a);
    do {
        rg_raw_reader_rewind(&file->reader);
        size_t n;
        while((n = rg_raw_reader_read(&file->reader, chunk, RG_ANALYSIS_CHUNK)) > 0)
            rg_analyzer_feed(a, chunk, n);
    } while(rg_analyzer_next_pass(a));

    if(file->reader.corrupt) *status = RadioGeddonAnalysisCorrupt;
    if(a->result.sample_count == 0) {
        *status = file->reader.corrupt ? RadioGeddonAnalysisCorrupt : RadioGeddonAnalysisNoRaw;
        free(a);
        a = NULL;
    }
    return a;
}

RgAnalyzer* radiogeddon_analysis_run_file(
    Storage* storage,
    const char* path,
    RadioGeddonAnalysisStatus* status) {
    RadioGeddonRawFile* file = radiogeddon_storage_raw_open(storage, path);
    if(!file) {
        *status = RadioGeddonAnalysisOpenFailed;
        return NULL;
    }
    RgAnalyzer* a = radiogeddon_analysis_run_raw(file, status);
    radiogeddon_storage_raw_close(file);
    return a;
}

static void radiogeddon_cat_time(FuriString* out, uint64_t us) {
    furi_string_cat_printf(
        out,
        "%lu.%03lus",
        (unsigned long)(us / 1000000u),
        (unsigned long)((us % 1000000u) / 1000u));
}

/* Hex of a '0'/'1' string, zero-padded at the front to whole nibbles. */
static void radiogeddon_cat_hex(FuriString* out, const char* bits, size_t n) {
    size_t pad = (4 - n % 4) % 4;
    unsigned nibble = 0;
    for(size_t i = 0; i < n + pad; i++) {
        nibble = (nibble << 1) | (i >= pad && bits[i - pad] == '1' ? 1u : 0u);
        if(i % 4 == 3) {
            furi_string_push_back(out, "0123456789ABCDEF"[nibble & 0xF]);
            nibble = 0;
        }
    }
}

static void radiogeddon_cat_peaks(FuriString* out, const RgPeak* p, size_t n) {
    if(n == 0) {
        furi_string_cat_str(out, " none\n");
        return;
    }
    for(size_t i = 0; i < n; i++) {
        furi_string_cat_printf(
            out,
            "%s%lu x%lu",
            (i % 2) ? "  " : " ",
            (unsigned long)p[i].center_us,
            (unsigned long)p[i].count);
        if(i % 2 == 1 || i + 1 == n) furi_string_cat_str(out, "\n");
    }
}

static char radiogeddon_group_letter(uint8_t group) {
    return group < RG_ANALYZER_MAX_GROUPS ? (char)('A' + group) : '?';
}

void radiogeddon_analysis_cat_status(FuriString* out, RadioGeddonAnalysisStatus status) {
    switch(status) {
    case RadioGeddonAnalysisNoMemory:
        furi_string_cat_str(
            out, "Not enough free memory\nfor the analysis. Close\nother apps and retry.\n");
        break;
    case RadioGeddonAnalysisOpenFailed:
        furi_string_cat_str(out, "Could not open the file.\n");
        break;
    case RadioGeddonAnalysisCorrupt:
        furi_string_cat_str(out, "RAW data is damaged:\nnon-numeric values were\nskipped.\n");
        break;
    case RadioGeddonAnalysisNoRaw:
        furi_string_cat_str(
            out,
            "No RAW timing data.\nUnknown-protocol analysis\nneeds a RAW capture.\nRecord one via Receive &\nRecord (Left button).\n");
        break;
    default:
        break;
    }
}

static void radiogeddon_report_observed(const RgAnalysis* r, FuriString* out) {
    furi_string_cat_str(out, "[OBSERVED] timing\n");
    furi_string_cat_printf(out, "Samples: %u, ", (unsigned)r->sample_count);
    radiogeddon_cat_time(out, r->duration_us);
    furi_string_cat_str(out, "\nHigh peaks (us x count):\n");
    radiogeddon_cat_peaks(out, r->high_peaks, r->high_peak_count);
    furi_string_cat_str(out, "Low peaks:\n");
    radiogeddon_cat_peaks(out, r->low_peaks, r->low_peak_count);
    furi_string_cat_printf(
        out,
        "Noise: %u%%  Jitter: ~%u%%\nQuality: %s\n",
        r->noise_pct,
        r->jitter_pct,
        rg_analyzer_quality_name(r->quality));
    furi_string_cat_printf(
        out,
        "Gap >= %lu us splits\nFrames: %u",
        (unsigned long)r->gap_us,
        (unsigned)r->frame_count);
    if(r->burst_count) furi_string_cat_printf(out, " (+%u bursts)", (unsigned)r->burst_count);
    furi_string_cat_str(out, "\n");
}

static void radiogeddon_report_fields(const RgAnalysis* r, FuriString* out) {
    if(!r->have_field_diff) {
        if(r->signal_frames < 2)
            furi_string_cat_str(
                out,
                "Capture more presses to\ncompare frames and reveal\nconstant vs changing\nfields.\n");
        return;
    }
    furi_string_cat_printf(
        out,
        "Fields over %u frames\n(. same, X changes):\n%s\n",
        (unsigned)(r->compared + 1),
        r->field_map);
    if(r->changing_bits == 0) {
        furi_string_cat_printf(
            out, "All %u bits constant:\nlikely a fixed code.\n", (unsigned)r->const_bits);
        return;
    }
    furi_string_cat_printf(
        out, "%u bits change: counter,\nbutton or encrypted data.\n", (unsigned)r->changing_bits);
    if(r->id_len > 0) {
        furi_string_cat_printf(
            out,
            "Constant bits %u-%u may\nbe a device ID: 0x",
            (unsigned)r->id_start,
            (unsigned)(r->id_start + r->id_len - 1));
        if(strlen(r->bits) >= r->id_start + r->id_len)
            radiogeddon_cat_hex(out, r->bits + r->id_start, r->id_len);
        furi_string_cat_str(out, "\n");
    }
    furi_string_cat_str(out, "Not verified; nothing is\ndecrypted or predicted.\n");
}

static void radiogeddon_report_hypothesis(const RgAnalysis* r, FuriString* out) {
    furi_string_cat_str(out, "[HYPOTHESIS] structure\n");
    if(r->encoding == RgEncodingUnknown) {
        furi_string_cat_str(
            out,
            "No frame fits PWM, PPM\nor Manchester. It may be\nnoise, FSK or an encoding\nnot modelled here.\n");
        return;
    }
    furi_string_cat_printf(
        out,
        "%s, confidence %d%%\n",
        rg_analyzer_encoding_name(r->encoding),
        r->encoding_confidence);
    if(r->alternative != RgEncodingUnknown) {
        furi_string_cat_printf(
            out,
            "Alt: %s fits %d%%\n",
            rg_analyzer_encoding_name(r->alternative),
            r->alternative_fit);
    }
    furi_string_cat_printf(out, "Te ~%lu us\n", (unsigned long)r->te_us);
    switch(r->encoding) {
    case RgEncodingPWM:
        furi_string_cat_printf(
            out,
            "Pulses %lu/%lu us,\n1 = long pulse\n",
            (unsigned long)r->params.pwm_short_us,
            (unsigned long)r->params.pwm_long_us);
        break;
    case RgEncodingPPM:
        furi_string_cat_printf(
            out,
            "Gaps %lu/%lu us,\n1 = long gap\n",
            (unsigned long)r->params.ppm_short_us,
            (unsigned long)r->params.ppm_long_us);
        break;
    default:
        furi_string_cat_str(out, "1 = low-to-high (IEEE);\ninvert for G.E. Thomas\n");
        break;
    }
    furi_string_cat_printf(
        out,
        "Decode fit: %d%%\nSignal frames: %u of %u\n",
        r->fit_pct,
        (unsigned)r->signal_frames,
        (unsigned)r->frame_count);
    if(r->signal_frames == 0) return;
    furi_string_cat_printf(
        out, "Bit length: %u (%u frames)\n", (unsigned)r->bit_count, (unsigned)r->bit_count_frames);

    char bits[RG_ANALYZER_MAX_BITS + 1];
    for(size_t g = 0; g < r->group_count; g++) {
        const RgGroup* grp = &r->groups[g];
        rg_analyzer_frame_bits(&r->frames[grp->frame], bits);
        furi_string_cat_printf(
            out, "Pattern %c x%u", radiogeddon_group_letter((uint8_t)g), (unsigned)grp->exact);
        if(grp->aligned) furi_string_cat_printf(out, " +%u shifted", (unsigned)grp->aligned);
        furi_string_cat_printf(out, ":\n%s\n0x", bits);
        radiogeddon_cat_hex(out, bits, grp->bit_count);
        furi_string_cat_str(out, "\n");
    }
    if(r->ungrouped)
        furi_string_cat_printf(out, "+%u frames, other patterns\n", (unsigned)r->ungrouped);
    radiogeddon_report_fields(r, out);
}

static void radiogeddon_report_frames(const RgAnalysis* r, FuriString* out) {
    if(r->frames_kept == 0) return;
    furi_string_cat_str(out, "Frames (start, bits, pat)\n");
    for(size_t i = 0; i < r->frames_kept; i++) {
        const RgFrame* f = &r->frames[i];
        furi_string_cat_printf(out, "%2u ", (unsigned)(i + 1));
        radiogeddon_cat_time(out, f->start_us);
        if(f->fit < RG_ANALYZER_GOOD_FIT) {
            furi_string_cat_str(out, " noise\n");
            continue;
        }
        furi_string_cat_printf(out, " %ub ", (unsigned)f->bit_count);
        if(f->group == RG_ANALYZER_NO_GROUP) {
            furi_string_cat_str(out, "?");
        } else {
            furi_string_push_back(out, radiogeddon_group_letter(f->group));
            if(f->shift) furi_string_cat_printf(out, "%+d", f->shift);
        }
        if(f->truncated) furi_string_cat_str(out, " long");
        furi_string_cat_str(out, "\n");
    }
    if(r->frame_count > r->frames_kept)
        furi_string_cat_printf(
            out, "(first %u of %u frames)\n", (unsigned)r->frames_kept, (unsigned)r->frame_count);
}

void radiogeddon_analysis_unknown(Storage* storage, const char* path, FuriString* out) {
    RadioGeddonAnalysisStatus status;
    RgAnalyzer* a = radiogeddon_analysis_run_file(storage, path, &status);
    if(!a) {
        radiogeddon_analysis_cat_status(out, status);
        return;
    }
    const RgAnalysis* r = &a->result;

    furi_string_cat_str(
        out,
        "OBSERVED = measured in\nthe file. HYPOTHESIS =\ninferred, may be wrong.\nOnly firmware decoders\n(Receive) confirm a\nprotocol.\n");
    if(status == RadioGeddonAnalysisCorrupt) radiogeddon_analysis_cat_status(out, status);
    furi_string_cat_str(out, "----------------\n");
    radiogeddon_report_observed(r, out);
    furi_string_cat_str(out, "----------------\n");
    radiogeddon_report_hypothesis(r, out);
    furi_string_cat_str(out, "----------------\n");
    radiogeddon_report_frames(r, out);
    free(a);
}

/* Dominant pattern of one file, kept while the other file is analysed. */
typedef struct {
    RgEncoding encoding;
    size_t bits_len;
    size_t frames;
    char bits[RG_ANALYZER_MAX_BITS + 1];
} RadioGeddonPattern;

static bool
    radiogeddon_dominant_pattern(Storage* storage, const char* path, RadioGeddonPattern* p) {
    RadioGeddonAnalysisStatus status;
    RgAnalyzer* a = radiogeddon_analysis_run_file(storage, path, &status);
    if(!a) return false;
    const RgAnalysis* r = &a->result;
    bool ok = r->group_count > 0;
    if(ok) {
        p->encoding = r->encoding;
        p->bits_len = strlen(r->bits);
        p->frames = r->groups[0].exact + r->groups[0].aligned;
        memcpy(p->bits, r->bits, p->bits_len + 1);
    }
    free(a);
    return ok;
}

static void radiogeddon_compare_patterns(
    Storage* storage,
    const char* path_a,
    const char* path_b,
    FuriString* out) {
    RadioGeddonPattern* pa = malloc(sizeof(RadioGeddonPattern));
    RadioGeddonPattern* pb = malloc(sizeof(RadioGeddonPattern));
    bool ok_a = radiogeddon_dominant_pattern(storage, path_a, pa);
    bool ok_b = ok_a && radiogeddon_dominant_pattern(storage, path_b, pb);
    furi_string_cat_str(out, "[HYPOTHESIS] patterns\n");
    if(!ok_a || !ok_b) {
        furi_string_cat_printf(out, "No decodable frames in %s.\n", !ok_a ? "A" : "B");
    } else {
        furi_string_cat_printf(
            out,
            "A: %s %ub x%u\nB: %s %ub x%u\n",
            rg_analyzer_encoding_name(pa->encoding),
            (unsigned)pa->bits_len,
            (unsigned)pa->frames,
            rg_analyzer_encoding_name(pb->encoding),
            (unsigned)pb->bits_len,
            (unsigned)pb->frames);
        if(pa->encoding != pb->encoding) {
            furi_string_cat_str(out, "Different encodings.\n");
        } else if(pa->bits_len == pb->bits_len && strcmp(pa->bits, pb->bits) == 0) {
            furi_string_cat_str(out, "Same frame pattern.\n");
        } else {
            int shift = 0;
            size_t ov = 0;
            size_t m = rg_analyzer_align(
                pa->bits, pa->bits_len, pb->bits, pb->bits_len, RG_ANALYZER_MAX_SHIFT, &shift, &ov);
            furi_string_cat_printf(out, "%u of %u bits differ", (unsigned)(ov - m), (unsigned)ov);
            if(shift) furi_string_cat_printf(out, " (shift %+d)", shift);
            furi_string_cat_str(out, "\n");
            if(shift == 0 && pa->bits_len == pb->bits_len) {
                for(size_t k = 0; k < pa->bits_len; k++)
                    furi_string_push_back(out, pa->bits[k] == pb->bits[k] ? '.' : 'X');
                furi_string_cat_str(out, "\n");
            }
        }
    }
    free(pa);
    free(pb);
}

int radiogeddon_analysis_raw_similarity(
    Storage* storage,
    const char* path_a,
    const char* path_b,
    FuriString* out) {
    RadioGeddonRawFile* fa = radiogeddon_storage_raw_open(storage, path_a);
    RadioGeddonRawFile* fb = fa ? radiogeddon_storage_raw_open(storage, path_b) : NULL;
    int score = -1;
    if(fa && fb) {
        int32_t ca[RG_ANALYSIS_CHUNK], cb[RG_ANALYSIS_CHUNK];
        RgSimilarity sim;
        rg_similarity_init(&sim);
        while(true) {
            size_t na = rg_raw_reader_read(&fa->reader, ca, RG_ANALYSIS_CHUNK);
            size_t nb = rg_raw_reader_read(&fb->reader, cb, RG_ANALYSIS_CHUNK);
            size_t common = na < nb ? na : nb;
            rg_similarity_feed(&sim, ca, cb, common);
            rg_similarity_tail(&sim, na - common, nb - common);
            if(na == 0 && nb == 0) break;
        }
        if(sim.na > 0 && sim.nb > 0) {
            score = rg_similarity_score(&sim);
            furi_string_cat_printf(out, "RAW timing match: %d%%\n", score);
            if(score >= 90) {
                furi_string_cat_str(out, "-> near-identical capture.\n");
            } else if(score >= 60) {
                furi_string_cat_str(out, "-> similar structure.\n");
            } else {
                furi_string_cat_str(out, "-> clearly different.\n");
            }
            furi_string_cat_str(out, "(sample by sample; a\ndifferent start lowers it)\n");
        }
    }
    radiogeddon_storage_raw_close(fa);
    radiogeddon_storage_raw_close(fb);
    if(score >= 0) radiogeddon_compare_patterns(storage, path_a, path_b, out);
    return score;
}
