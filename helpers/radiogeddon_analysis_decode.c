#include "radiogeddon_analysis.h"
#include "../radiogeddon_edition.h" /* RG_CORE_TEXT */
#include "rg_decode.h"
#include "rg_div.h"

/*
 * Decode with Firmware's report (radiogeddon_analysis_decode), apart from the
 * other reports so it stays in the app: it lends the radio layer's protocol
 * environment to the decoders, and the Full edition's reports module calls
 * it through the host table.
 */

/* ---- Firmware decoders over a RAW capture ------------------------------ */

/* Free heap kept back beyond the decode log, for the GUI and storage. The
 * decoders themselves are checked against the measured radio session cost
 * before this runs (radiogeddon_scene_radio_memory_ok). */
#define RG_DECODE_HEAP_SPARE (4u * 1024u)

static RG_CORE_TEXT void radiogeddon_analysis_decode_progress(void* context, uint32_t offset) {
    RadioGeddonRawFile* file = context;
    uint32_t size = (uint32_t)stream_size(file->stream);
    radiogeddon_analysis_report_progress(offset / 1024u, size / 1024u);
}

static RG_CORE_TEXT void radiogeddon_analysis_cat_seconds(FuriString* out, uint64_t us) {
    uint32_t ms = rg_div_u64_u32(us, 1000u); // saturates past 49 days
    furi_string_cat_printf(
        out, "%lu.%03lu s", (unsigned long)(ms / 1000u), (unsigned long)(ms % 1000u));
}

RG_CORE_TEXT void radiogeddon_analysis_decode(
    Storage* storage,
    RadioGeddonSubGhz* subghz,
    const char* path,
    FuriString* out) {
    if(memmgr_heap_get_max_free_block() < sizeof(RgDecodeLog) + RG_DECODE_HEAP_SPARE) {
        radiogeddon_analysis_cat_status(out, RadioGeddonAnalysisNoMemory);
        return;
    }
    RadioGeddonRawFile* file = radiogeddon_storage_raw_open(storage, path);
    if(!file) {
        radiogeddon_analysis_cat_status(out, RadioGeddonAnalysisOpenFailed);
        return;
    }
    RgDecodeLog* log = malloc(sizeof(RgDecodeLog));
    rg_decode_log_init(log);
    bool ran = radiogeddon_subghz_decode_raw(
        subghz, &file->reader, log, radiogeddon_analysis_decode_progress, file);
    bool any_data = file->reader.any_data;
    bool corrupt = file->reader.corrupt;
    uint32_t lost = file->reader.lost;
    radiogeddon_storage_raw_close(file);

    if(!ran) {
        furi_string_cat_str(out, "The radio is busy.\nStop receiving or\ntransmitting first.\n");
    } else if(!any_data || log->samples == 0) {
        radiogeddon_analysis_cat_status(
            out, corrupt ? RadioGeddonAnalysisCorrupt : RadioGeddonAnalysisNoRaw);
    } else {
        furi_string_cat_str(
            out,
            "CONFIRMED = a firmware\ndecoder decoded it, as\nin Receive. Nothing is\ntransmitted.\n");
        if(corrupt) radiogeddon_analysis_cat_status(out, RadioGeddonAnalysisCorrupt);
        if(lost) {
            furi_string_cat_printf(
                out,
                "Recording lost %lu\nsamples; frames there\nmay not decode.\n",
                (unsigned long)lost);
        }
        furi_string_cat_str(out, "----------------\n");
        if(log->hit_count == 0) {
            furi_string_cat_str(
                out,
                "No protocol decoded.\nNo firmware decoder\nfound a complete frame.\n"
                "Unknown Protocol\nAnalysis can still show\nthe structure.\n");
        } else {
            furi_string_cat_printf(
                out,
                "[CONFIRMED] %lu decodes,\n%lu different%s\n",
                (unsigned long)log->decodes,
                (unsigned long)log->hit_count,
                log->dropped ? " or more" : "");
        }
        for(size_t i = 0; i < log->hit_count; i++) {
            const RgDecodeHit* h = &log->hit[i];
            furi_string_cat_printf(
                out,
                "----------------\n%u. %s x%lu\nat ",
                (unsigned)(i + 1),
                h->protocol,
                (unsigned long)h->count);
            radiogeddon_analysis_cat_seconds(out, h->first_us);
            if(h->count > 1) {
                furi_string_cat_str(out, " to ");
                radiogeddon_analysis_cat_seconds(out, h->last_us);
            }
            furi_string_cat_printf(out, "\n%s", h->text);
            if(h->text[0] && h->text[strlen(h->text) - 1] != '\n')
                furi_string_push_back(out, '\n');
            if(h->truncated) furi_string_cat_str(out, "(text cut short)\n");
        }
        if(log->dropped) {
            furi_string_cat_printf(
                out,
                "----------------\n%lu decodes not listed\n(list full).\n",
                (unsigned long)log->dropped);
        }
        furi_string_cat_str(out, "----------------\nRead ");
        furi_string_cat_printf(out, "%lu samples,\n", (unsigned long)log->samples);
        radiogeddon_analysis_cat_seconds(out, log->time_us);
        furi_string_cat_str(out, " of signal.\n");
    }
    free(log);
}
