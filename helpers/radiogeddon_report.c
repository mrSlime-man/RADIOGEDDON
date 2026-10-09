#include "radiogeddon_report.h"
#include "radiogeddon_analysis.h"
#include "../radiogeddon_version.h"

#include <datetime/datetime.h>
#include <furi_hal_rtc.h>

static bool radiogeddon_report_write(File* file, FuriString* text) {
    size_t len = furi_string_size(text);
    return storage_file_write(file, furi_string_get_cstr(text), len) == len;
}

static void radiogeddon_report_header(FuriString* out, const RadioGeddonLoadedSignal* sig) {
    DateTime dt;
    furi_hal_rtc_get_datetime(&dt);
    furi_string_printf(
        out,
        "RadioGeddon %s analysis report\n"
        "File: %s%s\n"
        "Written: %04u-%02u-%02u %02u:%02u (Flipper clock)\n"
        "\n"
        "Labels: [CONFIRMED] a firmware decoder matched;\n"
        "[OBSERVED] measured from the recording;\n"
        "[HEURISTIC] a guess from statistics;\n"
        "[HYPOTHESIS] an inference, not verified.\n",
        RADIOGEDDON_VERSION,
        furi_string_get_cstr(sig->name),
        RADIOGEDDON_SUB_EXTENSION,
        dt.year,
        dt.month,
        dt.day,
        dt.hour,
        dt.minute);
}

RadioGeddonReportResult radiogeddon_report_save(
    Storage* storage,
    const char* sub_path,
    const RadioGeddonLoadedSignal* sig,
    FuriString* scratch,
    FuriString* out_path) {
    storage_common_mkdir(storage, RADIOGEDDON_REPORTS_FOLDER);
    if(!radiogeddon_storage_make_unique_path_in(
           storage, out_path, RADIOGEDDON_REPORTS_FOLDER, furi_string_get_cstr(sig->name), ".txt"))
        return RadioGeddonReportNoName;

    File* file = storage_file_alloc(storage);
    // CREATE_NEW: never replace a file, even one that appeared just now.
    if(!storage_file_open(file, furi_string_get_cstr(out_path), FSAM_WRITE, FSOM_CREATE_NEW)) {
        storage_file_free(file);
        return RadioGeddonReportOpenFailed;
    }

    bool ok = true;
    radiogeddon_report_header(scratch, sig);
    ok = ok && radiogeddon_report_write(file, scratch);

    furi_string_set(scratch, "\n== Signal Info & Analysis ==\n");
    radiogeddon_analysis_describe(sig, scratch);
    furi_string_cat_str(scratch, "----------------\n");
    radiogeddon_analysis_analyze(storage, sub_path, sig, scratch);
    ok = ok && radiogeddon_report_write(file, scratch);

    if(ok && sig->kind == RadioGeddonSignalKindRaw) {
        furi_string_set(scratch, "\n== Unknown Protocol Analysis ==\n");
        radiogeddon_analysis_unknown(storage, sub_path, scratch);
        ok = radiogeddon_report_write(file, scratch);
    } else if(ok && sig->kind == RadioGeddonSignalKindProtocol) {
        furi_string_set(scratch, "\n== Crypto Analysis ==\n");
        radiogeddon_analysis_crypto(sig, scratch);
        furi_string_cat_str(
            scratch,
            "This identifies crypto characteristics only.\n"
            "It does NOT recover keys or defeat rolling codes.\n");
        ok = radiogeddon_report_write(file, scratch);
    }
    furi_string_reset(scratch);

    ok = storage_file_close(file) && ok;
    storage_file_free(file);
    if(!ok) {
        storage_common_remove(storage, furi_string_get_cstr(out_path));
        return RadioGeddonReportWriteFailed;
    }
    return RadioGeddonReportOk;
}

const char* radiogeddon_report_result_text(RadioGeddonReportResult result) {
    switch(result) {
    case RadioGeddonReportOk:
        return "Saved";
    case RadioGeddonReportNoName:
        return "Too many reports\nwith this name";
    case RadioGeddonReportOpenFailed:
        return "Cannot create file";
    case RadioGeddonReportWriteFailed:
        return "SD card write failed";
    default:
        return "Failed";
    }
}
