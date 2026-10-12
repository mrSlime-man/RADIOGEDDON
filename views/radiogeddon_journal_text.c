/*
 * Full edition (in the lab module): the Research Journal's pages as text.
 * Every line is a count of recorded events or a recorded entry; nothing is
 * estimated.
 */
#include "radiogeddon_journal_text.h"

#if RG_FEATURE_JOURNAL

#include <datetime/datetime.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    char* out;
    size_t size;
    size_t len;
} JText;

static void jcat(JText* t, const char* fmt, ...) {
    if(t->len + 1 >= t->size) return;
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(t->out + t->len, t->size - t->len, fmt, ap);
    va_end(ap);
    if(n < 0) return;
    if((size_t)n >= t->size - t->len) {
        t->out[t->len] = '\0'; // only whole lines
        t->len = t->size;
        return;
    }
    t->len += (size_t)n;
}

static void jtime(char* out, size_t size, uint32_t ts) {
    if(ts == 0) {
        strlcpy(out, "--", size);
        return;
    }
    DateTime dt;
    datetime_timestamp_to_datetime(ts, &dt);
    snprintf(out, size, "%02u-%02u %02u:%02u", dt.month, dt.day, dt.hour, dt.minute);
}

size_t radiogeddon_journal_text(const RgJournal* j, bool xp_on, char* out, size_t size) {
    JText t = {out, size, 0};
    if(size) out[0] = '\0';
    if(xp_on) {
        uint32_t xp = rg_journal_xp(j);
        uint8_t level = rg_journal_level(xp);
        jcat(&t, "Research level %u, %lu XP\n", (unsigned)level, (unsigned long)xp);
        if(level < RG_JOURNAL_MAX_LEVEL)
            jcat(&t, "Next level at %lu XP\n", (unsigned long)rg_journal_level_xp(level + 1));
    } else {
        jcat(&t, "Research Journal\n");
    }
    if(j->recovered) jcat(&t, "(journal file was damaged;\nstarted over)\n");
    jcat(&t, "\nCounted events:\n");
    static const uint8_t order[] = {
        RgJrnCapture,
        RgJrnAnalysis,
        RgJrnDiscovery,
        RgJrnStudio,
        RgJrnFingerprint,
        RgJrnSimilar,
        RgJrnCompare,
        RgJrnMulti,
        RgJrnSession,
        RgJrnReport,
        RgJrnActlog,
        RgJrnScan,
    };
    for(size_t i = 0; i < sizeof(order); i++)
        jcat(
            &t,
            "%s: %lu\n",
            rg_journal_event_name((RgJournalEvent)order[i]),
            (unsigned long)j->counters[order[i]]);
    jcat(&t, "\nProtocols identified (%u):\n", (unsigned)j->protocol_count);
    if(j->protocol_count == 0) jcat(&t, "none yet (firmware decoders)\n");
    for(size_t i = 0; i < j->protocol_count; i++)
        jcat(&t, "%s%s", j->protocols[i], i + 1 < j->protocol_count ? ", " : "\n");
    jcat(&t, "\nRecent:\n");
    if(j->recent_count == 0) jcat(&t, "nothing recorded\n");
    for(size_t i = 0; i < j->recent_count; i++) {
        const RgJournalEntry* e = rg_journal_recent(j, i);
        char when[16];
        jtime(when, sizeof(when), e->time);
        jcat(&t, "%s %s\n", when, rg_journal_event_name((RgJournalEvent)e->event));
        if(e->note[0]) jcat(&t, "  %s\n", e->note);
    }
    return t.len < size ? t.len : strlen(out);
}

size_t radiogeddon_journal_achievements_text(
    const RgJournal* j,
    uint32_t fresh,
    char* out,
    size_t size) {
    JText t = {out, size, 0};
    if(size) out[0] = '\0';
    uint32_t n = 0;
    for(size_t a = 0; a < RgAchCount; a++)
        if(j->achievements & (1u << a)) n++;
    jcat(&t, "Badges: %lu of %u\n\n", (unsigned long)n, (unsigned)RgAchCount);
    for(size_t a = 0; a < RgAchCount; a++) {
        if(!(j->achievements & (1u << a))) continue;
        char when[16];
        jtime(when, sizeof(when), j->earned_time[a]);
        jcat(
            &t,
            "[x] %s%s\n    %s\n",
            rg_achievement_name((RgAchievement)a),
            (fresh & (1u << a)) ? " NEW" : "",
            when);
    }
    for(size_t a = 0; a < RgAchCount; a++) {
        if(j->achievements & (1u << a)) continue;
        jcat(
            &t,
            "[ ] %s\n    %s\n",
            rg_achievement_name((RgAchievement)a),
            rg_achievement_hint((RgAchievement)a));
    }
    uint8_t level = rg_journal_level(rg_journal_xp(j));
    jcat(&t, "\nLab decorations:\n");
    for(int d = RgDecoPlant; d < RgDecoCount; d++)
        jcat(
            &t,
            "%s %s (level %u)\n",
            rg_decoration_unlocked((RgDecoration)d, level) ? "[x]" : "[ ]",
            rg_decoration_name((RgDecoration)d),
            (unsigned)rg_decoration_level((RgDecoration)d));
    jcat(&t, "\nProgress is optional: every\ntool works at any level.\n");
    return t.len < size ? t.len : strlen(out);
}

#endif
