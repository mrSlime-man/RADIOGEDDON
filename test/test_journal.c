/**
 * Host tests for the Research Journal, XP and achievements
 * (helpers/rg_journal.c) and the Dolphin Assistant's rules and rate limits
 * (helpers/rg_assist.c).
 * Run: make -C test
 */
#include "../helpers/rg_journal.h"
#include "../helpers/rg_assist.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_failures = 0;
static int g_checks = 0;

#define CHECK(cond, msg)                                             \
    do {                                                             \
        g_checks++;                                                  \
        if(!(cond)) {                                                \
            g_failures++;                                            \
            printf("  FAIL: %s (%s:%d)\n", msg, __FILE__, __LINE__); \
        }                                                            \
    } while(0)

static void test_counters_and_recent(void) {
    printf("test_counters_and_recent\n");
    RgJournal j;
    rg_journal_init(&j);
    CHECK(rg_journal_xp(&j) == 0 && rg_journal_level(0) == 1, "empty journal");
    uint32_t got = rg_journal_record(&j, RgJrnCapture, 1, "gate.sub", 100);
    CHECK(got == (1u << RgAchFirstCapture), "first capture earned once");
    got = rg_journal_record(&j, RgJrnCapture, 1, "gate2.sub", 101);
    CHECK(got == 0, "not earned twice");
    CHECK(j.counters[RgJrnCapture] == 2, "counted");
    CHECK(j.earned_time[RgAchFirstCapture] == 100, "earned time");
    rg_journal_record(&j, RgJrnScan, 1, NULL, 102);
    CHECK(j.recent_count == 2, "scans are counted but not listed");
    const RgJournalEntry* e = rg_journal_recent(&j, 0);
    CHECK(e && e->event == RgJrnCapture && strcmp(e->note, "gate2.sub") == 0, "newest first");
    CHECK(rg_journal_recent(&j, 2) == NULL, "past the end");
    for(int i = 0; i < 30; i++)
        rg_journal_record(&j, RgJrnAnalysis, 1, "x", 200 + i);
    CHECK(j.recent_count == RG_JOURNAL_RECENT, "ring is bounded");
    e = rg_journal_recent(&j, RG_JOURNAL_RECENT - 1);
    CHECK(e && e->time == 200 + 30 - RG_JOURNAL_RECENT, "oldest kept");
    // A long or odd note is cut and cleaned.
    rg_journal_record(&j, RgJrnSession, 1, "a very long session name over the limit\x01", 300);
    e = rg_journal_recent(&j, 0);
    CHECK(strlen(e->note) == RG_JOURNAL_NOTE - 1, "note cut");
    uint32_t xp = rg_journal_xp(&j);
    rg_journal_clear_history(&j);
    CHECK(j.recent_count == 0 && rg_journal_recent(&j, 0) == NULL, "history cleared");
    CHECK(rg_journal_xp(&j) == xp && j.counters[RgJrnAnalysis] == 30, "counters stay");
    CHECK(rg_journal_record(&j, RgJrnEventCount, 1, NULL, 1) == 0, "unknown event ignored");
    j.counters[RgJrnCompare] = UINT32_MAX - 1;
    rg_journal_record(&j, RgJrnCompare, 5, NULL, 1);
    CHECK(j.counters[RgJrnCompare] == UINT32_MAX, "saturates");
}

static void test_xp_and_achievements(void) {
    printf("test_xp_and_achievements\n");
    RgJournal j;
    rg_journal_init(&j);
    // Hundreds of captures earn no more than the cap.
    for(int i = 0; i < 400; i++)
        rg_journal_record(&j, RgJrnCapture, 1, NULL, 1);
    uint32_t a = rg_journal_xp(&j);
    for(int i = 0; i < 400; i++)
        rg_journal_record(&j, RgJrnCapture, 1, NULL, 1);
    CHECK(rg_journal_xp(&j) == a, "capture XP is capped");
    CHECK(j.achievements & (1u << RgAchLabRegular), "Lab Regular");
    // Protocols: distinct names only.
    rg_journal_record(&j, RgJrnProtocol, 1, "Princeton", 2);
    rg_journal_record(&j, RgJrnProtocol, 1, "Princeton", 3);
    rg_journal_record(&j, RgJrnProtocol, 1, "CAME", 4);
    CHECK(j.protocol_count == 2, "distinct protocols");
    uint32_t got = rg_journal_record(&j, RgJrnProtocol, 1, "Nice FLO", 5);
    CHECK(got & (1u << RgAchProtocolSpotter), "Protocol Spotter at three");
    // Explorer at five tools, Polymath at all of them.
    RgJournalEvent tools[] = {
        RgJrnAnalysis,
        RgJrnSimilar,
        RgJrnCompare,
        RgJrnMulti,
        RgJrnSession,
        RgJrnReport,
        RgJrnActlog,
        RgJrnStudio,
        RgJrnDiscovery};
    for(size_t i = 0; i < 4; i++)
        rg_journal_record(&j, tools[i], 1, NULL, 10);
    CHECK(!(j.achievements & (1u << RgAchExplorer)), "not yet an explorer");
    got = rg_journal_record(&j, tools[4], 1, NULL, 11);
    CHECK(got & (1u << RgAchExplorer), "Explorer at five tools");
    for(size_t i = 5; i < 9; i++)
        rg_journal_record(&j, tools[i], 1, NULL, 12);
    CHECK(j.achievements & (1u << RgAchPolymath), "Polymath with every tool");
    CHECK(j.achievements == (1u << RgAchCount) - 1u, "every achievement");
    CHECK(rg_journal_achievements_due(&j) == j.achievements, "derived equals stored");
    uint32_t xp = rg_journal_xp(&j);
    uint8_t level = rg_journal_level(xp);
    CHECK(level >= 4 && level <= RG_JOURNAL_MAX_LEVEL, "levelled up");
    CHECK(xp >= rg_journal_level_xp(level), "XP covers the level");
    CHECK(level == RG_JOURNAL_MAX_LEVEL || xp < rg_journal_level_xp(level + 1), "below the next");
    CHECK(rg_journal_level(UINT32_MAX) == RG_JOURNAL_MAX_LEVEL, "level capped");
    CHECK(rg_journal_level_xp(2) == 40 && rg_journal_level_xp(3) == 120, "thresholds");
    // Decorations.
    CHECK(rg_decoration_unlocked(RgDecoNone, 1), "no decoration always");
    CHECK(!rg_decoration_unlocked(RgDecoPlant, 1) && rg_decoration_unlocked(RgDecoPlant, 2),
          "plant at level 2");
    CHECK(!rg_decoration_unlocked(RgDecoCount, 10), "unknown decoration locked");
    CHECK(strcmp(rg_achievement_name(RgAchNightWatch), "Night Watch") == 0, "name");
    CHECK(strcmp(rg_achievement_name(RgAchCount), "?") == 0, "unknown name");
}

static void test_persistence(void) {
    printf("test_persistence\n");
    RgJournal j, k;
    rg_journal_init(&j);
    rg_journal_record(&j, RgJrnCapture, 1, "a.sub", 10);
    rg_journal_record(&j, RgJrnProtocol, 1, "Princeton", 11);
    rg_journal_record(&j, RgJrnSimilar, 1, "a.sub", 12);
    size_t n = rg_journal_encoded_size();
    uint8_t* buf = malloc(n);
    CHECK(rg_journal_encode(&j, buf, n) == n, "encoded");
    CHECK(rg_journal_encode(&j, buf, n - 1) == 0, "refuses a short buffer");
    CHECK(rg_journal_decode(&k, buf, n), "decoded");
    CHECK(memcmp(&j, &k, sizeof(j)) == 0, "round trip");
    // Any one damaged byte: refused, empty, marked recovered.
    size_t refused = 0;
    for(size_t i = 0; i < n; i++) {
        buf[i] ^= 0x5A;
        if(!rg_journal_decode(&k, buf, n) && k.recovered && k.counters[RgJrnCapture] == 0)
            refused++;
        buf[i] ^= 0x5A;
    }
    CHECK(refused == n, "every damaged byte is caught");
    CHECK(!rg_journal_decode(&k, buf, n - 1) && k.recovered, "short file");
    CHECK(!rg_journal_decode(&k, NULL, 0) && k.recovered, "no file");
    free(buf);
}

static RgAssistFacts facts_ctx(void) {
    RgAssistFacts f;
    memset(&f, 0, sizeof(f));
    f.have_ctx = true;
    f.ctx_raw = true;
    return f;
}

static void test_assist_rules(void) {
    printf("test_assist_rules\n");
    RgAssistFacts f;
    memset(&f, 0, sizeof(f));
    CHECK(rg_assist_rule(&f) == RgTipNone, "nothing to suggest");
    f = facts_ctx();
    CHECK(rg_assist_rule(&f) == RgTipAnalyze, "analyze a new RAW recording");
    f.recording = true;
    CHECK(rg_assist_rule(&f) == RgTipNone, "silent while recording");
    f = facts_ctx();
    f.low_memory = true;
    CHECK(rg_assist_rule(&f) == RgTipTryClassic, "low memory first");
    f.classic = true;
    CHECK(rg_assist_rule(&f) == RgTipAnalyze, "already Classic: no such tip");
    f = facts_ctx();
    f.ctx_analyzed = true;
    f.ctx_too_few_frames = true;
    CHECK(rg_assist_rule(&f) == RgTipRecordMore, "too few frames");
    f.ctx_too_few_frames = false;
    CHECK(rg_assist_rule(&f) == RgTipFindSimilar, "then find similar");
    f.ctx_searched = true;
    f.ctx_similar = 2;
    CHECK(rg_assist_rule(&f) == RgTipCompare, "then compare");
    f.ctx_compared = true;
    CHECK(rg_assist_rule(&f) == RgTipInspectPulses, "then Pulse Studio");
    f.ctx_studio = true;
    f.session_active = true;
    CHECK(rg_assist_rule(&f) == RgTipAddToSession, "then the session");
    f.ctx_in_session = true;
    f.session_signals = 2;
    CHECK(rg_assist_rule(&f) == RgTipExportReport, "then the report");
    f.session_reported = true;
    CHECK(rg_assist_rule(&f) == RgTipNone, "workflow complete");
    // A decoded recording is not offered RAW tools.
    f = facts_ctx();
    f.ctx_raw = false;
    CHECK(rg_assist_rule(&f) == RgTipNone, "decoded file, nothing analysed: no RAW tip");
    f.ctx_analyzed = true;
    f.ctx_searched = true;
    CHECK(rg_assist_rule(&f) == RgTipNone, "no Pulse Studio for a decoded file");
    for(int t = 1; t < RgTipCount; t++) {
        CHECK(rg_tip_text((RgTip)t)[0] != '\0', "every tip has text");
        CHECK(rg_tip_action((RgTip)t) != RgTipActionNone, "every tip has an action");
    }
    CHECK(rg_tip_action(RgTipCount) == RgTipActionNone, "unknown tip");
}

static void test_assist_limits(void) {
    printf("test_assist_limits\n");
    RgAssist a;
    rg_assist_init(&a, true);
    RgAssistFacts f = facts_ctx();
    CHECK(rg_assist_update(&a, &f, 1000) == RgTipAnalyze, "first tip at once");
    CHECK(rg_assist_update(&a, &f, 1100) == RgTipAnalyze, "stays while relevant");
    f.ctx_analyzed = true;
    CHECK(rg_assist_update(&a, &f, 2000) == RgTipNone, "a new tip waits for the gap");
    CHECK(rg_assist_update(&a, &f, 1000 + RG_ASSIST_MIN_GAP_MS) == RgTipFindSimilar,
          "then shows");
    rg_assist_dismiss(&a);
    CHECK(rg_assist_update(&a, &f, 100000) != RgTipFindSimilar, "dismissed stays away");
    rg_assist_new_context(&a);
    CHECK(rg_assist_update(&a, &f, 120000) == RgTipNone, "same tip not repeated too soon");
    CHECK(rg_assist_update(&a, &f, 20000 + RG_ASSIST_REPEAT_MS) == RgTipFindSimilar,
          "after the repeat time, in a new context, it may return");
    RgAssist off;
    rg_assist_init(&off, false);
    CHECK(rg_assist_update(&off, &f, 5000) == RgTipNone, "disabled");
    // Recording silences a tip on show at once.
    f.recording = true;
    CHECK(rg_assist_update(&a, &f, 20001 + RG_ASSIST_REPEAT_MS) == RgTipNone, "recording");
    CHECK(a.current == RgTipNone, "nothing on show");
}

int main(void) {
    test_counters_and_recent();
    test_xp_and_achievements();
    test_persistence();
    test_assist_rules();
    test_assist_limits();
    printf("\n%d checks, %d failures\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
