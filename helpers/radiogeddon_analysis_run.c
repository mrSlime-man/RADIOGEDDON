#include "radiogeddon_analysis.h"

/*
 * The analyzer over a RAW file (radiogeddon_analysis_run_*), apart from the
 * reports in radiogeddon_analysis.c so the Full edition's Multi-Capture
 * Compare module (radiogeddon_modules.h) can link it on its own: it needs
 * nothing but the analyzer, the RAW reader and the firmware's storage.
 */

#define RG_ANALYSIS_CHUNK           64u
/* Free heap kept back beyond the analyzer itself, for the GUI and storage. */
#define RG_ANALYSIS_HEAP_SPARE      (6u * 1024u)
/* Report progress every this many chunks (about 1,000 samples). */
#define RG_ANALYSIS_PROGRESS_CHUNKS 16u
#define RG_ANALYSIS_PASSES          3u

static RadioGeddonProgressCallback radiogeddon_analysis_progress_cb;
static void* radiogeddon_analysis_progress_ctx;

void radiogeddon_analysis_set_progress(RadioGeddonProgressCallback callback, void* context) {
    radiogeddon_analysis_progress_cb = callback;
    radiogeddon_analysis_progress_ctx = context;
}

/* Progress in kilobytes over all passes; passes the engine skips just jump. */
static void radiogeddon_analysis_progress(RadioGeddonRawFile* file, int pass) {
    if(!radiogeddon_analysis_progress_cb) return;
    uint64_t size = stream_size(file->stream);
    uint64_t pos = stream_tell(file->stream);
    if(pos > size) pos = size;
    uint32_t done = (uint32_t)(((uint64_t)(pass - 1) * size + pos) / 1024u);
    uint32_t total = (uint32_t)(RG_ANALYSIS_PASSES * size / 1024u);
    radiogeddon_analysis_progress_cb(radiogeddon_analysis_progress_ctx, done, total);
}

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
        uint32_t chunks = 0;
        while((n = rg_raw_reader_read(&file->reader, chunk, RG_ANALYSIS_CHUNK)) > 0) {
            rg_analyzer_feed(a, chunk, n);
            if(++chunks % RG_ANALYSIS_PROGRESS_CHUNKS == 0)
                radiogeddon_analysis_progress(file, a->pass);
        }
    } while(rg_analyzer_next_pass(a));

    if(file->reader.corrupt) *status = RadioGeddonAnalysisCorrupt;
    a->result.lost_samples = file->reader.lost;
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

void radiogeddon_analysis_report_progress(uint32_t done, uint32_t total) {
    if(radiogeddon_analysis_progress_cb)
        radiogeddon_analysis_progress_cb(radiogeddon_analysis_progress_ctx, done, total);
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
