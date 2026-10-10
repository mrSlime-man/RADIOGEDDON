/**
 * @file radiogeddon_report.h
 * @brief Save a recording's analysis reports as a text file on the SD card.
 *
 * Writes the same reports the screens show (Signal Info & Analysis, then
 * Decode with Firmware and Unknown Protocol Analysis for a RAW capture, or
 * Crypto Analysis for a decoded signal), with their [CONFIRMED]/[OBSERVED]/[HEURISTIC]/[HYPOTHESIS]
 * labels, to apps_data/radiogeddon/reports/<name>.txt. An existing report is
 * never overwritten: the name gets _2, _3 and so on.
 */
#pragma once

#include <furi.h>
#include <storage/storage.h>
#include "radiogeddon_storage.h"
#include "radiogeddon_subghz.h"

typedef enum {
    RadioGeddonReportOk,
    RadioGeddonReportNoName, /* <name>.txt to <name>_99.txt all exist */
    RadioGeddonReportOpenFailed,
    RadioGeddonReportWriteFailed,
} RadioGeddonReportResult;

/**
 * Analyse the file at @p sub_path (already loaded into @p sig) and write the
 * report. A RAW capture is also run through the firmware's decoders with
 * @p subghz; pass NULL when they do not fit in memory, and the report says
 * they were not run. @p scratch is reused for each section; @p out_path
 * receives the report's path. A partly written report is removed.
 */
RadioGeddonReportResult radiogeddon_report_save(
    Storage* storage,
    RadioGeddonSubGhz* subghz,
    const char* sub_path,
    const RadioGeddonLoadedSignal* sig,
    FuriString* scratch,
    FuriString* out_path);

/** Short message for a result. */
const char* radiogeddon_report_result_text(RadioGeddonReportResult result);
