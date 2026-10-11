/**
 * @file radiogeddon_multi.h
 * @brief Full edition: Multi-Capture Compare on the device (files -> summaries).
 */
#pragma once

#include "../radiogeddon_edition.h"

#if RG_FEATURE_MULTI_COMPARE

#include <furi.h>
#include <storage/storage.h>
#include "rg_multi.h"
#include "radiogeddon_analysis.h"

/** Free heap one capture's analysis needs on top of the summaries. */
size_t radiogeddon_multi_analysis_memory(void);

/**
 * Summarise one .sub file into @p cap: its metadata, then the analyzer over
 * its RAW data (streamed; nothing kept but the summary) or, for a decoded
 * key file, its key bits. False (and @p cap marked unreadable) when the file
 * cannot be read or analysed; @p status says why.
 */
bool radiogeddon_multi_capture(
    Storage* storage,
    const char* path,
    RgMultiCapture* cap,
    RadioGeddonAnalysisStatus* status);

#endif
