#include "radiogeddon_fp.h"

#if RG_FEATURE_FINGERPRINT

#include "radiogeddon_analysis.h"
#include "radiogeddon_storage.h"

#define TAG "RadioGeddonFp"

/* Recordings fingerprinted per folder listing (the folder is closed while
 * they are read). */
#define FP_BATCH 12u

static uint32_t fp_name_hash(const char* name) {
    return rg_fp_fnv(RG_FP_FNV_INIT, name, strlen(name));
}

typedef struct {
    uint32_t* hash;
    size_t count;
} FpSeen;

static bool fp_seen(const FpSeen* s, uint32_t h) {
    for(size_t i = 0; i < s->count; i++)
        if(s->hash[i] == h) return true;
    return false;
}

static void fp_see(FpSeen* s, uint32_t h) {
    if(s->count < RG_FP_INDEX_MAX && !fp_seen(s, h)) s->hash[s->count++] = h;
}

static bool fp_is_sub(const char* name) {
    size_t n = strlen(name);
    size_t e = strlen(RADIOGEDDON_SUB_EXTENSION);
    return n > e && strcmp(name + n - e, RADIOGEDDON_SUB_EXTENSION) == 0;
}

/* The recording's size and modification time now; false when it is gone. */
static bool fp_stat(Storage* storage, const char* name, uint32_t* size, uint32_t* ts) {
    FuriString* path = furi_string_alloc_printf("%s/%s", RADIOGEDDON_SIGNALS_FOLDER, name);
    FileInfo info;
    bool ok = storage_common_stat(storage, furi_string_get_cstr(path), &info) == FSE_OK &&
              !(info.flags & FSF_DIRECTORY);
    *size = ok ? (uint32_t)(info.size > UINT32_MAX ? UINT32_MAX : info.size) : 0;
    *ts = 0;
    if(ok && storage_common_timestamp(storage, furi_string_get_cstr(path), ts) != FSE_OK) *ts = 0;
    furi_string_free(path);
    return ok;
}

/* A preset's name as a small number (only equality matters). */
static uint8_t fp_preset_id(const char* preset) {
    if(!preset || !preset[0]) return 0xFF;
    return (uint8_t)(rg_fp_fnv(RG_FP_FNV_INIT, preset, strlen(preset)) % 250u);
}

/* Fingerprint one recording; false when it cannot be read now. */
static bool fp_compute(Storage* storage, const char* name, RgFingerprint* fp) {
    FuriString* path = furi_string_alloc_printf("%s/%s", RADIOGEDDON_SIGNALS_FOLDER, name);
    RadioGeddonLoadedSignal sig;
    radiogeddon_loaded_signal_init(&sig);
    bool ok = radiogeddon_storage_load(storage, furi_string_get_cstr(path), &sig);
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
                RgAnalyzer* a = radiogeddon_analysis_run_raw(file, &status);
                if(a) {
                    // The 4th pass: distributions, preamble and sync gap.
                    RgFpBuilder* b = malloc(sizeof(RgFpBuilder));
                    rg_fp_builder_init(b, &a->result);
                    rg_raw_reader_rewind(&file->reader);
                    int32_t chunk[64];
                    size_t n;
                    while((n = rg_raw_reader_read(&file->reader, chunk, 64)) > 0)
                        rg_fp_builder_feed(b, chunk, n);
                    rg_fp_from_analysis(fp, &a->result, b, sig.frequency, preset);
                    free(b);
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

/* Up to @p max names of .sub files not yet seen (and not too long). */
static size_t fp_next_batch(
    Storage* storage,
    const FpSeen* seen,
    char (*names)[RG_FP_NAME_MAX],
    size_t max,
    uint32_t* total,
    bool* truncated) {
    File* dir = storage_file_alloc(storage);
    FileInfo info;
    char name[RG_FP_NAME_MAX + 8];
    size_t n = 0;
    uint32_t files = 0;
    if(storage_dir_open(dir, RADIOGEDDON_SIGNALS_FOLDER)) {
        while(storage_dir_read(dir, &info, name, sizeof(name))) {
            if((info.flags & FSF_DIRECTORY) || !fp_is_sub(name)) continue;
            files++;
            if(strlen(name) >= RG_FP_NAME_MAX) continue;
            if(n < max && !fp_seen(seen, fp_name_hash(name)))
                strlcpy(names[n++], name, RG_FP_NAME_MAX);
        }
    }
    storage_dir_close(dir);
    storage_file_free(dir);
    if(total) *total = files;
    if(truncated) *truncated = files > RG_FP_INDEX_MAX;
    return n;
}

size_t radiogeddon_fp_memory(void) {
    return sizeof(RgAnalyzer) + sizeof(RgFpBuilder) + RG_FP_INDEX_MAX * sizeof(uint32_t) +
           FP_BATCH * RG_FP_NAME_MAX + 2048u;
}

/* Rewrite the index: keep current records, add the missing recordings. */
static bool fp_update(
    Storage* storage,
    RadioGeddonFpResult* out,
    FpSeen* seen,
    RadioGeddonProgressCallback progress,
    void* context) {
    uint32_t files = 0;
    char(*names)[RG_FP_NAME_MAX] = malloc(FP_BATCH * RG_FP_NAME_MAX);
    // How much work there is, for the progress figure.
    fp_next_batch(storage, seen, names, 0, &files, &out->truncated);
    uint32_t done = 0, total = files ? files : 1;

    File* in = storage_file_alloc(storage);
    File* tmp = storage_file_alloc(storage);
    bool ok = storage_file_open(tmp, RADIOGEDDON_FP_TEMP, FSAM_WRITE, FSOM_CREATE_ALWAYS);
    uint8_t header[RG_FP_HEADER_BYTES];
    rg_fp_header_encode(0, header);
    ok = ok && storage_file_write(tmp, header, sizeof(header)) == sizeof(header);
    uint32_t count = 0;

    // 1. The old index's records that still describe their recording.
    if(ok && storage_file_open(in, RADIOGEDDON_FP_INDEX, FSAM_READ, FSOM_OPEN_EXISTING)) {
        bool valid = storage_file_read(in, header, sizeof(header)) == sizeof(header) &&
                     rg_fp_header_decode(header) >= 0;
        RgFpRecord rec;
        bool end = !valid;
        while(!end && ok && count < RG_FP_INDEX_MAX) {
            if(!fp_read_record(in, &rec, &end)) {
                if(!end) out->dropped++; // damaged: skipped, the next stays aligned
                continue;
            }
            uint32_t size, ts, h = fp_name_hash(rec.name);
            if(fp_seen(seen, h) || !fp_stat(storage, rec.name, &size, &ts) ||
               !rg_fp_record_current(&rec, size, ts)) {
                out->dropped++; // gone, renamed, changed or a duplicate
                continue;
            }
            ok = fp_write_record(tmp, &rec);
            fp_see(seen, h);
            count++;
            done++;
            if(progress) progress(context, done, total);
        }
        if(!valid) out->dropped++;
    }
    storage_file_close(in);

    // 2. Recordings without a record, a batch at a time.
    while(ok && count < RG_FP_INDEX_MAX) {
        size_t n = fp_next_batch(storage, seen, names, FP_BATCH, NULL, NULL);
        if(n == 0) break;
        for(size_t i = 0; i < n && ok && count < RG_FP_INDEX_MAX; i++) {
            RgFpRecord rec;
            memset(&rec, 0, sizeof(rec));
            strlcpy(rec.name, names[i], sizeof(rec.name));
            fp_see(seen, fp_name_hash(rec.name)); // never retried in this search
            if(fp_stat(storage, rec.name, &rec.size, &rec.timestamp) &&
               fp_compute(storage, rec.name, &rec.fp)) {
                ok = fp_write_record(tmp, &rec);
                count++;
                out->computed++;
            } else {
                out->unreadable++;
            }
            done++;
            if(progress) progress(context, done < total ? done : total, total);
        }
        if(seen->count >= RG_FP_INDEX_MAX) break;
    }
    free(names);

    // 3. The real count in the header, then swap the files.
    if(ok) {
        rg_fp_header_encode(count, header);
        ok = storage_file_seek(tmp, 0, true) &&
             storage_file_write(tmp, header, sizeof(header)) == sizeof(header);
    }
    ok = storage_file_close(tmp) && ok;
    storage_file_free(tmp);
    storage_file_free(in);
    if(ok) {
        FS_Error err = storage_common_rename(storage, RADIOGEDDON_FP_TEMP, RADIOGEDDON_FP_INDEX);
        if(err == FSE_EXIST) {
            storage_common_remove(storage, RADIOGEDDON_FP_INDEX);
            err = storage_common_rename(storage, RADIOGEDDON_FP_TEMP, RADIOGEDDON_FP_INDEX);
        }
        ok = err == FSE_OK;
    }
    if(!ok) {
        storage_common_remove(storage, RADIOGEDDON_FP_TEMP);
        FURI_LOG_W(TAG, "index not written");
    }
    out->indexed = count;
    return ok;
}

bool radiogeddon_fp_find_similar(
    Storage* storage,
    const char* target,
    RadioGeddonFpResult* out,
    RadioGeddonProgressCallback progress,
    void* context) {
    memset(out, 0, sizeof(*out));
    if(memmgr_heap_get_max_free_block() < radiogeddon_fp_memory()) return false;
    const char* slash = strrchr(target, '/');
    const char* tname = slash ? slash + 1 : target;

    FpSeen seen = {malloc(RG_FP_INDEX_MAX * sizeof(uint32_t)), 0};
    out->index_failed = !fp_update(storage, out, &seen, progress, context);
    free(seen.hash);

    RgFpRecord* rec = malloc(sizeof(RgFpRecord));
    RgFingerprint* tfp = malloc(sizeof(RgFingerprint));
    File* file = storage_file_alloc(storage);
    bool found = false;
    // The target's own fingerprint: from the index, or computed now.
    if(!out->index_failed &&
       storage_file_open(file, RADIOGEDDON_FP_INDEX, FSAM_READ, FSOM_OPEN_EXISTING)) {
        uint8_t header[RG_FP_HEADER_BYTES];
        bool end = storage_file_read(file, header, sizeof(header)) != sizeof(header);
        while(!end && !found) {
            if(fp_read_record(file, rec, &end) && strcmp(rec->name, tname) == 0) {
                *tfp = rec->fp;
                found = true;
            }
        }
    }
    storage_file_close(file);
    if(!found) found = fp_compute(storage, tname, tfp);
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
