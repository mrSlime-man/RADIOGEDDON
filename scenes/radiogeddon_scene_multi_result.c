#include "radiogeddon_scene.h"
#include "../helpers/rg_multi.h"

#if RG_FEATURE_MULTI_COMPARE

// Full edition: run Multi-Capture Compare over the list. The engine is a
// module (radiogeddon_modules.h) loaded for the comparison only; it reads
// one file at a time and keeps only a summary of each. The report text is
// kept while shown.

#define MULTI_REPORT_SIZE (6u * 1024u)
/* Free heap kept beyond the comparison and its report. */
#define MULTI_HEAP_SPARE  (6u * 1024u)

static void radiogeddon_scene_multi_result_file(void* context, size_t index, size_t count) {
    RadioGeddonApp* app = context;
    snprintf(
        app->multi_header,
        sizeof(app->multi_header),
        "Capture %u of %u",
        (unsigned)(index + 1),
        (unsigned)count);
    radiogeddon_scene_show_progress(app, app->multi_header);
}

void radiogeddon_scene_multi_result_on_enter(void* context) {
    RadioGeddonApp* app = context;
    size_t n = app->multi_count;
    text_box_reset(app->text_box);
    text_box_set_font(app->text_box, TextBoxFontText);
    if(!radiogeddon_scene_module_load(
           app,
           RADIOGEDDON_MODULE_MULTI,
           radiogeddon_scene_multi_memory(n) + MULTI_REPORT_SIZE + MULTI_HEAP_SPARE)) {
        furi_string_printf(app->temp_str, "%s\n\n%s\n", app->message_header, app->message_text);
        text_box_set_text(app->text_box, furi_string_get_cstr(app->temp_str));
        view_dispatcher_switch_to_view(app->view_dispatcher, RadioGeddonViewTextBox);
        return;
    }
    const RadioGeddonMultiModule* api = app->module_api;
    const char* paths[RG_MULTI_MAX_CAPTURES];
    for(size_t i = 0; i < n; i++)
        paths[i] = furi_string_get_cstr(app->multi_paths[i]);
    app->multi_report = malloc(MULTI_REPORT_SIZE);
    api->compare(
        app->storage,
        paths,
        n,
        NULL,
        app->multi_report,
        MULTI_REPORT_SIZE,
        radiogeddon_scene_multi_result_file,
        radiogeddon_scene_progress,
        app);
    radiogeddon_scene_progress_end(app);
    radiogeddon_memdiag_sample("Multi-compare");
    radiogeddon_scene_module_unload(app);

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
