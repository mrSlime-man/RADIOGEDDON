#include "radiogeddon_analysis.h"
#include "rg_analyzer.h"
#include "rg_checksum.h"
#include "../radiogeddon_edition.h"

/*
 * Unknown Protocol Analysis report (radiogeddon_analysis_unknown), apart from
 * the other reports: the Catalog edition links it into the app, the Full
 * edition builds it as a module (radiogeddon_modules.h) loaded only while
 * the analysis runs, with the checksum tests it alone uses.
 */

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
    if(r->lost_samples) {
        furi_string_cat_printf(
            out,
            "Lost while recording: %lu\nsamples (SD too slow);\ntiming jumps at gaps\n",
            (unsigned long)r->lost_samples);
    }
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
    // Every reading, so an ambiguous capture shows as one.
    furi_string_cat_printf(
        out,
        "Fits: PWM %u%% PPM %u%%\nManchester %u%%\n",
        r->encoding_fit[RgEncodingPWM],
        r->encoding_fit[RgEncodingPPM],
        r->encoding_fit[RgEncodingManchester]);
    if(r->encoding_by_pairing) {
        furi_string_cat_printf(
            out,
            "Chosen by pulse/gap\npairing: %u%% of %lu\npairs %s. Manchester\ndata mixes both.\n",
            r->params.pwm_equal ? r->pair_same_pct : r->pair_opposite_pct,
            (unsigned long)r->pair_count,
            r->params.pwm_equal ? "equal" : "opposite");
    }
    furi_string_cat_printf(out, "Te ~%lu us\n", (unsigned long)r->te_us);
    switch(r->encoding) {
    case RgEncodingPWM:
        furi_string_cat_printf(
            out,
            "Pulses %lu/%lu us,\n1 = long pulse%s\n",
            (unsigned long)r->params.pwm_short_us,
            (unsigned long)r->params.pwm_long_us,
            r->params.pwm_equal ? "\n(gap = pulse)" : "");
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
    // How the main patterns repeat within a press (measured start times).
    for(uint8_t g = 0; g < r->group_count && g < 2; g++) {
        RgRepeatTiming t;
        if(!rg_analyzer_repeat_timing(r, g, &t)) continue;
        furi_string_cat_printf(
            out,
            "[OBSERVED] %c repeats\nevery %lu.%lu ms (%lu\ngaps, %lu-%lu ms)\n",
            radiogeddon_group_letter(g),
            (unsigned long)(t.median_us / 1000u),
            (unsigned long)(t.median_us % 1000u / 100u),
            (unsigned long)t.intervals,
            (unsigned long)(t.min_us / 1000u),
            (unsigned long)((t.max_us + 999u) / 1000u));
    }
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
        if(f->repeat_bits) {
            furi_string_cat_str(out, " rep");
        } else if(f->truncated) {
            furi_string_cat_str(out, " long");
        }
        furi_string_cat_str(out, "\n");
    }
    if(r->frame_count > r->frames_kept)
        furi_string_cat_printf(
            out, "(first %u of %u frames)\n", (unsigned)r->frames_kept, (unsigned)r->frame_count);
    for(size_t i = 0; i < r->frames_kept; i++) {
        if(r->frames[i].repeat_bits) {
            furi_string_cat_str(
                out,
                "rep = repeats sent with\nno gap; cut to the bits'\nown period [HYPOTHESIS]\n");
            break;
        }
    }
}

#if RG_FEATURE_CHECKSUM_HINTS
/* Checksum structure over the distinct frames of the modal length. */
static void radiogeddon_report_checksums(const RgAnalysis* r, FuriString* out) {
    furi_string_cat_str(out, "----------------\nChecksum structure\n[HYPOTHESIS]\n");
    if(r->bit_count < 12) {
        furi_string_cat_str(out, "Frames too short.\n");
        return;
    }
    RgChecksumSet* set = malloc(sizeof(RgChecksumSet));
    rg_checksum_set_init(set);
    char bits[RG_ANALYZER_MAX_BITS + 1];
    // Which frames are the evidence: the distinct ones of the modal length
    // (a frame repeated twice is one frame; repeats prove nothing).
    uint8_t used[RG_CHECKSUM_MAX_FRAMES];
    memset(used, 0, sizeof(used));
    size_t clean = 0;
    for(size_t i = 0; i < r->frames_kept; i++) {
        const RgFrame* f = &r->frames[i];
        if(f->fit < RG_ANALYZER_GOOD_FIT || f->bit_count != r->bit_count) continue;
        clean++;
        rg_analyzer_frame_bits(f, bits);
        size_t before = set->count;
        if(rg_checksum_set_add(set, bits, f->bit_count) && set->count > before &&
           before < RG_CHECKSUM_MAX_FRAMES)
            used[before] = (uint8_t)(i + 1);
    }
    RgChecksumResult* res = malloc(sizeof(RgChecksumResult));
    rg_checksum_analyze(set, res);
    furi_string_cat_printf(
        out,
        "[OBSERVED] %u clean %u-bit\nframes, %u distinct:\n",
        (unsigned)clean,
        (unsigned)res->bit_count,
        (unsigned)res->frames);
    for(size_t k = 0; k < set->count; k++)
        furi_string_cat_printf(out, "%s%u", k ? "," : " #", (unsigned)used[k]);
    if(set->count) furi_string_cat_str(out, "\n");
    if(!res->enough_for_byte) {
        furi_string_cat_printf(
            out,
            "Not enough evidence:\nbyte checks need %u,\nnibble %u, parity %u\ndistinct frames (e.g.\nseveral presses or\nbuttons in one capture).\n",
            (unsigned)RG_CHECKSUM_MIN_BYTE,
            (unsigned)RG_CHECKSUM_MIN_NIBBLE,
            (unsigned)RG_CHECKSUM_MIN_PARITY);
    } else if(res->hit_count == 0) {
        furi_string_cat_str(out, "No common checksum\n(XOR, sum, CRC-8,\nparity) fits all.\n");
        if(res->frames < RG_CHECKSUM_MIN_PARITY)
            furi_string_cat_printf(
                out,
                "(Nibble needs %u, parity\n%u distinct frames.)\n",
                (unsigned)RG_CHECKSUM_MIN_NIBBLE,
                (unsigned)RG_CHECKSUM_MIN_PARITY);
    }
    char line[96];
    for(size_t i = 0; i < res->hit_count; i++) {
        rg_checksum_describe(&res->hit[i], line, sizeof(line));
        furi_string_cat_printf(
            out,
            "- %s; holds on all %u\ndistinct frames above\n",
            line,
            (unsigned)res->hit[i].frames);
    }
    if(res->hit_count) {
        furi_string_cat_str(
            out, "An observed match, not\nan identified field: the\nprotocol is not known.\n");
    }
    free(res);
    free(set);
}
#endif

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
        "OBSERVED = measured in\nthe file. HYPOTHESIS =\ninferred, may be wrong.\nOnly firmware decoders\n(Receive, Decode with\nFirmware) confirm a\nprotocol.\n");
    if(status == RadioGeddonAnalysisCorrupt) radiogeddon_analysis_cat_status(out, status);
    furi_string_cat_str(out, "----------------\n");
    radiogeddon_report_observed(r, out);
    furi_string_cat_str(out, "----------------\n");
    radiogeddon_report_hypothesis(r, out);
    furi_string_cat_str(out, "----------------\n");
    radiogeddon_report_frames(r, out);
#if RG_FEATURE_CHECKSUM_HINTS
    radiogeddon_report_checksums(r, out);
#endif
    free(a);
}
