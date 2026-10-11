#include "radiogeddon_multi.h"

#if RG_FEATURE_MULTI_COMPARE

#include "radiogeddon_storage.h"

size_t radiogeddon_multi_analysis_memory(void) {
    return sizeof(RgAnalyzer);
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

#endif
