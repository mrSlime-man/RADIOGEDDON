/**
 * @file radiogeddon_research.h
 * @brief Full edition: the unified research workflow's state.
 *
 * - The recording in context: the last one saved, or opened from the
 *   Database or the Analyze menu. Tools started from a tip, the Dolphin Lab
 *   room or a menu act on it without asking for the file again, and what has
 *   been done with it (analysed, searched for similar ones, compared, opened
 *   in Pulse Studio, added to the session) is remembered for this run only.
 * - The Research Journal (helpers/rg_journal.h): every research event is
 *   recorded here; newly earned achievements make the dolphin celebrate.
 *   The journal is written to the SD card a few seconds after a change,
 *   never while a recording runs, and on exit (journal.tmp, then renamed),
 *   so a write cut short never damages the previous file.
 * - The Dolphin Assistant (helpers/rg_assist.h): its facts come only from
 *   the above and the radio's real state.
 *
 * Nothing here touches the radio; memory is the struct in the app (~1 KB).
 */
#pragma once

#include "../radiogeddon_edition.h"

#if RG_FEATURE_JOURNAL

#include <furi.h>
#include "rg_journal.h"
#include "rg_assist.h"

#define RADIOGEDDON_JOURNAL_PATH EXT_PATH("apps_data/radiogeddon/journal.bin")
#define RADIOGEDDON_JOURNAL_TEMP EXT_PATH("apps_data/radiogeddon/journal.tmp")
#define RADIOGEDDON_JOURNAL_MARK EXT_PATH("apps_data/radiogeddon/journal.cmt")
#define RADIOGEDDON_JOURNAL_BAD  EXT_PATH("apps_data/radiogeddon/journal.bad")

/* Recordings remembered as similar to the one in context (for Compare). */
#define RADIOGEDDON_RESEARCH_SIMILAR 4u
#define RADIOGEDDON_RESEARCH_NAME    64u

typedef struct RadioGeddonApp RadioGeddonApp;

/** What has been done with the recording in context. */
typedef enum {
    RadioGeddonStepAnalyzed, // arg: signal frames the analysis found
    RadioGeddonStepSearched, // arg: recordings found similar (score >= 70)
    RadioGeddonStepCompared,
    RadioGeddonStepStudio,
    RadioGeddonStepInSession,
} RadioGeddonResearchStep;

typedef struct {
    RgJournal journal;
    RgAssist assist;
    bool dirty;
    uint32_t dirty_tick;
    uint32_t fresh_achievements; // earned and not yet seen on the Journal
    // The recording in context ("" when none).
    char ctx_name[RADIOGEDDON_RESEARCH_NAME];
    bool ctx_raw;
    bool ctx_analyzed;
    bool ctx_too_few;
    bool ctx_searched;
    bool ctx_compared;
    bool ctx_studio;
    bool ctx_in_session;
    uint8_t ctx_similar;
    char similar[RADIOGEDDON_RESEARCH_SIMILAR][RADIOGEDDON_RESEARCH_NAME];
    // The active session as this run saw it.
    uint8_t session_added; // recordings added to it this run
    bool session_reported;
    uint8_t tip; // RgTip on show
} RadioGeddonResearch;

/** Read the journal (app start); a damaged file is kept as journal.bad. */
void radiogeddon_research_alloc(RadioGeddonApp* app);
/** Write the journal if it changed (app exit). */
void radiogeddon_research_free(RadioGeddonApp* app);

/**
 * Record a research event (@p note: a file, session or protocol name, may be
 * NULL). Newly earned achievements make the dolphin celebrate.
 */
void radiogeddon_research_record(
    RadioGeddonApp* app,
    RgJournalEvent event,
    uint32_t count,
    const char* note);

/** The recording at @p path is now the one in context (RAW or not). */
void radiogeddon_research_set_context(RadioGeddonApp* app, const char* path, bool raw);

/** Full path of the recording in context into @p out; false when none. */
bool radiogeddon_research_context_path(RadioGeddonApp* app, FuriString* out);

/** A step done on the recording at @p path (ignored for another file). */
void radiogeddon_research_step(
    RadioGeddonApp* app,
    const char* path,
    RadioGeddonResearchStep step,
    uint32_t arg);

/** Remember up to RADIOGEDDON_RESEARCH_SIMILAR names found similar to it. */
void radiogeddon_research_set_similar(RadioGeddonApp* app, const char* const* names, size_t n);

/** A recording was added to the active session (from Receive or a tip). */
void radiogeddon_research_session_added(RadioGeddonApp* app);

/** The app's 100 ms tick: deferred journal writes. */
void radiogeddon_research_tick(RadioGeddonApp* app);

/** Write the journal now if it changed. */
bool radiogeddon_research_save(RadioGeddonApp* app);

/** The tip to show now (RgTipNone: none), rate-limited by rg_assist. */
RgTip radiogeddon_research_tip(RadioGeddonApp* app);

/** The tip on show was dismissed. */
void radiogeddon_research_dismiss_tip(RadioGeddonApp* app);

/** Clear the journal's history (counters stay) or everything; saved at once. */
bool radiogeddon_research_clear(RadioGeddonApp* app, bool everything);

#endif
