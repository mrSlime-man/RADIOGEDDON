/**
 * @file rg_assist.h
 * @brief Dolphin Assistant (Full edition): the next research step, chosen
 * by fixed rules from what the app really did.
 *
 * No language model and nothing learned: the app fills RgAssistFacts from
 * its own state (the recording in context and what has been done with it,
 * the active session, the last analysis' outcome, memory) and the rules
 * below, in order, pick at most one tip. Each tip names an action the app
 * maps to a screen, so choosing it opens the tool on the recording or
 * session already in context. A tip never transmits, never says what a
 * signal is for or whose it is, and never describes an analysis that has
 * not been run.
 *
 *   recording in progress        -> nothing (never disturb a recording)
 *   low memory, lab style        -> Try Classic mode
 *   last analysis found too few
 *     frames on the recording    -> Record more samples?
 *   RAW recording not analysed   -> Analyze this signal?
 *   similar recordings found,
 *     not compared yet           -> Compare captures?
 *   analysed, no similarity
 *     search yet                 -> Find similar signals?
 *   analysed, Pulse Studio not
 *     opened on it               -> Inspect pulse timing?
 *   a session is active and the
 *     recording is not in it     -> Add to session?
 *   the session has 2+ signals
 *     and no report yet          -> Export report?
 *
 * Tips are rate-limited (RG_ASSIST_MIN_GAP_MS between new tips, and one tip
 * is not repeated within RG_ASSIST_REPEAT_MS), a dismissed tip stays away
 * until the context changes (another recording), and the whole assistant
 * can be switched off. Pure C with no SDK headers; host-tested in
 * test/test_assist.c.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    RgTipNone,
    RgTipTryClassic,
    RgTipRecordMore,
    RgTipAnalyze,
    RgTipCompare,
    RgTipFindSimilar,
    RgTipInspectPulses,
    RgTipAddToSession,
    RgTipExportReport,
    RgTipCount,
} RgTip;

/** What the app does when a tip is chosen. */
typedef enum {
    RgTipActionNone,
    RgTipActionSettings, // open Settings (the UI style row)
    RgTipActionReceive, // open Receive & Record
    RgTipActionDiscovery, // Smart Protocol Discovery on the recording
    RgTipActionMultiCompare, // compare the recording with its similar ones
    RgTipActionSimilar, // Find Similar Signals for the recording
    RgTipActionStudio, // Pulse Studio on the recording
    RgTipActionSessionAdd, // add the recording to the active session
    RgTipActionSessionReport, // the active session's report
} RgTipAction;

typedef struct {
    bool recording; // the recorder runs now
    bool low_memory; // a receive session would be refused now
    bool classic; // the Classic style is in use
    /* the recording in context (the last saved or opened) */
    bool have_ctx;
    bool ctx_raw;
    bool ctx_analyzed; // an analysis of it finished
    bool ctx_too_few_frames; // that analysis found fewer than 2 signal frames
    bool ctx_searched; // Find Similar Signals ran on it
    uint8_t ctx_similar; // recordings found similar (score >= 70)
    bool ctx_compared; // it was compared with them
    bool ctx_studio; // Pulse Studio was opened on it
    bool ctx_in_session; // it is in the active session
    /* the active research session */
    bool session_active;
    uint8_t session_signals;
    bool session_reported;
} RgAssistFacts;

#define RG_ASSIST_MIN_GAP_MS 15000u // between two different new tips
#define RG_ASSIST_REPEAT_MS  300000u // before the same tip shows again

typedef struct {
    bool enabled;
    uint32_t context; // generation of the recording in context
    uint32_t dismissed; // tips dismissed in this context (bits)
    uint32_t last_shown_ms[RgTipCount];
    uint32_t shown_mask; // tips shown at least once (bits)
    uint32_t last_new_ms; // when the last new tip appeared
    uint8_t current; // tip on show now
    bool any_shown;
} RgAssist;

void rg_assist_init(RgAssist* a, bool enabled);

/** The recording in context changed: dismissed tips may return. */
void rg_assist_new_context(RgAssist* a);

/** The rules alone: the tip the facts call for (no rate limit). */
RgTip rg_assist_rule(const RgAssistFacts* f);

/**
 * The tip to show at @p now_ms, rate limits and dismissals applied; the
 * tip on show stays until the facts no longer call for it.
 */
RgTip rg_assist_update(RgAssist* a, const RgAssistFacts* f, uint32_t now_ms);

/** The user dismissed the tip on show. */
void rg_assist_dismiss(RgAssist* a);

const char* rg_tip_text(RgTip tip);
/** A short form for a menu row (at most 13 characters). */
const char* rg_tip_short(RgTip tip);
RgTipAction rg_tip_action(RgTip tip);
