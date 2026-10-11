#include "radiogeddon_scene.h"

#if RG_FEATURE_MULTI_COMPARE

// Full edition: run Multi-Capture Compare over the list. One file is open
// at a time; each leaves only its summary (RgMultiCapture). The summaries
// are freed once the report is written; the report text is kept while shown.

#define MULTI_REPORT_SIZE (6u * 1024u)
/* Free heap kept beyond the summaries, the report and one analysis. */
#define MULTI_HEAP_SPARE  (6u * 1024u)

void radiogeddon_scene_multi_result_on_enter(void* context) {
    RadioGeddonApp* app = context;
    size_t n = app->multi_count;
    size_t caps_bytes = n * sizeof(RgMultiCapture);
    size_t need = caps_bytes + sizeof(RgMultiResult) + MULTI_REPORT_SIZE +
                  radiogeddon_multi_analysis_memory() + MULTI_HEAP_SPARE;
    if(memmgr_heap_get_max_free_block() < need) {
        furi_string_set_str(
            app->temp_str,
            "Not enough free memory\nfor this comparison.\nUse fewer recordings or\nrestart the Flipper.\n");
        text_box_reset(app->text_box);
        text_box_set_text(app->text_box, furi_string_get_cstr(app->temp_str));
        view_dispatcher_switch_to_view(app->view_dispatcher, RadioGeddonViewTextBox);
        return;
    }
    RgMultiCapture* caps = malloc(caps_bytes);
    for(size_t i = 0; i < n; i++) {
        snprintf(
            app->multi_header,
            sizeof(app->multi_header),
            "Capture %u of %u",
            (unsigned)(i + 1),
            (unsigned)n);
        radiogeddon_scene_show_progress(app, app->multi_header);
        RadioGeddonAnalysisStatus status;
        radiogeddon_multi_capture(
            app->storage, furi_string_get_cstr(app->multi_paths[i]), &caps[i], &status);
        radiogeddon_scene_progress_end(app);
    }
    radiogeddon_memdiag_sample("Multi-compare");
    RgMultiResult* res = malloc(sizeof(RgMultiResult));
    rg_multi_compare(caps, n, res);
    app->multi_report = malloc(MULTI_REPORT_SIZE);
    RgText text;
    rg_text_init(&text, app->multi_report, MULTI_REPORT_SIZE);
    rg_multi_report(caps, n, res, &text);
    free(res);
    free(caps);

    text_box_reset(app->text_box);
    text_box_set_font(app->text_box, TextBoxFontText);
    text_box_set_text(app->text_box, app->multi_report);
    view_dispatcher_switch_to_view(app->view_dispatcher, RadioGeddonViewTextBox);
}

bool radiogeddon_scene_multi_result_on_event(void* context, SceneManagerEvent event) {
    UNUSED(context);
    UNUSED(event);
    return false;
}

void radiogeddon_scene_multi_result_on_exit(void* context) {
    RadioGeddonApp* app = context;
    text_box_reset(app->text_box);
    free(app->multi_report);
    app->multi_report = NULL;
}

#endif
