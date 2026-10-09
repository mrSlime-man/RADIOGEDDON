#include "radiogeddon_analysis.h"
#include "radiogeddon_dsp.h"

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
        if(sig->bit_count) furi_string_cat_printf(out, "Bits: %lu\n", (unsigned long)sig->bit_count);
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

static void radiogeddon_analysis_analyze_raw(
    Storage* storage,
    const char* path,
    FuriString* out) {
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
        furi_string_cat_printf(
            out, "Est. base Te: ~%lu us\n", (unsigned long)clusters[0].center);
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
                out, "Bytes: %lu (+%lu bits)\n",
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
            furi_string_cat_str(
                out, "[HEURISTIC] low bits often\nencode button/command.\n");
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
            out,
            "Fixed payload: same code\nevery press. No rolling\ncounter, no encryption.\n");
        break;
    case SubGhzProtocolTypeDynamic:
        furi_string_cat_printf(out, "[CONFIRMED] Dynamic code\n");
        furi_string_cat_str(
            out,
            "Rolling counter present.\nPayload changes each\npress (e.g. KeeLoq-style).\n"
            "Likely encrypted hop\ncode. Key recovery is\nNOT performed.\n");
        break;
    case SubGhzProtocolWeatherStation:
        furi_string_cat_str(out, "[CONFIRMED] Telemetry\n(weather/sensor). Usually\nunencrypted data frame.\n");
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

        if(a->key != b->key && a->key && b->key &&
           furi_string_equal(a->protocol, b->protocol)) {
            uint64_t diff = (a->key > b->key) ? (a->key - b->key) : (b->key - a->key);
            furi_string_cat_printf(
                out, "[HEURISTIC] key delta:\n %lu\n", (unsigned long)(diff & 0xFFFFFFFF));
            furi_string_cat_str(
                out, "Small delta across presses\nsuggests rolling counter.\n");
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
