#include "radiogeddon_multi.h"

#if RG_FEATURE_MULTI_COMPARE

#include "radiogeddon_storage.h"

size_t radiogeddon_multi_memory(size_t count) {
    if(count > RG_MULTI_MAX_CAPTURES) count = RG_MULTI_MAX_CAPTURES;
    return count * sizeof(RgMultiCapture) + sizeof(RgMultiResult) + sizeof(RgAnalyzer);
}

bool radiogeddon_multi_capture(
    Storage* storage,
    const char* path,
    RgMultiCapture* cap,
    RadioGeddonAnalysisStatus* status) {
    const char* slash = strrchr(path, '/');
    char name[RG_MULTI_NAME_LEN];
    strlcpy(name, slash ? slash + 1 : path, sizeof(name));
    char* dot = strrchr(name, '.');
    if(dot) *dot = '\0';
    rg_multi_capture_init(cap, name);
    *status = RadioGeddonAnalysisOk;

    RadioGeddonLoadedSignal sig;
    radiogeddon_loaded_signal_init(&sig);
    bool loaded = radiogeddon_storage_load(storage, path, &sig);
    if(loaded) {
        cap->frequency = sig.frequency;
        strlcpy(cap->preset, furi_string_get_cstr(sig.preset), sizeof(cap->preset));
    }
    RadioGeddonSignalKind kind = sig.kind;
    uint32_t bits = sig.bit_count;
    uint64_t key = sig.key;
    if(loaded && kind == RadioGeddonSignalKindProtocol) {
        strlcpy(cap->protocol, furi_string_get_cstr(sig.protocol), sizeof(cap->protocol));
    }
    radiogeddon_loaded_signal_reset(&sig);
    if(!loaded) {
        *status = RadioGeddonAnalysisOpenFailed;
        return false;
    }

    if(kind == RadioGeddonSignalKindProtocol) {
        if(bits == 0 || bits > 64) {
            // A key the summary cannot hold bit for bit (or no key at all).
            *status = RadioGeddonAnalysisNoRaw;
            return false;
        }
        char protocol[RG_MULTI_PRESET_LEN];
        strlcpy(protocol, cap->protocol, sizeof(protocol));
        rg_multi_capture_from_key(cap, protocol, bits, key);
        return true;
    }

    RgAnalyzer* a = radiogeddon_analysis_run_file(storage, path, status);
    if(!a) return false;
    rg_multi_capture_from_analysis(cap, &a->result);
    free(a);
    return true;
}

size_t radiogeddon_multi_compare_files(
    Storage* storage,
    const char* const* paths,
    size_t count,
    const char* heading,
    char* report,
    size_t size,
    RadioGeddonMultiFileCallback on_file,
    RadioGeddonProgressCallback progress,
    void* context) {
    if(count > RG_MULTI_MAX_CAPTURES) count = RG_MULTI_MAX_CAPTURES;
    RgText text;
    rg_text_init(&text, report, size);
    if(heading) rg_text_printf(&text, "%s", heading);
    RgMultiCapture* caps = malloc((count ? count : 1) * sizeof(RgMultiCapture));
    radiogeddon_analysis_set_progress(progress, context);
    for(size_t i = 0; i < count; i++) {
        if(on_file) on_file(context, i, count);
        RadioGeddonAnalysisStatus status;
        radiogeddon_multi_capture(storage, paths[i], &caps[i], &status);
    }
    radiogeddon_analysis_set_progress(NULL, NULL);
    RgMultiResult* res = malloc(sizeof(RgMultiResult));
    rg_multi_compare(caps, count, res);
    rg_multi_report(caps, count, res, &text);
    free(res);
    free(caps);
    return text.len;
}

#endif
