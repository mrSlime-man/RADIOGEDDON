#include "radiogeddon_scene.h"

#if RG_FEATURE_BITSTREAM

// Full edition: Bitstream Explorer for the open RAW capture. The scene, its
// screen and the analyzer are a module (radiogeddon_modules.h), loaded with
// the scene; the analyzer streams the file (three passes, never loaded
// whole) and only its result, the frames and their bits (an RgAnalysis), is
// kept while the explorer is open. Screen and result are freed on exit.

typedef enum {
    BitstreamEventFailed = 970,
} BitstreamEvent;

static const char* radiogeddon_scene_bitstream_error(RadioGeddonAnalysisStatus status) {
    switch(status) {
    case RadioGeddonAnalysisNoMemory:
        return "Not enough free\nmemory. Close other\napps and retry.";
    case RadioGeddonAnalysisOpenFailed:
        return "Could not open\nthe file.";
    case RadioGeddonAnalysisCorrupt:
        return "RAW data is damaged:\nno readable samples.";
    default:
        return "No RAW timing data:\nthe explorer needs a\nRAW capture.";
    }
}

void radiogeddon_scene_bitstream_on_enter(void* context) {
    RadioGeddonApp* app = context;
    RadioGeddonAnalysisStatus status = RadioGeddonAnalysisOk;
    // The analyzer and its copied result exist together for a moment. The
    // module was loaded with this much to spare (scene_tools.c).
    size_t work = RADIOGEDDON_BITSTREAM_WORK;
    if(memmgr_heap_get_max_free_block() < work) {
        status = RadioGeddonAnalysisNoMemory;
    } else {
        radiogeddon_scene_show_progress(app, "Reading bits...");
        // This module's analyzer reports to the busy popup too.
        radiogeddon_analysis_set_progress(radiogeddon_scene_progress, app);
        RgAnalyzer* a = radiogeddon_analysis_run_file(
            app->storage, furi_string_get_cstr(app->file_path), &status);
        radiogeddon_analysis_set_progress(NULL, NULL);
        radiogeddon_scene_progress_end(app);
        if(a) {
            app->bits_doc = malloc(sizeof(RgAnalysis));
            memcpy(app->bits_doc, &a->result, sizeof(RgAnalysis));
            free(a);
        }
    }
    if(!app->bits_doc) {
        app->message_header = "Cannot explore";
        app->message_text = radiogeddon_scene_bitstream_error(status);
        view_dispatcher_send_custom_event(app->view_dispatcher, BitstreamEventFailed);
        return;
    }

    app->bits_view = radiogeddon_bits_view_alloc();
    view_dispatcher_add_view(
        app->view_dispatcher, RadioGeddonViewBits, radiogeddon_bits_view_get_view(app->bits_view));
    radiogeddon_bits_view_set_analysis(app->bits_view, app->bits_doc);
    radiogeddon_memdiag_sample("Bitstream");
    view_dispatcher_switch_to_view(app->view_dispatcher, RadioGeddonViewBits);
}

bool radiogeddon_scene_bitstream_on_event(void* context, SceneManagerEvent event) {
    RadioGeddonApp* app = context;
    if(!app->bits_doc && event.type == SceneManagerEventTypeCustom &&
       event.event == BitstreamEventFailed) {
        // Leave first, then explain: never back into a scene that failed.
        const char* header = app->message_header;
        const char* text = app->message_text;
        scene_manager_previous_scene(app->scene_manager);
        radiogeddon_scene_show_message(app, header, text);
        return true;
    }
    return false;
}

void radiogeddon_scene_bitstream_on_exit(void* context) {
    RadioGeddonApp* app = context;
    if(app->bits_view) {
        view_dispatcher_remove_view(app->view_dispatcher, RadioGeddonViewBits);
        radiogeddon_bits_view_free(app->bits_view);
        app->bits_view = NULL;
    }
    free(app->bits_doc);
    app->bits_doc = NULL;
}

#endif
