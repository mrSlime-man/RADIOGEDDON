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

#define RG_ANALYSIS_LOAD_CAP 4096u

/* Pack the constant-field bits (field_map=='.') into a hex device-ID candidate. */
static void radiogeddon_pack_device_id(const RgAnalysis* a, FuriString* out) {
    uint64_t id = 0;
    size_t n = 0;
    for(size_t k = 0; k < a->bit_count && n < 64; k++) {
        if(a->field_map[k] == '.') {
            id = (id << 1) | (uint64_t)(a->bits[k] == '1' ? 1 : 0);
            n++;
        }
    }
    if(n == 0) {
        furi_string_cat_str(out, "Device ID: (none constant)\n");
        return;
    }
    furi_string_cat_printf(
        out,
        "Device ID cand: 0x%08lX%08lX\n (%u constant bits)\n",
        (unsigned long)(id >> 32),
        (unsigned long)(id & 0xFFFFFFFF),
        (unsigned)n);
}

void radiogeddon_analysis_unknown(Storage* storage, const char* path, FuriString* out) {
    int32_t* buf = malloc(sizeof(int32_t) * RG_ANALYSIS_LOAD_CAP);
    size_t n = radiogeddon_storage_load_raw_samples(storage, path, buf, RG_ANALYSIS_LOAD_CAP);
    if(n == 0) {
        furi_string_cat_str(
            out,
            "No RAW timing data.\nUnknown-protocol analysis\nneeds a RAW capture.\nRecord one via Receive &\nRecord (Left button).\n");
        free(buf);
        return;
    }

    RgAnalysis a;
    rg_analyzer_run(buf, n, &a);
    free(buf);

    furi_string_cat_str(out, "[HYPOTHESIS] structural\nanalysis (not a decode)\n");
    furi_string_cat_printf(out, "Samples: %u\n", (unsigned)a.sample_count);
    furi_string_cat_printf(out, "Base Te: ~%lu us\n", (unsigned long)a.te_us);
    furi_string_cat_printf(out, "Timing groups: %u\n", (unsigned)a.cluster_count);
    furi_string_cat_printf(
        out, "Encoding: %s (%d%%)\n", rg_analyzer_encoding_name(a.encoding), a.encoding_confidence);
    furi_string_cat_printf(out, "Frames: %u", (unsigned)a.frame_count);
    if(a.frames_repeat) {
        furi_string_cat_printf(out, " (repeat x%u)", (unsigned)a.repeat_count);
    }
    furi_string_cat_str(out, "\n");

    if(a.bit_count > 0) {
        furi_string_cat_printf(out, "Frame bits: %u\n", (unsigned)a.bit_count);
        furi_string_cat_printf(out, "Bits:\n%s\n", a.bits);
    }

    if(a.have_field_diff) {
        furi_string_cat_str(out, "----------------\n");
        furi_string_cat_printf(
            out,
            "Fields across repeats:\n const %u / changing %u\n",
            (unsigned)a.const_bits,
            (unsigned)a.changing_bits);
        furi_string_cat_printf(out, "Map (.=const X=change):\n%s\n", a.field_map);
        radiogeddon_pack_device_id(&a, out);
        if(a.changing_bits > 0) {
            furi_string_cat_str(
                out,
                "[HYPOTHESIS] changing bits\nresemble a rolling counter;\nconstant bits a fixed ID.\nNot verified; no key is\nrecovered.\n");
        } else {
            furi_string_cat_str(out, "[HYPOTHESIS] fully constant\n-> likely a fixed code.\n");
        }
    } else if(a.frame_count < 2) {
        furi_string_cat_str(
            out,
            "Capture more presses to\ncompare frames and reveal\nconstant vs changing\nfields.\n");
    }
}

int radiogeddon_analysis_raw_similarity(
    Storage* storage,
    const char* path_a,
    const char* path_b,
    FuriString* out) {
    int32_t* a = malloc(sizeof(int32_t) * RG_ANALYSIS_LOAD_CAP);
    int32_t* b = malloc(sizeof(int32_t) * RG_ANALYSIS_LOAD_CAP);
    size_t na = radiogeddon_storage_load_raw_samples(storage, path_a, a, RG_ANALYSIS_LOAD_CAP);
    size_t nb = radiogeddon_storage_load_raw_samples(storage, path_b, b, RG_ANALYSIS_LOAD_CAP);
    int score = -1;
    if(na > 0 && nb > 0) {
        score = rg_analyzer_similarity(a, na, b, nb);
        furi_string_cat_printf(out, "RAW timing match: %d%%\n", score);
        if(score >= 90) {
            furi_string_cat_str(out, "-> near-identical capture.\n");
        } else if(score >= 60) {
            furi_string_cat_str(out, "-> similar structure.\n");
        } else {
            furi_string_cat_str(out, "-> clearly different.\n");
        }
    }
    free(a);
    free(b);
    return score;
}
