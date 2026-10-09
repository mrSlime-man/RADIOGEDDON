/**
 * @file rg_db.h
 * @brief Signal database index: per-file metadata, sorting, filtering, search
 *        and duplicate detection for the recordings on the SD card.
 *
 * One small fixed-size entry per .sub file (about 40 bytes) plus its name in
 * a shared pool, both supplied by the caller, so the index of a few hundred
 * recordings fits in a few kilobytes. Metadata comes from the first block of
 * each file (rg_db_parse_header); nothing else is read for the list.
 *
 * Duplicates:
 * - decoded signals with the same protocol, frequency, bit count and key are
 *   duplicates (the key is compared through a 32-bit hash of its text);
 * - RAW captures are duplicates when their size and a hash of their whole
 *   content match. The caller hashes only the RAW files that share a size
 *   with another one (rg_db_needs_content_hash).
 *
 * No SDK includes; unit-tested on the host (test/test_db.c).
 */
#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#define RG_DB_PROTO_MAX  16 /* protocol name bytes kept, including NUL */
#define RG_DB_SEARCH_MAX 32
#define RG_DB_HEAD_BYTES 512 /* bytes of each file read for its metadata */

typedef enum {
    RgDbKindCorrupt, /* unreadable, empty, or not a Sub-GHz file */
    RgDbKindUnknown, /* a Sub-GHz file without a Protocol line */
    RgDbKindRaw,
    RgDbKindProtocol,
} RgDbKind;

#define RG_DB_FLAG_DUPLICATE 0x01u /* another file holds the same signal */
#define RG_DB_FLAG_HASHED    0x02u /* hash covers the whole content (RAW) */
#define RG_DB_FLAG_HAS_KEY   0x04u /* hash covers a Key line (decoded) */

typedef struct {
    uint16_t name_off; /* NUL-terminated file name in the pool */
    uint8_t kind; /* RgDbKind */
    uint8_t flags;
    uint16_t bits; /* decoded: Bit line, 0 if absent */
    uint16_t dup_count; /* other files holding the same signal */
    char protocol[RG_DB_PROTO_MAX];
    uint32_t frequency; /* Hz, 0 if absent */
    uint32_t mtime; /* seconds since 1970 (UTC), 0 if unknown */
    uint32_t size; /* bytes */
    uint32_t hash; /* key (decoded) or content (RAW) identity */
} RgDbEntry;

typedef struct {
    RgDbEntry* entries;
    size_t count;
    size_t cap;
    char* names;
    size_t names_len;
    size_t names_cap;
} RgDb;

typedef enum {
    RgDbSortDate, /* newest first */
    RgDbSortName,
    RgDbSortFrequency,
    RgDbSortProtocol,
    RgDbSortCount,
} RgDbSort;

typedef enum {
    RgDbShowAll,
    RgDbShowRaw,
    RgDbShowDecoded,
    RgDbShowProtocol, /* only RgDbQuery.protocol */
    RgDbShowDuplicates,
    RgDbShowDamaged, /* corrupt or unknown */
    RgDbShowCount,
} RgDbShow;

typedef struct {
    RgDbSort sort;
    RgDbShow show;
    char protocol[RG_DB_PROTO_MAX];
    char search[RG_DB_SEARCH_MAX]; /* case-insensitive part of the name; "" = any */
} RgDbQuery;

/** Use caller-owned storage for up to @p cap entries and @p names_cap name bytes. */
void rg_db_init(RgDb* db, RgDbEntry* entries, size_t cap, char* names, size_t names_cap);

/**
 * Append an entry for file @p name (copied into the pool), zeroed except for
 * the name. NULL when the entry table or the name pool is full.
 */
RgDbEntry* rg_db_add(RgDb* db, const char* name);

const char* rg_db_name(const RgDb* db, const RgDbEntry* e);

/**
 * Fill kind, protocol, frequency, bits and (decoded) key hash from the first
 * @p len bytes of a file. A last line without a newline is only used when
 * @p whole_file says the block is the entire file (otherwise it may be cut).
 */
void rg_db_parse_header(RgDbEntry* e, const char* text, size_t len, bool whole_file);

/** FNV-1a, continued from @p hash (start with RG_DB_HASH_INIT). */
#define RG_DB_HASH_INIT 2166136261u
uint32_t rg_db_hash(uint32_t hash, const void* data, size_t len);

/** True for a RAW entry that shares its size with another RAW entry. */
bool rg_db_needs_content_hash(const RgDb* db, size_t index);

/** Record the whole-content hash of a RAW entry. */
void rg_db_set_content_hash(RgDbEntry* e, uint32_t hash);

/** Set RG_DB_FLAG_DUPLICATE and dup_count on every entry (call after hashing). */
void rg_db_mark_duplicates(RgDb* db);

/** True if the two entries hold the same signal (see the file comment). */
bool rg_db_same_signal(const RgDbEntry* a, const RgDbEntry* b);

/**
 * Indices of the entries matching @p q, in its sort order. Writes at most
 * @p max indices and returns how many.
 */
size_t rg_db_select(const RgDb* db, const RgDbQuery* q, uint16_t* out, size_t max);

/**
 * Distinct protocol names of decoded entries, sorted, as pointers into the
 * entries. Returns how many (at most @p max).
 */
size_t rg_db_protocols(const RgDb* db, const char** out, size_t max);

/** Short labels for the UI. */
const char* rg_db_sort_name(RgDbSort sort);
const char* rg_db_show_name(RgDbShow show);

/** Case-insensitive substring test ("" matches everything). */
bool rg_db_name_matches(const char* name, const char* search);

/** Index of the entry named @p name (exact match), or -1. */
int32_t rg_db_find(const RgDb* db, const char* name);

/* ---- File names for Rename ------------------------------------------------ */

/** Longest name a file can be renamed to, without the extension. */
#define RG_DB_NAME_MAX 60

typedef enum {
    RgDbNameOk,
    RgDbNameEmpty,
    RgDbNameTooLong,
    RgDbNameBadChar, /* < > : " / \ | ? * or a control character */
    RgDbNameBadEdge, /* starts with '.' or a space, or ends with '.' or a space */
} RgDbNameError;

/**
 * Check a new file name, given without its extension. Rejects what the FAT
 * file system on the SD card cannot store or would silently change, and names
 * starting with '.', which the Database hides.
 */
RgDbNameError rg_db_check_name(const char* name);

/** Short message for a name error, for the keyboard's error box. */
const char* rg_db_name_error_text(RgDbNameError error);

/** Remove a trailing @p ext (any case, e.g. ".sub") from @p name in place. */
void rg_db_strip_ext(char* name, const char* ext);
