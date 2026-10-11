/**
 * @file rg_journal.h
 * @brief Research Journal, Research XP and achievements (Full edition).
 *
 * The journal counts real research events as the app reports them (a
 * recording saved, an analysis finished, a fingerprint search run, a
 * comparison, a session created, a report exported, an activity log
 * completed, a tool opened on a recording, a protocol a firmware decoder
 * identified) and keeps the most recent ones with a short note. Nothing is
 * inferred or invented: a figure on the Journal screen is a count of these
 * events, and the screen never scans the SD card to make one up.
 *
 * Research XP and achievements are derived from the counters, never stored
 * on their own, so they cannot disagree with them. XP rewards research
 * breadth with per-event caps (a hundred recordings earn no more than
 * twenty), and nothing is earned by transmitting. Progress is optional: no
 * function of the app depends on a level.
 *
 * Persisted as one small binary file (magic, version, payload, CRC-32). A
 * damaged or foreign file is never half-read: the journal starts empty and
 * says it recovered. Clearing the history keeps the counters (and so the
 * XP); resetting clears everything. Recordings are never touched.
 *
 * Pure C with no SDK headers; host-tested in test/test_journal.c and fuzzed
 * (test/fuzz/fuzz_journal.c).
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/** Journal events. Stored by number: append, never reorder. */
typedef enum {
    RgJrnCapture, // a recording was saved (RAW or decoded)
    RgJrnAnalysis, // an analysis report finished
    RgJrnFingerprint, // fingerprints computed (arg: how many)
    RgJrnSimilar, // a Find Similar Signals search finished
    RgJrnCompare, // two recordings compared
    RgJrnMulti, // a multi-capture comparison finished
    RgJrnSession, // a research session created
    RgJrnReport, // a report exported to the SD card
    RgJrnActlog, // an activity log completed
    RgJrnStudio, // Pulse Studio opened on a recording
    RgJrnProtocol, // a firmware decoder identified a protocol (note: its name)
    RgJrnScan, // a scan (Scanner, Adaptive, Range) ran
    RgJrnDiscovery, // Smart Protocol Discovery finished
    RgJrnEventCount,
} RgJournalEvent;

/** Achievements. Stored by number: append, never reorder. */
typedef enum {
    RgAchFirstCapture,
    RgAchFirstAnalysis,
    RgAchFingerprinter, // first similarity search
    RgAchComparer, // first multi-capture comparison
    RgAchSessionStarter,
    RgAchReporter, // first exported report
    RgAchNightWatch, // first completed activity log
    RgAchExplorer, // five different research tools used
    RgAchPulseReader, // first Pulse Studio session
    RgAchProtocolSpotter, // three different protocols identified
    RgAchLabRegular, // 25 recordings saved
    RgAchPolymath, // every research tool used
    RgAchCount,
} RgAchievement;

#define RG_JOURNAL_RECENT    12
#define RG_JOURNAL_NOTE      24
#define RG_JOURNAL_PROTOCOLS 8
#define RG_JOURNAL_PROTO_LEN 16
#define RG_JOURNAL_MAX_LEVEL 10

typedef struct {
    uint32_t time; // seconds (the caller's clock: RTC epoch on the device)
    uint8_t event; // RgJournalEvent
    char note[RG_JOURNAL_NOTE]; // a file, session or protocol name (may be empty)
} RgJournalEntry;

typedef struct {
    uint32_t counters[RgJrnEventCount];
    uint32_t achievements; // bit per RgAchievement, as earned
    uint32_t earned_time[RgAchCount]; // when each was earned (0: not yet)
    char protocols[RG_JOURNAL_PROTOCOLS][RG_JOURNAL_PROTO_LEN]; // distinct, first seen
    uint8_t protocol_count;
    uint8_t recent_count;
    uint8_t recent_head; // next slot
    RgJournalEntry recent[RG_JOURNAL_RECENT];
    bool recovered; // the file was damaged and the journal started over
} RgJournal;

void rg_journal_init(RgJournal* j);

/**
 * Record @p event at @p time with @p note (NULL: none) and @p count (how
 * many, usually 1). Returns the achievements this earned (bits), so the app
 * can celebrate; 0 when none.
 */
uint32_t rg_journal_record(
    RgJournal* j,
    RgJournalEvent event,
    uint32_t count,
    const char* note,
    uint32_t time);

/** Forget the recent list (counters, XP and achievements stay). */
void rg_journal_clear_history(RgJournal* j);

/** Research XP from the counters and achievements. */
uint32_t rg_journal_xp(const RgJournal* j);
/** Level 1..RG_JOURNAL_MAX_LEVEL for @p xp, and the XP the next one needs. */
uint8_t rg_journal_level(uint32_t xp);
uint32_t rg_journal_level_xp(uint8_t level);
/** Distinct research tools used (bits of RgJournalEvent). */
uint8_t rg_journal_tools_used(const RgJournal* j);

/** Achievements that hold for the counters now (bits). */
uint32_t rg_journal_achievements_due(const RgJournal* j);

const char* rg_journal_event_name(RgJournalEvent e);
const char* rg_achievement_name(RgAchievement a);
const char* rg_achievement_hint(RgAchievement a);

/** The i-th most recent entry (0 = newest), NULL past the end. */
const RgJournalEntry* rg_journal_recent(const RgJournal* j, size_t i);

/* ---- decorations unlocked by level (Dolphin Corner) --------------------- */

/** Lab decorations. Stored by number in settings: append only. */
typedef enum {
    RgDecoNone,
    RgDecoPlant, // level 2
    RgDecoPoster, // level 3
    RgDecoLamp, // level 4
    RgDecoMug, // level 5
    RgDecoTrophy, // level 6
    RgDecoCount,
} RgDecoration;

uint8_t rg_decoration_level(RgDecoration d);
const char* rg_decoration_name(RgDecoration d);
bool rg_decoration_unlocked(RgDecoration d, uint8_t level);

/* ---- persistence -------------------------------------------------------- */

#define RG_JOURNAL_MAGIC   "RGJ1"
#define RG_JOURNAL_VERSION 1u

/** Bytes rg_journal_encode writes. */
size_t rg_journal_encoded_size(void);
/** Serialize into @p out (rg_journal_encoded_size() bytes). */
size_t rg_journal_encode(const RgJournal* j, uint8_t* out, size_t size);
/**
 * Read a serialized journal. False (and @p j left empty with
 * j->recovered set) when the bytes are damaged, short or of another
 * version.
 */
bool rg_journal_decode(RgJournal* j, const uint8_t* in, size_t size);
