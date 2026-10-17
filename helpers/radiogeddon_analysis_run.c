#include "radiogeddon_analysis.h"
#include "../radiogeddon_edition.h" /* RG_TOOL_TEXT */

/*
 * The analyzer over a RAW file (radiogeddon_analysis_run_*), apart from the
 * reports in radiogeddon_analysis.c so the Full edition's Multi-Capture
 * Compare module (radiogeddon_modules.h) can link it on its own: it needs
 * nothing but the analyzer, the RAW reader and the firmware's storage. In
 * the Full edition it is linked only into modules (the resident app keeps
 * radiogeddon_analysis_status.c), so the analyzer's ~9 KB of code takes RAM
 * only while a tool that analyses is open.
 */

#define RG_ANALYSIS_CHUNK           64u
/* Free heap kept back beyond the analysis itself, for the GUI and storage. */
#define RG_ANALYSIS_HEAP_SPARE      (6u * 1024u)
/* Report progress every this many chunks (about 1,000 samples). */
#define RG_ANALYSIS_PROGRESS_CHUNKS 16u
#define RG_ANALYSIS_PASSES          3u

/* Progress in kilobytes over all passes; passes the engine skips just jump. */
static RG_TOOL_TEXT void radiogeddon_analysis_progress(RadioGeddonRawFile* file, int pass) {
    if(!radiogeddon_analysis_progress_wanted()) return;
    uint64_t size = stream_size(file->stream);
    uint64_t pos = stream_tell(file->stream);
    if(pos > size) pos = size;
    uint32_t done = (uint32_t)(((uint64_t)(pass - 1) * size + pos) / 1024u);
    uint32_t total = (uint32_t)(RG_ANALYSIS_PASSES * size / 1024u);
    radiogeddon_analysis_report_progress(done, total);
}

/* The analysis of @p file, or of the file at @p path when @p file is NULL.
 * The result is allocated first, then the file (when opened here) and the
 * working state above it: when those are freed their blocks join the free
 * space after the result instead of leaving a hole under it. The working
 * state lives only for the passes. */
static RG_TOOL_TEXT RgAnalysis* radiogeddon_analysis_run(
    RadioGeddonRawFile* file,
    Storage* storage,
    const char* path,
    RadioGeddonAnalysisFinish finish,
    const RadioGeddonAnalysisTap* tap,
    RadioGeddonAnalysisStatus* status) {
    if(memmgr_heap_get_max_free_block() < RADIOGEDDON_ANALYSIS_MEMORY + RG_ANALYSIS_HEAP_SPARE) {
        *status = RadioGeddonAnalysisNoMemory;
        return NULL;
    }
    RgAnalysis* out = malloc(sizeof(RgAnalysis));
    RadioGeddonRawFile* f = file ? file : radiogeddon_storage_raw_open(storage, path);
    if(!f) {
        *status = RadioGeddonAnalysisOpenFailed;
        free(out);
        return NULL;
    }
    RgAnalyzer* a = malloc(sizeof(RgAnalyzer));
    int32_t chunk[RG_ANALYSIS_CHUNK];
    rg_analyzer_begin(a, out);
    do {
        rg_raw_reader_rewind(&f->reader);
        // The tap reads along with the second pass: the timing is known.
        bool tapped = tap && a->pass == 2;
        if(tapped) tap->start(tap->context, out);
        size_t n;
        uint32_t chunks = 0;
        while((n = rg_raw_reader_read(&f->reader, chunk, RG_ANALYSIS_CHUNK)) > 0) {
            rg_analyzer_feed(a, chunk, n);
            if(tapped) tap->feed(tap->context, chunk, n);
            if(++chunks % RG_ANALYSIS_PROGRESS_CHUNKS == 0)
                radiogeddon_analysis_progress(f, a->pass);
        }
    } while(rg_analyzer_next_pass(a));
    if(finish) finish(a);
    free(a);

    *status = f->reader.corrupt ? RadioGeddonAnalysisCorrupt : RadioGeddonAnalysisOk;
    out->lost_samples = f->reader.lost;
    if(out->sample_count == 0) {
        *status = f->reader.corrupt ? RadioGeddonAnalysisCorrupt : RadioGeddonAnalysisNoRaw;
        free(out);
        out = NULL;
    }
    if(!file) radiogeddon_storage_raw_close(f);
    return out;
}

RG_TOOL_TEXT RgAnalysis* radiogeddon_analysis_run_raw(
    RadioGeddonRawFile* file,
    RadioGeddonAnalysisFinish finish,
    const RadioGeddonAnalysisTap* tap,
    RadioGeddonAnalysisStatus* status) {
    return radiogeddon_analysis_run(file, NULL, NULL, finish, tap, status);
}

RG_TOOL_TEXT RgAnalysis* radiogeddon_analysis_run_file(
    Storage* storage,
    const char* path,
    RadioGeddonAnalysisFinish finish,
    RadioGeddonAnalysisStatus* status) {
    return radiogeddon_analysis_run(NULL, storage, path, finish, NULL, status);
}
