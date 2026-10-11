/**
 * @file radiogeddon_multi.h
 * @brief Full edition: Multi-Capture Compare on files (built into its module).
 *
 * Linked into the Multi-Capture Compare module (radiogeddon_modules.h), not
 * the app: the app reaches it through RadioGeddonMultiModule.
 */
#pragma once

#include "../radiogeddon_edition.h"

#if RG_FEATURE_MULTI_COMPARE

#include <furi.h>
#include <storage/storage.h>
#include "rg_multi.h"
#include "radiogeddon_analysis.h"
#include "radiogeddon_modules.h"

/** Heap a comparison of @p count files takes (summaries, result, one analysis). */
size_t radiogeddon_multi_memory(size_t count);

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

/** RadioGeddonMultiModule.compare. */
size_t radiogeddon_multi_compare_files(
    Storage* storage,
    const char* const* paths,
    size_t count,
    const char* heading,
    char* report,
    size_t size,
    RadioGeddonMultiFileCallback on_file,
    RadioGeddonProgressCallback progress,
    void* context);

#endif
