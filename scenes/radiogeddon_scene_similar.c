#include "../helpers/radiogeddon_sessions.h"
#include "radiogeddon_scene.h"
#include "../helpers/radiogeddon_fp.h"

#if RG_FEATURE_FINGERPRINT

/*
 * Find Similar Signals (Full edition, in the fingerprint module with its
 * engine): the index is brought up to date and every recording ranked
 * against the open one (app->file_path). The results stay while this screen or one opened from it
 * is on the stack, so returning here never searches again; they are freed at
 * the main menu. Similar structure is shown as a heuristic, never as proof
 * of a common transmitter.
 *
 * Scope: all recordings, those saved within an hour or a day of the
 * target, or those of the active research session (offered only while one
 * is active). The "Scope" row cycles and ranks again; the index is reused.
 *
 * Scene state: the list (low byte 0) or a match's actions (1), and the
 * match chosen (above it).
 */

static const uint32_t similar_scope_s[] = {0, 3600u, 86400u, 0};
static const char* const similar_scope_text[] = {
    "Scope: all recordings",
    "Scope: within 1 hour",
    "Scope: within 1 day",
    "Scope: session",
};
#define SIMILAR_SCOPE_SESSION 3u

typedef struct RadioGeddonSimilar {
    RadioGeddonFpResult res;
    char why[RG_FP_TOP][200];
    char target[RG_FP_NAME_MAX];
    uint8_t scope; // index into similar_scope_s
    char session[RG_SESSION_NAME_MAX]; // the session searched (session scope)
} RadioGeddonSimilar;

typedef enum {
    SimilarModeList = 0,
    SimilarModeMatch = 1,
    SimilarModeWhy = 2,
} SimilarMode;

typedef enum {
    SimilarItemInfo = 900, // the summary line (no action)
    SimilarItemMatchBase = 910, // + match
    SimilarItemWhy = 950,
    SimilarItemOpen,
    SimilarItemCompare,
    SimilarItemCompareAll,
    SimilarItemScope,
    SimilarEventFailed = 960,
} SimilarItem;

static const char* similar_name(const char* path) {
    const char* slash = strrchr(path, '/');
    return slash ? slash + 1 : path;
}

static void similar_release(RadioGeddonApp* app) {
    free(app->similar);
    app->similar = NULL;
}

static void similar_cb(void* context, uint32_t index) {
    RadioGeddonApp* app = context;
    view_dispatcher_send_custom_event(app->view_dispatcher, index);
}

static void similar_show_list(RadioGeddonApp* app, uint32_t selected) {
    RadioGeddonSimilar* s = app->similar;
    Submenu* submenu = app->submenu;
    submenu_reset(submenu);
    submenu_set_header(submenu, "Similar signals");
    char line[64];
    const RadioGeddonFpResult* r = &s->res;
    for(size_t i = 0; i < r->count; i++) {
        snprintf(line, sizeof(line), "%u%% %s", (unsigned)r->hit[i].match.score, r->hit[i].name);
        submenu_add_item(submenu, line, SimilarItemMatchBase + i, similar_cb, app);
    }
    if(!r->target_found) {
        submenu_add_item(submenu, "Cannot read this file", SimilarItemInfo, similar_cb, app);
    } else if(!r->target_structure) {
        submenu_add_item(submenu, "No structure to match", SimilarItemInfo, similar_cb, app);
    } else if(r->count == 0) {
        submenu_add_item(submenu, "No similar recordings", SimilarItemInfo, similar_cb, app);
    }
    if(r->index_failed) {
        snprintf(line, sizeof(line), "%lu compared, index not saved", (unsigned long)r->compared);
    } else if(r->truncated) {
        // The index holds what the heap (or RG_FP_INDEX_MAX) allowed.
        snprintf(
            line,
            sizeof(line),
            "%lu compared, first %lu",
            (unsigned long)r->compared,
            (unsigned long)r->limit);
    } else if(r->out_of_scope) {
        snprintf(
            line,
            sizeof(line),
            "%lu compared, %lu out of scope",
            (unsigned long)r->compared,
            (unsigned long)r->out_of_scope);
    } else {
        snprintf(line, sizeof(line), "%lu compared", (unsigned long)r->compared);
    }
    submenu_add_item(submenu, line, SimilarItemInfo, similar_cb, app);
    if(s->scope == SIMILAR_SCOPE_SESSION) {
        snprintf(line, sizeof(line), "Scope: %s", s->session);
        submenu_add_item(submenu, line, SimilarItemScope, similar_cb, app);
    } else {
        submenu_add_item(submenu, similar_scope_text[s->scope], SimilarItemScope, similar_cb, app);
    }
    if(r->count > 1)
        submenu_add_item(submenu, "Compare all matches", SimilarItemCompareAll, similar_cb, app);
    submenu_set_selected_item(submenu, selected);
    view_dispatcher_switch_to_view(app->view_dispatcher, RadioGeddonViewSubmenu);
}

static void similar_show_match(RadioGeddonApp* app, size_t i) {
    RadioGeddonSimilar* s = app->similar;
    Submenu* submenu = app->submenu;
    submenu_reset(submenu);
    submenu_set_header(submenu, s->res.hit[i].name);
    submenu_add_item(submenu, "Why similar?", SimilarItemWhy, similar_cb, app);
    submenu_add_item(submenu, "Open recording", SimilarItemOpen, similar_cb, app);
    submenu_add_item(submenu, "Compare with this", SimilarItemCompare, similar_cb, app);
    view_dispatcher_switch_to_view(app->view_dispatcher, RadioGeddonViewSubmenu);
}

/* Run the search. */
static bool similar_search(RadioGeddonApp* app) {
    // The session scope reads the session (its names) for the search only.
    size_t need = radiogeddon_fp_memory() + sizeof(RadioGeddonSimilar) + sizeof(RgSession);
    if(memmgr_heap_get_max_free_block() < need) {
        app->message_header = "Not enough memory";
        app->message_text = "Close other tools or\nuse Classic mode.";
        return false;
    }
    uint8_t scope = 0;
    if(!app->similar) {
        app->similar = malloc(sizeof(RadioGeddonSimilar));
    } else {
        scope = app->similar->scope; // a new scope ranks again
    }
    RadioGeddonSimilar* s = app->similar;
    memset(s, 0, sizeof(*s));
    s->scope = scope;
    strlcpy(s->target, similar_name(furi_string_get_cstr(app->file_path)), sizeof(s->target));
    RgSession* session = NULL;
    if(s->scope == SIMILAR_SCOPE_SESSION) {
        radiogeddon_session_active(app->storage, s->session, sizeof(s->session));
        session = malloc(sizeof(RgSession));
        RgSessionLoad load = s->session[0] ?
                                 radiogeddon_session_load(app->storage, s->session, session) :
                                 RgSessionLoadMissing;
        if(load != RgSessionLoadOk && load != RgSessionLoadRecovered) {
            // The session went away: rank everything instead.
            free(session);
            session = NULL;
            s->scope = 0;
        }
    }
    radiogeddon_scene_show_progress(app, "Fingerprinting...");
    bool ok = radiogeddon_fp_find_similar(
        app->storage,
        furi_string_get_cstr(app->file_path),
        similar_scope_s[s->scope],
        session,
        &s->res,
        radiogeddon_scene_progress,
        app);
    radiogeddon_scene_progress_end(app);
    free(session);
    if(ok) {
        for(size_t i = 0; i < s->res.count; i++)
            radiogeddon_fp_explain(&s->res, i, s->why[i], sizeof(s->why[i]));
    }
    if(!ok) {
        similar_release(app);
        app->message_header = "Not enough memory";
        app->message_text = "Close other tools or\nuse Classic mode.";
        return false;
    }
#if RG_FEATURE_JOURNAL
    const RadioGeddonFpResult* r = &s->res;
    if(r->computed) radiogeddon_research_record(app, RgJrnFingerprint, r->computed, NULL);
    radiogeddon_research_record(app, RgJrnSimilar, 1, s->target);
    uint32_t strong = 0;
    const char* names[RADIOGEDDON_RESEARCH_SIMILAR];
    for(size_t i = 0; i < r->count; i++) {
        if(r->hit[i].match.score < 70) continue;
        if(strong < RADIOGEDDON_RESEARCH_SIMILAR) names[strong] = r->hit[i].name;
        strong++;
    }
    radiogeddon_research_set_similar(
        app, names, strong < RADIOGEDDON_RESEARCH_SIMILAR ? strong : RADIOGEDDON_RESEARCH_SIMILAR);
    radiogeddon_research_step(
        app, furi_string_get_cstr(app->file_path), RadioGeddonStepSearched, strong);
#endif
    if(s->res.count && s->res.hit[0].match.score >= 85)
        radiogeddon_ui_event(app, RgMascotEventMatch, 0);
    return true;
}

void radiogeddon_scene_similar_on_enter(void* context) {
    RadioGeddonApp* app = context;
    uint32_t state = scene_manager_get_scene_state(app->scene_manager, RadioGeddonSceneSimilar);
    const char* target = similar_name(furi_string_get_cstr(app->file_path));
    // Back from a screen opened here: the results are still those.
    if(!app->similar || strcmp(app->similar->target, target) != 0) {
        state = 0;
        if(!similar_search(app)) {
            view_dispatcher_send_custom_event(app->view_dispatcher, SimilarEventFailed);
            return;
        }
    }
    scene_manager_set_scene_state(app->scene_manager, RadioGeddonSceneSimilar, state & ~0xFFu);
    similar_show_list(app, state >> 8 ? SimilarItemMatchBase + (state >> 8) - 1u : 0);
}

/* Load @p name of the signals folder into app->loaded (file_path) or
 * loaded_b (file_path_b). */
static bool similar_load(RadioGeddonApp* app, const char* name, bool second) {
    FuriString* path = second ? app->file_path_b : app->file_path;
    RadioGeddonLoadedSignal* sig = second ? &app->loaded_b : &app->loaded;
    furi_string_printf(path, "%s/%s", RADIOGEDDON_SIGNALS_FOLDER, name);
    radiogeddon_loaded_signal_reset(sig);
    radiogeddon_loaded_signal_init(sig);
    return radiogeddon_storage_load(app->storage, furi_string_get_cstr(path), sig);
}

bool radiogeddon_scene_similar_on_event(void* context, SceneManagerEvent event) {
    RadioGeddonApp* app = context;
    if(event.type == SceneManagerEventTypeBack) {
        uint32_t state =
            scene_manager_get_scene_state(app->scene_manager, RadioGeddonSceneSimilar);
        if((state & 0xFFu) == SimilarModeWhy) {
            scene_manager_set_scene_state(
                app->scene_manager, RadioGeddonSceneSimilar, (state & ~0xFFu) | SimilarModeMatch);
            similar_show_match(app, (state >> 8) - 1u);
            return true;
        }
        if((state & 0xFFu) == SimilarModeMatch) {
            scene_manager_set_scene_state(
                app->scene_manager, RadioGeddonSceneSimilar, state & ~0xFFu);
            similar_show_list(app, SimilarItemMatchBase + (state >> 8) - 1u);
            return true;
        }
        return false;
    }
    if(event.type != SceneManagerEventTypeCustom) return false;
    if(event.event == SimilarEventFailed) {
        const char* header = app->message_header;
        const char* text = app->message_text;
        scene_manager_previous_scene(app->scene_manager);
        radiogeddon_scene_show_message(app, header, text);
        return true;
    }
    RadioGeddonSimilar* s = app->similar;
    if(!s) return false;
    uint32_t state = scene_manager_get_scene_state(app->scene_manager, RadioGeddonSceneSimilar);
    size_t match = state >> 8 ? (state >> 8) - 1u : 0;
    if(event.event >= SimilarItemMatchBase && event.event < SimilarItemMatchBase + RG_FP_TOP) {
        match = event.event - SimilarItemMatchBase;
        scene_manager_set_scene_state(
            app->scene_manager, RadioGeddonSceneSimilar, ((match + 1u) << 8) | SimilarModeMatch);
        similar_show_match(app, match);
        return true;
    }
    switch(event.event) {
    case SimilarItemScope:
        s->scope = (uint8_t)((s->scope + 1u) % COUNT_OF(similar_scope_s));
        if(s->scope == SIMILAR_SCOPE_SESSION) {
            // Offered only while a session is active.
            char active[RG_SESSION_NAME_MAX];
            radiogeddon_session_active(app->storage, active, sizeof(active));
            if(!active[0]) s->scope = 0;
        }
        if(!similar_search(app)) {
            view_dispatcher_send_custom_event(app->view_dispatcher, SimilarEventFailed);
            return true;
        }
        scene_manager_set_scene_state(app->scene_manager, RadioGeddonSceneSimilar, 0);
        similar_show_list(app, SimilarItemScope);
        return true;
    case SimilarItemWhy:
        scene_manager_set_scene_state(
            app->scene_manager, RadioGeddonSceneSimilar, (state & ~0xFFu) | SimilarModeWhy);
        text_box_reset(app->text_box);
        text_box_set_font(app->text_box, TextBoxFontText);
        text_box_set_text(app->text_box, s->why[match]);
        view_dispatcher_switch_to_view(app->view_dispatcher, RadioGeddonViewTextBox);
        return true;
    case SimilarItemOpen:
        if(similar_load(app, s->res.hit[match].name, false)) {
            app->file_damaged = false;
            app->have_loaded_signal = true;
#if RG_FEATURE_JOURNAL
            radiogeddon_research_set_context(
                app,
                furi_string_get_cstr(app->file_path),
                app->loaded.kind == RadioGeddonSignalKindRaw);
#endif
            scene_manager_set_scene_state(app->scene_manager, RadioGeddonSceneSavedInfo, 0);
            scene_manager_next_scene(app->scene_manager, RadioGeddonSceneSavedInfo);
        } else {
            radiogeddon_scene_show_message(app, "Cannot open", "Not a readable\n.sub file.");
        }
        return true;
    case SimilarItemCompare: {
        // The recording searched for is A, the match B.
        char target[RG_FP_NAME_MAX];
        strlcpy(target, s->target, sizeof(target));
        if(similar_load(app, target, false) && similar_load(app, s->res.hit[match].name, true)) {
            scene_manager_next_scene(app->scene_manager, RadioGeddonSceneCompareResult);
        } else {
            radiogeddon_scene_show_message(
                app, "Cannot compare", "A file is not a\nreadable .sub file.");
        }
        return true;
    }
    case SimilarItemCompareAll: {
#if RG_FEATURE_MULTI_COMPARE
        radiogeddon_multi_clear(app);
        FuriString* path =
            furi_string_alloc_printf("%s/%s", RADIOGEDDON_SIGNALS_FOLDER, s->target);
        radiogeddon_multi_add(app, furi_string_get_cstr(path));
        for(size_t i = 0; i < s->res.count; i++) {
            furi_string_printf(path, "%s/%s", RADIOGEDDON_SIGNALS_FOLDER, s->res.hit[i].name);
            if(!radiogeddon_multi_add(app, furi_string_get_cstr(path))) break;
        }
        furi_string_free(path);
        scene_manager_set_scene_state(app->scene_manager, RadioGeddonSceneMulti, 0);
        scene_manager_next_scene(app->scene_manager, RadioGeddonSceneMulti);
#endif
        return true;
    }
    default:
        return true; // the summary line
    }
}

void radiogeddon_scene_similar_on_exit(void* context) {
    RadioGeddonApp* app = context;
    radiogeddon_ui_cancel(app);
    submenu_reset(app->submenu);
    text_box_reset(app->text_box);
}

#endif
