#include "radiogeddon_analysis.h"

/*
 * The progress hook and the status texts of the whole-file analyses, apart
 * from the analyzer runner (radiogeddon_analysis_run.c): the resident app
 * needs these for the firmware decode report even in the Full edition, where
 * the runner and the analyzer are linked only into modules. A module that
 * links the runner links its own copy of this file, so its progress hook is
 * its own (set by the module's scenes).
 */

static RadioGeddonProgressCallback radiogeddon_analysis_progress_cb;
static void* radiogeddon_analysis_progress_ctx;

void radiogeddon_analysis_set_progress(RadioGeddonProgressCallback callback, void* context) {
    radiogeddon_analysis_progress_cb = callback;
    radiogeddon_analysis_progress_ctx = context;
}

bool radiogeddon_analysis_progress_wanted(void) {
    return radiogeddon_analysis_progress_cb != NULL;
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
