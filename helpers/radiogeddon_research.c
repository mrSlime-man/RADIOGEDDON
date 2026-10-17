#include "radiogeddon_research.h"
#include "../radiogeddon_edition.h" /* RG_LAB_TEXT */

#if RG_FEATURE_JOURNAL

#include "../radiogeddon.h"
#include "../scenes/radiogeddon_scene.h"
#include "radiogeddon_safefile.h"
#include <furi_hal_rtc.h>

#define TAG "RadioGeddonJournal"

/* A changed journal is written this long after the last change. */
#define RESEARCH_SAVE_DELAY_MS 3000u

static RG_LAB_TEXT uint32_t research_time(void) {
    return furi_hal_rtc_get_timestamp();
}

static const RgSafeFile journal_file = {
    RADIOGEDDON_JOURNAL_PATH,
    RADIOGEDDON_JOURNAL_TEMP,
    RADIOGEDDON_JOURNAL_MARK,
};

static RG_LAB_TEXT RadioGeddonResearch* research(RadioGeddonApp* app) {
    return &app->research;
}

RG_LAB_TEXT void radiogeddon_research_alloc(RadioGeddonApp* app) {
    RadioGeddonResearch* r = research(app);
    memset(r, 0, sizeof(*r));
    rg_journal_init(&r->journal);
    rg_assist_init(&r->assist, app->settings.assistant);
    size_t size = rg_journal_encoded_size();
    uint8_t* buf = malloc(size);
    radiogeddon_safefile_recover(app->storage, &journal_file);
    File* file = storage_file_alloc(app->storage);
    bool present = false;
    size_t got = 0;
    if(storage_file_open(file, RADIOGEDDON_JOURNAL_PATH, FSAM_READ, FSOM_OPEN_EXISTING)) {
        present = true;
        got = storage_file_read(file, buf, size);
        // A longer file is not this version's either.
        uint8_t extra;
        if(got == size && storage_file_read(file, &extra, 1) > 0) got = 0;
    }
    storage_file_close(file);
    storage_file_free(file);
    if(present && !rg_journal_decode(&r->journal, buf, got)) {
        // Keep the damaged file for inspection; start a new journal.
        FURI_LOG_W(TAG, "journal damaged, starting over");
        storage_common_remove(app->storage, RADIOGEDDON_JOURNAL_BAD);
        storage_common_rename(app->storage, RADIOGEDDON_JOURNAL_PATH, RADIOGEDDON_JOURNAL_BAD);
    }
    free(buf);
}

RG_LAB_TEXT bool radiogeddon_research_save(RadioGeddonApp* app) {
    RadioGeddonResearch* r = research(app);
    if(!r->dirty) return true;
    size_t size = rg_journal_encoded_size();
    uint8_t* buf = malloc(size);
    size_t n = rg_journal_encode(&r->journal, buf, size);
    bool ok = n == size && radiogeddon_safefile_begin(app->storage, &journal_file);
    if(ok) {
        File* file = storage_file_alloc(app->storage);
        ok = storage_file_open(file, RADIOGEDDON_JOURNAL_TEMP, FSAM_WRITE, FSOM_CREATE_ALWAYS) &&
             storage_file_write(file, buf, n) == n;
        ok = storage_file_close(file) && ok;
        storage_file_free(file);
        if(ok) {
            ok = radiogeddon_safefile_commit(app->storage, &journal_file);
        } else {
            storage_common_remove(app->storage, RADIOGEDDON_JOURNAL_TEMP);
        }
    }
    free(buf);
    if(!ok) {
        FURI_LOG_W(TAG, "journal not saved");
        // Retry later rather than at every tick.
        r->dirty_tick = furi_get_tick() + 30000u;
        return false;
    }
    r->dirty = false;
    return true;
}

RG_LAB_TEXT void radiogeddon_research_free(RadioGeddonApp* app) {
    radiogeddon_research_save(app);
}

RG_LAB_TEXT void radiogeddon_research_record(
    RadioGeddonApp* app,
    RgJournalEvent event,
    uint32_t count,
    const char* note) {
    RadioGeddonResearch* r = research(app);
    uint8_t level = rg_journal_level(rg_journal_xp(&r->journal));
    uint32_t fresh = rg_journal_record(&r->journal, event, count, note, research_time());
    r->dirty = true;
    r->dirty_tick = furi_get_tick();
    bool up = rg_journal_level(rg_journal_xp(&r->journal)) > level;
    if((fresh || up) && app->settings.research_xp) {
        r->fresh_achievements |= fresh;
#if RG_FEATURE_DOLPHIN_LAB
        radiogeddon_ui_event(app, RgMascotEventAchievement, 0);
#endif
    }
    if(event == RgJrnReport) r->session_reported = true;
}

static RG_LAB_TEXT const char* research_name(const char* path) {
    const char* slash = strrchr(path, '/');
    return slash ? slash + 1 : path;
}

RG_LAB_TEXT void
    radiogeddon_research_set_context(RadioGeddonApp* app, const char* path, bool raw) {
    RadioGeddonResearch* r = research(app);
    const char* name = research_name(path);
    if(strcmp(name, r->ctx_name) == 0 && r->ctx_raw == raw) return;
    strlcpy(r->ctx_name, name, sizeof(r->ctx_name));
    r->ctx_raw = raw;
    r->ctx_analyzed = false;
    r->ctx_too_few = false;
    r->ctx_searched = false;
    r->ctx_compared = false;
    r->ctx_studio = false;
    r->ctx_in_session = false;
    r->ctx_similar = 0;
    memset(r->similar, 0, sizeof(r->similar));
    rg_assist_new_context(&r->assist);
}

RG_LAB_TEXT bool radiogeddon_research_context_path(RadioGeddonApp* app, FuriString* out) {
    RadioGeddonResearch* r = research(app);
    if(!r->ctx_name[0]) return false;
    furi_string_printf(out, "%s/%s", RADIOGEDDON_SIGNALS_FOLDER, r->ctx_name);
    // A recording deleted or renamed since is no longer the context.
    if(!storage_file_exists(app->storage, furi_string_get_cstr(out))) {
        r->ctx_name[0] = '\0';
        return false;
    }
    return true;
}

RG_LAB_TEXT void radiogeddon_research_step(
    RadioGeddonApp* app,
    const char* path,
    RadioGeddonResearchStep step,
    uint32_t arg) {
    RadioGeddonResearch* r = research(app);
    if(!path || strcmp(research_name(path), r->ctx_name) != 0) return;
    switch(step) {
    case RadioGeddonStepAnalyzed:
        r->ctx_analyzed = true;
        r->ctx_too_few = arg < 2;
        break;
    case RadioGeddonStepSearched:
        r->ctx_searched = true;
        r->ctx_similar = (uint8_t)(arg > 255 ? 255 : arg);
        break;
    case RadioGeddonStepCompared:
        r->ctx_compared = true;
        break;
    case RadioGeddonStepStudio:
        r->ctx_studio = true;
        break;
    case RadioGeddonStepInSession:
        r->ctx_in_session = true;
        break;
    }
}

RG_LAB_TEXT void
    radiogeddon_research_set_similar(RadioGeddonApp* app, const char* const* names, size_t n) {
    RadioGeddonResearch* r = research(app);
    memset(r->similar, 0, sizeof(r->similar));
    for(size_t i = 0; i < n && i < RADIOGEDDON_RESEARCH_SIMILAR; i++)
        strlcpy(r->similar[i], names[i], sizeof(r->similar[i]));
}

RG_LAB_TEXT void radiogeddon_research_session_added(RadioGeddonApp* app) {
    RadioGeddonResearch* r = research(app);
    if(r->session_added < 255) r->session_added++;
    r->session_reported = false;
}

RG_LAB_TEXT void radiogeddon_research_tick(RadioGeddonApp* app) {
    RadioGeddonResearch* r = research(app);
    if(!r->dirty) return;
    // Never compete with a recording for the card.
    if(radiogeddon_subghz_is_recording(app->subghz)) return;
    if((int32_t)(furi_get_tick() - r->dirty_tick) < (int32_t)RESEARCH_SAVE_DELAY_MS) return;
    radiogeddon_research_save(app);
}

RG_LAB_TEXT RgTip radiogeddon_research_tip(RadioGeddonApp* app) {
    RadioGeddonResearch* r = research(app);
    r->assist.enabled = app->settings.assistant;
    RgAssistFacts f;
    memset(&f, 0, sizeof(f));
    f.recording = radiogeddon_subghz_is_recording(app->subghz);
    f.low_memory = !radiogeddon_scene_decoders_fit(app);
#if RG_FEATURE_DOLPHIN_LAB
    f.classic = !radiogeddon_ui_active(app);
#else
    f.classic = true;
#endif
    FuriString* path = furi_string_alloc();
    f.have_ctx = radiogeddon_research_context_path(app, path);
    furi_string_free(path);
    f.ctx_raw = r->ctx_raw;
    f.ctx_analyzed = r->ctx_analyzed;
    f.ctx_too_few_frames = r->ctx_too_few;
    f.ctx_searched = r->ctx_searched;
    f.ctx_similar = r->ctx_similar;
    f.ctx_compared = r->ctx_compared;
    f.ctx_studio = r->ctx_studio;
    f.ctx_in_session = r->ctx_in_session;
#if RG_FEATURE_SESSIONS
    char session[RG_SESSION_NAME_MAX];
    radiogeddon_session_active(app->storage, session, sizeof(session));
    f.session_active = session[0] != '\0';
#endif
    f.session_signals = r->session_added;
    f.session_reported = r->session_reported;
    RgTip tip = rg_assist_update(&r->assist, &f, furi_get_tick());
    r->tip = (uint8_t)tip;
    return tip;
}

RG_LAB_TEXT void radiogeddon_research_dismiss_tip(RadioGeddonApp* app) {
    rg_assist_dismiss(&research(app)->assist);
    research(app)->tip = RgTipNone;
}

RG_LAB_TEXT bool radiogeddon_research_clear(RadioGeddonApp* app, bool everything) {
    RadioGeddonResearch* r = research(app);
    if(everything) {
        rg_journal_init(&r->journal);
        r->fresh_achievements = 0;
    } else {
        rg_journal_clear_history(&r->journal);
    }
    r->dirty = true;
    return radiogeddon_research_save(app);
}

#endif
