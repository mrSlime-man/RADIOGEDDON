#include "radiogeddon_fp.h"

#if RG_FEATURE_FINGERPRINT

#include "radiogeddon_analysis.h"
#include "radiogeddon_storage.h"
#include "radiogeddon_safefile.h"

#define TAG "RadioGeddonFp"

/* New recordings are found a batch per folder listing (the folder is closed
 * while they are read): their names, sizes and folder positions packed into
 * a pool sized to what the heap spares, within these bounds. */
#define FP_POOL_MIN 768u
#define FP_POOL_MAX 4096u
/* Heap a search needs besides the tables and the pool: the analysis of one
 * recording (result and working state), its fingerprint builder, file
 * buffers. */
#define FP_WORK     (RADIOGEDDON_ANALYSIS_MEMORY + sizeof(RgFpBuilder) + 2048u)
/* Heap kept free beyond all of it. */
#define FP_SPARE    2048u
/* Bitmap of the folder positions handled in one update. */
#define FP_DONE_BYTES ((RG_FP_INDEX_MAX + 7u) / 8u)

/*
 * Matching recordings to the old index's records, by name. Each record of
 * the old index gets a 32-bit key: a 16-bit hash of its name above its
 * record number, sorted. A recording's name finds its candidates by binary
 * search on the hash, and each candidate's record is read back from the
 * index to compare the names exactly: a hash only narrows the search, so two
 * names never count as one (beta 10 relied on 64-bit hashes being distinct).
 * 4 bytes per record instead of 8, and the folder is listed once.
 */
typedef struct {
    uint32_t* key; /* hash16 << 16 | record number, sorted */
    size_t count;
} FpKeys;

static uint16_t fp_name_hash16(const char* name) {
    uint32_t h = rg_fp_fnv(RG_FP_FNV_INIT, name, strlen(name));
    return (uint16_t)(h ^ (h >> 16));
}

/* Heapsort (no recursion, no work memory). */
static void fp_keys_sort(uint32_t* k, size_t n) {
    for(size_t start = n / 2; start-- > 0;) {
        for(size_t root = start;;) {
            size_t child = 2u * root + 1u;
            if(child >= n) break;
            if(child + 1u < n && k[child + 1u] > k[child]) child++;
            if(k[root] >= k[child]) break;
            uint32_t t = k[root];
            k[root] = k[child];
            k[child] = t;
            root = child;
        }
    }
    for(size_t end = n; end-- > 1;) {
        uint32_t t = k[0];
        k[0] = k[end];
        k[end] = t;
        for(size_t root = 0;;) {
            size_t child = 2u * root + 1u;
            if(child >= end) break;
            if(child + 1u < end && k[child + 1u] > k[child]) child++;
            if(k[root] >= k[child]) break;
            t = k[root];
            k[root] = k[child];
            k[child] = t;
            root = child;
        }
    }
}

/* First key with hash @p h (or count when none). */
static size_t fp_keys_find(const FpKeys* keys, uint16_t h) {
    uint32_t want = (uint32_t)h << 16;
    size_t lo = 0, hi = keys->count;
    while(lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        if(keys->key[mid] < want)
            lo = mid + 1;
        else
            hi = mid;
    }
    return lo < keys->count && (keys->key[lo] >> 16) == h ? lo : keys->count;
}

static bool fp_bit(const uint8_t* bits, size_t i) {
    return (bits[i / 8u] >> (i % 8u)) & 1u;
}

static void fp_set_bit(uint8_t* bits, size_t i) {
    bits[i / 8u] |= (uint8_t)(1u << (i % 8u));
}

static bool fp_is_sub(const char* name) {
    size_t n = strlen(name);
    size_t e = strlen(RADIOGEDDON_SUB_EXTENSION);
    return n > e && strcmp(name + n - e, RADIOGEDDON_SUB_EXTENSION) == 0;
}

static uint32_t fp_size32(uint64_t size) {
    return size > UINT32_MAX ? UINT32_MAX : (uint32_t)size;
}

/* The recording's modification time (0 if unknown). */
static uint32_t fp_timestamp(Storage* storage, const char* name) {
    FuriString* path = furi_string_alloc_printf("%s/%s", RADIOGEDDON_SIGNALS_FOLDER, name);
    uint32_t ts = 0;
    if(storage_common_timestamp(storage, furi_string_get_cstr(path), &ts) != FSE_OK) ts = 0;
    furi_string_free(path);
    return ts;
}

/* A preset's name as a small number (only equality matters). */
static uint8_t fp_preset_id(const char* preset) {
    if(!preset || !preset[0]) return 0xFF;
    return (uint8_t)(rg_fp_fnv(RG_FP_FNV_INIT, preset, strlen(preset)) % 250u);
}

/* The fingerprint builder reads the samples along with the analyzer's
 * second pass (radiogeddon_analysis.h): no pass of its own. */
static void fp_tap_start(void* context, const RgAnalysis* timing) {
    rg_fp_builder_init(context, timing);
}

static void fp_tap_feed(void* context, const int32_t* samples, size_t count) {
    rg_fp_builder_feed(context, samples, count);
}

/* Fingerprint one recording; false when it cannot be read now. A RAW file
 * is read four times in all: its header, then the analyzer's three passes
 * (the distributions, preamble and sync gap ride along with the second). */
static bool fp_compute(Storage* storage, const char* name, RgFingerprint* fp) {
    FuriString* path = furi_string_alloc_printf("%s/%s", RADIOGEDDON_SIGNALS_FOLDER, name);
    RadioGeddonLoadedSignal sig;
    radiogeddon_loaded_signal_init(&sig);
    bool ok = radiogeddon_storage_load_header(storage, furi_string_get_cstr(path), &sig);
    if(ok) {
        uint8_t preset = fp_preset_id(furi_string_get_cstr(sig.preset));
        if(sig.kind != RadioGeddonSignalKindRaw) {
            uint8_t key[12];
            memcpy(key, &sig.key, 8);
            memcpy(key + 8, &sig.bit_count, 4);
            rg_fp_from_decoded(
                fp,
                furi_string_get_cstr(sig.protocol),
                (uint16_t)(sig.bit_count > UINT16_MAX ? UINT16_MAX : sig.bit_count),
                rg_fp_fnv(RG_FP_FNV_INIT, key, sizeof(key)),
                sig.frequency,
                preset);
        } else {
            RadioGeddonRawFile* file =
                radiogeddon_storage_raw_open(storage, furi_string_get_cstr(path));
            ok = file != NULL;
            if(file) {
                RadioGeddonAnalysisStatus status;
                RgFpBuilder* b = malloc(sizeof(RgFpBuilder));
                rg_fp_builder_init(b, NULL);
                RadioGeddonAnalysisTap tap = {fp_tap_start, fp_tap_feed, b};
                RgAnalysis* a = radiogeddon_analysis_run_raw(file, NULL, &tap, &status);
                if(a) {
                    rg_fp_from_analysis(fp, a, b, sig.frequency, preset);
                    free(a);
                } else if(status == RadioGeddonAnalysisNoRaw) {
                    // No samples: a fingerprint without structure.
                    RgAnalysis* empty = malloc(sizeof(RgAnalysis));
                    memset(empty, 0, sizeof(*empty));
                    rg_fp_from_analysis(fp, empty, NULL, sig.frequency, preset);
                    free(empty);
                } else {
                    ok = false; // no memory now, or unreadable: try again next time
                }
                free(b);
                radiogeddon_storage_raw_close(file);
            }
        }
    }
    radiogeddon_loaded_signal_reset(&sig);
    furi_string_free(path);
    return ok;
}

static bool fp_write_record(File* file, const RgFpRecord* rec) {
    uint8_t buf[RG_FP_RECORD_BYTES];
    if(!rg_fp_record_encode(rec, buf)) return false;
    return storage_file_write(file, buf, sizeof(buf)) == sizeof(buf);
}

static bool fp_read_record(File* file, RgFpRecord* rec, bool* end) {
    uint8_t buf[RG_FP_RECORD_BYTES];
    size_t n = storage_file_read(file, buf, sizeof(buf));
    *end = n < sizeof(buf);
    return !*end && rg_fp_record_decode(buf, rec);
}

/* Bytes of one recording in the batch pool: folder position, size, name. */
#define FP_POOL_HEAD 6u

size_t radiogeddon_fp_memory(void) {
    // A typical library's tables (100 recordings) and the smallest pool;
    // larger ones are sized to the heap when the search runs.
    return FP_WORK + 100u * RG_FP_TRACK_BYTES + FP_DONE_BYTES + FP_POOL_MIN;
}

static const RgSafeFile fp_index_file = {
    RADIOGEDDON_FP_INDEX,
    RADIOGEDDON_FP_TEMP,
    RADIOGEDDON_FP_MARK,
};

/* Old record @p index, read back from the old index; false if unreadable. */
static bool fp_old_record(File* in, uint32_t index, RgFpRecord* rec) {
    bool end;
    return storage_file_seek(in, RG_FP_HEADER_BYTES + index * RG_FP_RECORD_BYTES, true) &&
           fp_read_record(in, rec, &end);
}

/* The old record named exactly @p name, among the candidates its hash finds
 * (the lowest record number first): its slot in the key table, or -1. */
static int32_t fp_old_find(File* in, const FpKeys* keys, const char* name, RgFpRecord* rec) {
    uint16_t h = fp_name_hash16(name);
    for(size_t i = fp_keys_find(keys, h); i < keys->count && (keys->key[i] >> 16) == h; i++) {
        uint32_t index = keys->key[i] & 0xFFFFu;
        if(fp_old_record(in, index, rec) && strcmp(rec->name, name) == 0) return (int32_t)i;
    }
    return -1;
}

typedef struct {
    Storage* storage;
    RadioGeddonFpResult* out;
    RadioGeddonProgressCallback progress;
    void* context;
    File* in; /* the old index, open while records are matched */
    File* tmp; /* the new index being written */
    FpKeys keys;
    uint8_t* done; /* folder positions handled (FP_DONE_BYTES) */
    uint8_t* kept; /* key slots whose record was copied to the new index */
    size_t limit;
    uint32_t count; /* records written */
    uint32_t files; /* .sub files in the folder */
    uint32_t indexable; /* ... whose names fit in a record */
    uint32_t progress_total;
    bool ok;
} FpUpdate;

static void fp_progress(FpUpdate* u, uint32_t done) {
    if(!u->progress) return;
    uint32_t total = u->progress_total > done ? u->progress_total : done;
    u->progress(u->context, done, total ? total : 1u);
}

/* 1. The old index: a key per readable record (hash of its name, record
 * number), sorted. Damaged records are counted and left out. */
static void fp_load_keys(FpUpdate* u) {
    RadioGeddonFpResult* out = u->out;
    if(!storage_file_open(u->in, RADIOGEDDON_FP_INDEX, FSAM_READ, FSOM_OPEN_EXISTING)) return;
    uint8_t header[RG_FP_HEADER_BYTES];
    int32_t records = storage_file_read(u->in, header, sizeof(header)) == sizeof(header) ?
                          rg_fp_header_decode(header) :
                          -1;
    if(records < 0) {
        out->dropped++; // another version, or no header: rebuilt
        return;
    }
    // Sized to the records the header announces (a cut-off file holds
    // fewer), never past what this search can track.
    size_t cap = (size_t)records < u->limit ? (size_t)records : u->limit;
    if(cap == 0) return;
    u->keys.key = malloc(cap * sizeof(uint32_t));
    u->kept = malloc((cap + 7u) / 8u);
    memset(u->kept, 0, (cap + 7u) / 8u);
    RgFpRecord* rec = malloc(sizeof(RgFpRecord));
    bool end = false;
    for(uint32_t i = 0; !end && i < RG_FP_INDEX_MAX; i++) {
        if(!fp_read_record(u->in, rec, &end)) {
            if(!end) out->dropped++; // damaged: skipped, the next stays aligned
            continue;
        }
        if(u->keys.count >= cap) {
            out->dropped++; // past what this search can track
            continue;
        }
        u->keys.key[u->keys.count++] = ((uint32_t)fp_name_hash16(rec->name) << 16) | i;
    }
    free(rec);
    fp_keys_sort(u->keys.key, u->keys.count);
}

/* 2. One listing of the folder: every recording whose record is still
 * current (same name, size and time) is copied to the new index. Sizes come
 * from the listing; only a recording whose size still matches is asked for
 * its time. */
static void fp_keep_current(FpUpdate* u) {
    File* dir = storage_file_alloc(u->storage);
    FileInfo info;
    char name[RG_FP_NAME_MAX + 8];
    RgFpRecord* rec = malloc(sizeof(RgFpRecord));
    if(storage_dir_open(dir, RADIOGEDDON_SIGNALS_FOLDER)) {
        while(u->ok && storage_dir_read(dir, &info, name, sizeof(name))) {
            if((info.flags & FSF_DIRECTORY) || !fp_is_sub(name)) continue;
            u->files++;
            if(strlen(name) >= RG_FP_NAME_MAX) continue;
            uint32_t pos = u->indexable++;
            if(pos >= u->limit || u->keys.count == 0) continue;
            int32_t kept_key = fp_old_find(u->in, &u->keys, name, rec);
            if(kept_key < 0) continue; // new: fingerprinted below
            uint32_t size = fp_size32(info.size);
            if(rec->size != size ||
               !rg_fp_record_current(rec, size, fp_timestamp(u->storage, name))) {
                continue; // changed: fingerprinted again below
            }
            u->ok = fp_write_record(u->tmp, rec);
            fp_set_bit(u->done, pos);
            fp_set_bit(u->kept, (size_t)kept_key);
            u->count++;
            fp_progress(u, u->count);
        }
    }
    storage_dir_close(dir);
    storage_file_free(dir);
    free(rec);
    // Records not kept: gone, renamed, changed or duplicates.
    u->out->dropped += (uint32_t)u->keys.count - u->count;
}

/* True when old record @p name was already kept (the folder changed between
 * listings and moved it): never written twice. */
static bool fp_was_kept(FpUpdate* u, const char* name, RgFpRecord* rec) {
    uint16_t h = fp_name_hash16(name);
    for(size_t i = fp_keys_find(&u->keys, h); i < u->keys.count && (u->keys.key[i] >> 16) == h;
        i++) {
        uint32_t index = u->keys.key[i] & 0xFFFFu;
        if(fp_bit(u->kept, i) && fp_old_record(u->in, index, rec) &&
           strcmp(rec->name, name) == 0)
            return true;
    }
    return false;
}

/* 3. Recordings without a current record, a pool of names per listing (the
 * folder is closed while they are fingerprinted), by folder position. */
static void fp_add_new(FpUpdate* u) {
    if(!u->ok || u->count >= u->limit) return;
    size_t want = u->indexable < u->limit ? u->indexable : u->limit;
    if(u->count >= want) return;
    size_t avail = memmgr_heap_get_max_free_block();
    size_t pool_size = avail > FP_WORK + FP_SPARE ? (avail - FP_WORK - FP_SPARE) / 2u : 0;
    if(pool_size > FP_POOL_MAX) pool_size = FP_POOL_MAX;
    if(pool_size < FP_POOL_MIN) pool_size = FP_POOL_MIN;
    // No more than the missing recordings can fill.
    size_t missing = (want - u->count) * (FP_POOL_HEAD + RG_FP_NAME_MAX);
    if(pool_size > missing) pool_size = missing;
    uint8_t* pool = malloc(pool_size);
    RgFpRecord* rec = malloc(sizeof(RgFpRecord));
    File* dir = storage_file_alloc(u->storage);
    FileInfo info;
    char name[RG_FP_NAME_MAX + 8];
    bool more = true;
    while(more && u->ok && u->count < u->limit) {
        size_t used = 0;
        more = false;
        if(storage_dir_open(dir, RADIOGEDDON_SIGNALS_FOLDER)) {
            uint32_t pos = 0;
            while(storage_dir_read(dir, &info, name, sizeof(name))) {
                if((info.flags & FSF_DIRECTORY) || !fp_is_sub(name)) continue;
                size_t len = strlen(name);
                if(len >= RG_FP_NAME_MAX) continue;
                uint32_t p = pos++;
                if(p >= u->limit || fp_bit(u->done, p)) continue;
                if(used + FP_POOL_HEAD + len + 1u > pool_size) {
                    more = true; // the pool is full: the next listing goes on
                    break;
                }
                uint16_t p16 = (uint16_t)p;
                uint32_t size = fp_size32(info.size);
                memcpy(pool + used, &p16, 2);
                memcpy(pool + used + 2u, &size, 4);
                memcpy(pool + used + FP_POOL_HEAD, name, len + 1u);
                used += FP_POOL_HEAD + len + 1u;
            }
        }
        storage_dir_close(dir);
        for(size_t at = 0; at < used && u->ok && u->count < u->limit;) {
            uint16_t p16;
            uint32_t size;
            memcpy(&p16, pool + at, 2);
            memcpy(&size, pool + at + 2u, 4);
            const char* entry = (const char*)pool + at + FP_POOL_HEAD;
            at += FP_POOL_HEAD + strlen(entry) + 1u;
            fp_set_bit(u->done, p16); // never retried in this search
            if(fp_was_kept(u, entry, rec)) continue;
            memset(rec, 0, sizeof(*rec));
            strlcpy(rec->name, entry, sizeof(rec->name));
            rec->size = size;
            rec->timestamp = fp_timestamp(u->storage, entry);
            if(fp_compute(u->storage, rec->name, &rec->fp)) {
                u->ok = fp_write_record(u->tmp, rec);
                u->count++;
                u->out->computed++;
            } else {
                u->out->unreadable++;
            }
            fp_progress(u, u->count + u->out->unreadable);
        }
    }
    storage_file_free(dir);
    free(rec);
    free(pool);
}

/* Rewrite the index: keep current records, add the missing recordings.
 * The old index is read once for its keys; one listing keeps the current
 * records; new recordings are found by listing the folder again only if
 * some are missing, a pool of names per listing. */
static bool fp_update(
    Storage* storage,
    RadioGeddonFpResult* out,
    RadioGeddonProgressCallback progress,
    void* context) {
    FpUpdate u = {
        .storage = storage,
        .out = out,
        .progress = progress,
        .context = context,
        .ok = true,
    };
    // Recordings the heap can track now (RG_FP_TRACK_BYTES each), up to
    // RG_FP_INDEX_MAX; past either, the first ones in the folder are
    // indexed.
    size_t avail = memmgr_heap_get_max_free_block();
    size_t fixed = FP_WORK + FP_SPARE + 2u * FP_DONE_BYTES + FP_POOL_MIN;
    u.limit = avail > fixed ? (avail - fixed) / sizeof(uint32_t) : 0;
    if(u.limit > RG_FP_INDEX_MAX) u.limit = RG_FP_INDEX_MAX;
    u.done = malloc(FP_DONE_BYTES);
    memset(u.done, 0, FP_DONE_BYTES);
    u.in = storage_file_alloc(storage);
    u.tmp = storage_file_alloc(storage);

    // Finishes an update a reboot cut off first, so the old index is read
    // whole (radiogeddon_safefile.h): its records are hours of analysis.
    u.ok = radiogeddon_safefile_begin(storage, &fp_index_file) &&
           storage_file_open(u.tmp, RADIOGEDDON_FP_TEMP, FSAM_WRITE, FSOM_CREATE_ALWAYS);
    uint8_t header[RG_FP_HEADER_BYTES];
    rg_fp_header_encode(0, header);
    u.ok = u.ok && storage_file_write(u.tmp, header, sizeof(header)) == sizeof(header);
    if(u.ok) {
        fp_load_keys(&u);
        u.progress_total = (uint32_t)u.keys.count;
        fp_keep_current(&u);
        u.progress_total = u.files;
        fp_add_new(&u);
    }
    storage_file_close(u.in);
    storage_file_free(u.in);
    size_t want = u.indexable < u.limit ? u.indexable : u.limit;
    out->truncated = u.files > want;
    out->limit = (uint32_t)want;
    free(u.kept);
    free(u.done);
    free(u.keys.key);

    // The real count in the header, then swap the files.
    if(u.ok) {
        rg_fp_header_encode(u.count, header);
        u.ok = storage_file_seek(u.tmp, 0, true) &&
               storage_file_write(u.tmp, header, sizeof(header)) == sizeof(header);
    }
    u.ok = storage_file_close(u.tmp) && u.ok;
    storage_file_free(u.tmp);
    if(u.ok) {
        u.ok = radiogeddon_safefile_commit(storage, &fp_index_file);
    } else {
        storage_common_remove(storage, RADIOGEDDON_FP_TEMP);
    }
    if(!u.ok) FURI_LOG_W(TAG, "index not written");
    out->indexed = u.count;
    return u.ok;
}

bool radiogeddon_fp_find_similar(
    Storage* storage,
    const char* target,
    uint32_t window_s,
    const RgSession* session,
    RadioGeddonFpResult* out,
    RadioGeddonProgressCallback progress,
    void* context) {
    memset(out, 0, sizeof(*out));
    if(memmgr_heap_get_max_free_block() < radiogeddon_fp_memory()) return false;
    const char* slash = strrchr(target, '/');
    const char* tname = slash ? slash + 1 : target;

    out->index_failed = !fp_update(storage, out, progress, context);

    RgFpRecord* rec = malloc(sizeof(RgFpRecord));
    RgFingerprint* tfp = malloc(sizeof(RgFingerprint));
    File* file = storage_file_alloc(storage);
    bool found = false;
    uint32_t target_ts = 0;
    // The target's own fingerprint: from the index, or computed now.
    if(!out->index_failed &&
       storage_file_open(file, RADIOGEDDON_FP_INDEX, FSAM_READ, FSOM_OPEN_EXISTING)) {
        uint8_t header[RG_FP_HEADER_BYTES];
        bool end = storage_file_read(file, header, sizeof(header)) != sizeof(header);
        while(!end && !found) {
            if(fp_read_record(file, rec, &end) && strcmp(rec->name, tname) == 0) {
                *tfp = rec->fp;
                target_ts = rec->timestamp;
                found = true;
            }
        }
    }
    storage_file_close(file);
    if(!found) {
        found = fp_compute(storage, tname, tfp);
        if(found) target_ts = fp_timestamp(storage, tname);
    }
    out->target_found = found;
    if(found) {
        out->target_structure = tfp->flags & RG_FP_FLAG_STRUCTURE;
        out->target_raw = tfp->flags & RG_FP_FLAG_RAW;
        out->target_bits = tfp->bit_count;
        out->target_te = tfp->te_us;
        out->target_encoding = tfp->encoding;
    }

    // Rank every other recording against it.
    if(found && !out->index_failed &&
       storage_file_open(file, RADIOGEDDON_FP_INDEX, FSAM_READ, FSOM_OPEN_EXISTING)) {
        RgFpRank rank;
        rg_fp_rank_init(&rank, RADIOGEDDON_FP_MIN_SCORE);
        uint8_t header[RG_FP_HEADER_BYTES];
        bool end = storage_file_read(file, header, sizeof(header)) != sizeof(header);
        uint32_t id = 0;
        while(!end) {
            uint32_t this_id = id++;
            if(!fp_read_record(file, rec, &end) || strcmp(rec->name, tname) == 0) continue;
            if(session && rg_session_find(session, rec->name) < 0) {
                out->out_of_scope++;
                continue;
            }
            if(window_s) {
                // In scope only with both times known and close enough.
                uint32_t d = rec->timestamp > target_ts ? rec->timestamp - target_ts :
                                                          target_ts - rec->timestamp;
                if(!rec->timestamp || !target_ts || d > window_s) {
                    out->out_of_scope++;
                    continue;
                }
            }
            RgFpMatch m;
            rg_fp_compare(tfp, &rec->fp, &m);
            rg_fp_rank_offer(&rank, this_id, &m);
            out->compared++;
        }
        // Names of the kept ones (read back by position).
        for(size_t i = 0; i < rank.count; i++) {
            uint32_t off = RG_FP_HEADER_BYTES + rank.c[i].id * RG_FP_RECORD_BYTES;
            if(!storage_file_seek(file, off, true) || !fp_read_record(file, rec, &end)) continue;
            RadioGeddonFpHit* h = &out->hit[out->count++];
            strlcpy(h->name, rec->name, sizeof(h->name));
            h->match = rank.c[i].match;
        }
    }
    storage_file_close(file);
    storage_file_free(file);
    free(tfp);
    free(rec);
    return true;
}

size_t radiogeddon_fp_explain(const RadioGeddonFpResult* r, size_t index, char* out, size_t size) {
    if(index >= r->count || size == 0) return 0;
    const RadioGeddonFpHit* h = &r->hit[index];
    char agree[96], against[96];
    rg_fp_evidence_text(h->match.evidence, 6, agree, sizeof(agree));
    rg_fp_evidence_text(h->match.contradicts, 4, against, sizeof(against));
    int n = snprintf(
        out,
        size,
        "%s\n%u%% %s [HEURISTIC]\n\nMatched: %s\n%s%s%s\nSimilar structure is\nnot proof of the same\ntransmitter or device.",
        h->name,
        (unsigned)h->match.score,
        rg_fp_strength(h->match.score),
        agree[0] ? agree : "nothing strongly",
        against[0] ? "Differs: " : "",
        against,
        against[0] ? "\n" : "");
    if(n < 0) return 0;
    return (size_t)n < size ? (size_t)n : size - 1;
}

#endif
