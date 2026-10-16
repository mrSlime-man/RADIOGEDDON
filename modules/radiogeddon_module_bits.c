/* Full edition module: the Bitstream Explorer's screen (see helpers/radiogeddon_modules.h). */
#include "../helpers/radiogeddon_modules.h"

#include <flipper_application/flipper_application.h>

static RgAnalysis* radiogeddon_bits_analyze(
    Storage* storage,
    const char* path,
    RadioGeddonAnalysisStatus* status,
    RadioGeddonProgressCallback progress,
    void* context) {
    radiogeddon_analysis_set_progress(progress, context);
    RgAnalyzer* a = radiogeddon_analysis_run_file(storage, path, status);
    radiogeddon_analysis_set_progress(NULL, NULL);
    if(!a) return NULL;
    // Only the result is kept: copy it out, then free the analyzer's state.
    RgAnalysis* doc = malloc(sizeof(RgAnalysis));
    memcpy(doc, &a->result, sizeof(RgAnalysis));
    free(a);
    return doc;
}

static const RadioGeddonBitsModule radiogeddon_bits_module = {
    .alloc = radiogeddon_bits_view_alloc,
    .free = radiogeddon_bits_view_free,
    .get_view = radiogeddon_bits_view_get_view,
    .set_analysis = radiogeddon_bits_view_set_analysis,
    .analyze = radiogeddon_bits_analyze,
};

static const FlipperAppPluginDescriptor radiogeddon_bits_descriptor = {
    .appid = RADIOGEDDON_MODULE_APPID,
    .ep_api_version = RADIOGEDDON_MODULE_API,
    .entry_point = &radiogeddon_bits_module,
};

const FlipperAppPluginDescriptor* radiogeddon_bits_module_ep(void) {
    return &radiogeddon_bits_descriptor;
}
