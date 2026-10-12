#include "rg_journal.h"
#include "../radiogeddon_edition.h"

#if RG_FEATURE_JOURNAL

#include "rg_crc.h"
#include <string.h>

void rg_journal_init(RgJournal* j) {
    memset(j, 0, sizeof(*j));
}

static void rg_journal_copy(char* dst, size_t size, const char* src) {
    size_t n = 0;
    if(src) {
        while(src[n] && n + 1 < size) {
            unsigned char c = (unsigned char)src[n];
            dst[n] = (c < 0x20 || c > 0x7E) ? '?' : (char)c;
            n++;
        }
    }
    dst[n] = '\0';
    while(++n < size)
        dst[n] = '\0';
}

/* Research tools (events that count towards Explorer and Polymath). */
#define RG_JOURNAL_TOOLS                                                                        \
    ((1u << RgJrnAnalysis) | (1u << RgJrnSimilar) | (1u << RgJrnCompare) | (1u << RgJrnMulti) | \
     (1u << RgJrnSession) | (1u << RgJrnReport) | (1u << RgJrnActlog) | (1u << RgJrnStudio) |   \
     (1u << RgJrnDiscovery))

uint8_t rg_journal_tools_used(const RgJournal* j) {
    uint8_t n = 0;
    for(size_t e = 0; e < RgJrnEventCount; e++)
        if((RG_JOURNAL_TOOLS & (1u << e)) && j->counters[e]) n++;
    return n;
}

static uint8_t rg_journal_tool_count(void) {
    uint8_t n = 0;
    for(size_t e = 0; e < RgJrnEventCount; e++)
        if(RG_JOURNAL_TOOLS & (1u << e)) n++;
    return n;
}

uint32_t rg_journal_achievements_due(const RgJournal* j) {
    const uint32_t* c = j->counters;
    uint32_t a = 0;
    if(c[RgJrnCapture]) a |= 1u << RgAchFirstCapture;
    if(c[RgJrnAnalysis] || c[RgJrnDiscovery]) a |= 1u << RgAchFirstAnalysis;
    if(c[RgJrnSimilar]) a |= 1u << RgAchFingerprinter;
    if(c[RgJrnMulti]) a |= 1u << RgAchComparer;
    if(c[RgJrnSession]) a |= 1u << RgAchSessionStarter;
    if(c[RgJrnReport]) a |= 1u << RgAchReporter;
    if(c[RgJrnActlog]) a |= 1u << RgAchNightWatch;
    if(rg_journal_tools_used(j) >= 5) a |= 1u << RgAchExplorer;
    if(c[RgJrnStudio]) a |= 1u << RgAchPulseReader;
    if(j->protocol_count >= 3) a |= 1u << RgAchProtocolSpotter;
    if(c[RgJrnCapture] >= 25) a |= 1u << RgAchLabRegular;
    if(rg_journal_tools_used(j) == rg_journal_tool_count()) a |= 1u << RgAchPolymath;
    return a;
}

uint32_t rg_journal_record(
    RgJournal* j,
    RgJournalEvent event,
    uint32_t count,
    const char* note,
    uint32_t time) {
    if((unsigned)event >= RgJrnEventCount) return 0;
    if(count == 0) count = 1;
    uint32_t* c = &j->counters[event];
    *c = (UINT32_MAX - *c < count) ? UINT32_MAX : *c + count;
    if(event == RgJrnProtocol && note && note[0]) {
        bool known = false;
        char name[RG_JOURNAL_PROTO_LEN];
        rg_journal_copy(name, sizeof(name), note);
        for(size_t i = 0; i < j->protocol_count; i++)
            if(strcmp(j->protocols[i], name) == 0) known = true;
        if(!known && j->protocol_count < RG_JOURNAL_PROTOCOLS) {
            memcpy(j->protocols[j->protocol_count], name, sizeof(name));
            j->protocol_count++;
        }
    }
    // Scans are counted but not listed: they would push research off the list.
    if(event != RgJrnScan) {
        RgJournalEntry* e = &j->recent[j->recent_head];
        e->time = time;
        e->event = (uint8_t)event;
        rg_journal_copy(e->note, sizeof(e->note), note);
        j->recent_head = (uint8_t)((j->recent_head + 1u) % RG_JOURNAL_RECENT);
        if(j->recent_count < RG_JOURNAL_RECENT) j->recent_count++;
    }
    uint32_t due = rg_journal_achievements_due(j);
    uint32_t fresh = due & ~j->achievements;
    for(size_t a = 0; a < RgAchCount; a++)
        if(fresh & (1u << a)) j->earned_time[a] = time ? time : 1u;
    j->achievements |= fresh;
    return fresh;
}

void rg_journal_clear_history(RgJournal* j) {
    memset(j->recent, 0, sizeof(j->recent));
    j->recent_count = 0;
    j->recent_head = 0;
}

const RgJournalEntry* rg_journal_recent(const RgJournal* j, size_t i) {
    if(i >= j->recent_count) return NULL;
    size_t slot = (j->recent_head + RG_JOURNAL_RECENT - 1u - i) % RG_JOURNAL_RECENT;
    return &j->recent[slot];
}

/* XP per event and the most events that earn it. */
static const struct {
    uint8_t xp;
    uint16_t cap;
} rg_journal_xp_table[RgJrnEventCount] = {
    [RgJrnCapture] = {5, 20},
    [RgJrnAnalysis] = {10, 30},
    [RgJrnFingerprint] = {1, 100},
    [RgJrnSimilar] = {8, 20},
    [RgJrnCompare] = {10, 20},
    [RgJrnMulti] = {15, 15},
    [RgJrnSession] = {20, 10},
    [RgJrnReport] = {15, 15},
    [RgJrnActlog] = {20, 10},
    [RgJrnStudio] = {8, 20},
    [RgJrnProtocol] = {0, 0}, // distinct protocols count instead (below)
    [RgJrnScan] = {1, 30},
    [RgJrnDiscovery] = {10, 20},
};

#define RG_JOURNAL_ACH_XP   25u
#define RG_JOURNAL_PROTO_XP 10u

uint32_t rg_journal_xp(const RgJournal* j) {
    uint32_t xp = 0;
    for(size_t e = 0; e < RgJrnEventCount; e++) {
        uint32_t n = j->counters[e];
        if(n > rg_journal_xp_table[e].cap) n = rg_journal_xp_table[e].cap;
        xp += n * rg_journal_xp_table[e].xp;
    }
    xp += (uint32_t)j->protocol_count * RG_JOURNAL_PROTO_XP;
    for(size_t a = 0; a < RgAchCount; a++)
        if(j->achievements & (1u << a)) xp += RG_JOURNAL_ACH_XP;
    return xp;
}

uint32_t rg_journal_level_xp(uint8_t level) {
    // 0, 40, 120, 240, 400, 600, ...: 20 * n * (n - 1) for level n.
    if(level <= 1) return 0;
    if(level > RG_JOURNAL_MAX_LEVEL) level = RG_JOURNAL_MAX_LEVEL;
    return 20u * level * (level - 1u);
}

uint8_t rg_journal_level(uint32_t xp) {
    uint8_t level = 1;
    while(level < RG_JOURNAL_MAX_LEVEL && xp >= rg_journal_level_xp((uint8_t)(level + 1u)))
        level++;
    return level;
}

static const char* const rg_journal_event_names[RgJrnEventCount] = {
    "Recording saved",
    "Analysis",
    "Fingerprints",
    "Similar search",
    "Comparison",
    "Multi-compare",
    "Session created",
    "Report exported",
    "Activity log",
    "Pulse Studio",
    "Protocol identified",
    "Scan",
    "Protocol Discovery",
};

const char* rg_journal_event_name(RgJournalEvent e) {
    return (unsigned)e < RgJrnEventCount ? rg_journal_event_names[e] : "?";
}

static const char* const rg_achievement_names[RgAchCount] = {
    "First Capture",
    "First Analysis",
    "Fingerprinter",
    "Comparer",
    "Session Starter",
    "Reporter",
    "Night Watch",
    "Explorer",
    "Pulse Reader",
    "Protocol Spotter",
    "Lab Regular",
    "Polymath",
};

static const char* const rg_achievement_hints[RgAchCount] = {
    "Save a recording",
    "Analyse a recording",
    "Find similar signals",
    "Compare several captures",
    "Create a research session",
    "Export a report",
    "Complete an activity log",
    "Use 5 research tools",
    "Open Pulse Studio",
    "Identify 3 protocols",
    "Save 25 recordings",
    "Use every research tool",
};

const char* rg_achievement_name(RgAchievement a) {
    return (unsigned)a < RgAchCount ? rg_achievement_names[a] : "?";
}

const char* rg_achievement_hint(RgAchievement a) {
    return (unsigned)a < RgAchCount ? rg_achievement_hints[a] : "?";
}

/* ---- decorations ----------------------------------------------------------- */

static const char* const rg_deco_names[RgDecoCount] = {
    "None",
    "Plant",
    "Poster",
    "Desk lamp",
    "Coffee mug",
    "Trophy",
};

uint8_t rg_decoration_level(RgDecoration d) {
    return (unsigned)d < RgDecoCount ? (uint8_t)(d + 1u) : 255u;
}

const char* rg_decoration_name(RgDecoration d) {
    return (unsigned)d < RgDecoCount ? rg_deco_names[d] : "?";
}

bool rg_decoration_unlocked(RgDecoration d, uint8_t level) {
    if(d == RgDecoNone) return true;
    return (unsigned)d < RgDecoCount && level >= rg_decoration_level(d);
}

/* ---- persistence ----------------------------------------------------------- */

/* Layout: magic(4) version(2) events(1) achievements-count(1),
 * counters(4 * events), achievements(4), earned(4 * ach), protocols(count 1 +
 * 8 * 16), recent(count 1, head 1, 12 * (4 + 1 + 24)), crc(4). */
#define RG_JOURNAL_ENTRY_BYTES (4u + 1u + RG_JOURNAL_NOTE)

size_t rg_journal_encoded_size(void) {
    return 8u + 4u * RgJrnEventCount + 4u + 4u * RgAchCount + 1u +
           RG_JOURNAL_PROTOCOLS * RG_JOURNAL_PROTO_LEN + 2u +
           RG_JOURNAL_RECENT * RG_JOURNAL_ENTRY_BYTES + 4u;
}

static uint8_t* rg_j_put32(uint8_t* p, uint32_t v) {
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
    return p + 4;
}

static const uint8_t* rg_j_get32(const uint8_t* p, uint32_t* v) {
    *v = (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
    return p + 4;
}

size_t rg_journal_encode(const RgJournal* j, uint8_t* out, size_t size) {
    size_t total = rg_journal_encoded_size();
    if(size < total) return 0;
    memset(out, 0, total);
    uint8_t* p = out;
    memcpy(p, RG_JOURNAL_MAGIC, 4);
    p += 4;
    *p++ = (uint8_t)RG_JOURNAL_VERSION;
    *p++ = (uint8_t)(RG_JOURNAL_VERSION >> 8);
    *p++ = RgJrnEventCount;
    *p++ = RgAchCount;
    for(size_t e = 0; e < RgJrnEventCount; e++)
        p = rg_j_put32(p, j->counters[e]);
    p = rg_j_put32(p, j->achievements);
    for(size_t a = 0; a < RgAchCount; a++)
        p = rg_j_put32(p, j->earned_time[a]);
    *p++ = j->protocol_count;
    memcpy(p, j->protocols, sizeof(j->protocols));
    p += sizeof(j->protocols);
    *p++ = j->recent_count;
    *p++ = j->recent_head;
    for(size_t i = 0; i < RG_JOURNAL_RECENT; i++) {
        p = rg_j_put32(p, j->recent[i].time);
        *p++ = j->recent[i].event;
        memcpy(p, j->recent[i].note, RG_JOURNAL_NOTE);
        p += RG_JOURNAL_NOTE;
    }
    rg_j_put32(p, rg_crc32(0, out, (size_t)(p - out)));
    return total;
}

static bool rg_journal_text_ok(const char* s, size_t size) {
    size_t n = 0;
    while(n < size && s[n]) {
        unsigned char c = (unsigned char)s[n];
        if(c < 0x20 || c > 0x7E) return false;
        n++;
    }
    if(n == size) return false; // not terminated
    while(n < size)
        if(s[n++]) return false;
    return true;
}

bool rg_journal_decode(RgJournal* j, const uint8_t* in, size_t size) {
    rg_journal_init(j);
    size_t total = rg_journal_encoded_size();
    bool ok = false;
    do {
        if(!in || size != total) break;
        if(memcmp(in, RG_JOURNAL_MAGIC, 4) != 0) break;
        if((in[4] | (in[5] << 8)) != RG_JOURNAL_VERSION) break;
        if(in[6] != RgJrnEventCount || in[7] != RgAchCount) break;
        uint32_t crc;
        rg_j_get32(in + total - 4, &crc);
        if(crc != rg_crc32(0, in, total - 4)) break;
        RgJournal t;
        rg_journal_init(&t);
        const uint8_t* p = in + 8;
        for(size_t e = 0; e < RgJrnEventCount; e++)
            p = rg_j_get32(p, &t.counters[e]);
        p = rg_j_get32(p, &t.achievements);
        for(size_t a = 0; a < RgAchCount; a++)
            p = rg_j_get32(p, &t.earned_time[a]);
        t.protocol_count = *p++;
        memcpy(t.protocols, p, sizeof(t.protocols));
        p += sizeof(t.protocols);
        t.recent_count = *p++;
        t.recent_head = *p++;
        for(size_t i = 0; i < RG_JOURNAL_RECENT; i++) {
            p = rg_j_get32(p, &t.recent[i].time);
            t.recent[i].event = *p++;
            memcpy(t.recent[i].note, p, RG_JOURNAL_NOTE);
            p += RG_JOURNAL_NOTE;
        }
        // Values the code could never have written.
        if(t.achievements >> RgAchCount) break;
        if(t.protocol_count > RG_JOURNAL_PROTOCOLS) break;
        if(t.recent_count > RG_JOURNAL_RECENT || t.recent_head >= RG_JOURNAL_RECENT) break;
        bool sane = true;
        for(size_t i = 0; i < RG_JOURNAL_PROTOCOLS && sane; i++)
            sane = rg_journal_text_ok(t.protocols[i], RG_JOURNAL_PROTO_LEN) &&
                   (i < t.protocol_count || t.protocols[i][0] == '\0');
        for(size_t i = 0; i < RG_JOURNAL_RECENT && sane; i++)
            sane = rg_journal_text_ok(t.recent[i].note, RG_JOURNAL_NOTE) &&
                   t.recent[i].event < RgJrnEventCount;
        if(!sane) break;
        *j = t;
        ok = true;
    } while(false);
    if(!ok) j->recovered = true;
    return ok;
}

#endif /* RG_FEATURE_JOURNAL */
