#include "radiogeddon_db.h"
#include "radiogeddon_storage.h"

#include <datetime/datetime.h>

#define TAG "RadioGeddonDb"

/* Never index more files than this, whatever the heap (view indices are
 * 16-bit). */
#define DB_MAX_FILES       2000u
/* Nor more bytes than beta 10's index of its 500-file cap took with typical
 * names (40-byte entries, 2-byte view slots, ~23-byte names): the compact
 * entries fit about 600 such files in it. */
#define DB_INDEX_MAX_BYTES (32u * 1024u)
/* Free heap left untouched for the screens opened from the Database. */
#define DB_HEAP_SPARE      (24u * 1024u)
#define DB_NAME_BUF        128u
#define DB_HASH_CHUNK      512u

/* Offsets of the parts of the index in its one block. */
typedef struct {
    size_t entries, view, names, protos, total;
} DbLayout;

static size_t db_align(size_t n) {
    return (n + 7u) & ~(size_t)7u;
}

static void db_layout(DbLayout* l, size_t cap, size_t pool, size_t protos) {
    l->entries = db_align(sizeof(RadioGeddonDb));
    l->view = l->entries + db_align(cap * sizeof(RgDbEntry));
    l->names = l->view + db_align(cap * sizeof(uint16_t));
    l->protos = l->names + pool;
    l->total = l->protos + protos;
}

static bool radiogeddon_db_is_sub(const FileInfo* info, const char* name) {
    if(file_info_is_dir(info) || name[0] == '.') return false;
    size_t len = strlen(name);
    size_t ext = strlen(RADIOGEDDON_SUB_EXTENSION);
    if(len <= ext) return false;
    const char* tail = name + len - ext;
    for(size_t i = 0; i < ext; i++) {
        char c = tail[i];
        if(c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
        if(c != RADIOGEDDON_SUB_EXTENSION[i]) return false;
    }
    return true;
}

/* Count the .sub files and the bytes their names need. */
static void radiogeddon_db_count(Storage* storage, char* name, size_t* files, size_t* name_bytes) {
    *files = 0;
    *name_bytes = 0;
    File* dir = storage_file_alloc(storage);
    if(storage_dir_open(dir, RADIOGEDDON_SIGNALS_FOLDER)) {
        FileInfo info;
        while(storage_dir_read(dir, &info, name, DB_NAME_BUF)) {
            if(!radiogeddon_db_is_sub(&info, name)) continue;
            (*files)++;
            *name_bytes += strlen(name) + 1;
        }
    }
    storage_dir_close(dir);
    storage_file_free(dir);
}

/* Read the head of one file into its entry; false when the protocol table
 * is full (more distinct protocol names than RG_DB_PROTOS_MAX holds). */
static bool radiogeddon_db_read_entry(
    Storage* storage,
    RgDb* db,
    RgDbEntry* e,
    const char* path,
    uint8_t* buf) {
    uint32_t ts = 0;
    if(storage_common_timestamp(storage, path, &ts) == FSE_OK) e->mtime = ts;
    File* file = storage_file_alloc(storage);
    size_t n = 0;
    if(storage_file_open(file, path, FSAM_READ, FSOM_OPEN_EXISTING)) {
        n = storage_file_read(file, buf, RG_DB_HEAD_BYTES);
    }
    storage_file_close(file);
    storage_file_free(file);
    char protocol[RG_DB_PROTO_MAX];
    rg_db_parse_header(e, (const char*)buf, n, n < RG_DB_HEAD_BYTES || n == e->size, protocol);
    return rg_db_set_protocol(db, e, protocol);
}

static uint32_t
    radiogeddon_db_hash_file(Storage* storage, const char* path, uint8_t* buf, bool* ok) {
    uint32_t h = RG_DB_HASH_INIT;
    File* file = storage_file_alloc(storage);
    *ok = storage_file_open(file, path, FSAM_READ, FSOM_OPEN_EXISTING);
    if(*ok) {
        size_t n;
        while((n = storage_file_read(file, buf, DB_HASH_CHUNK)) > 0)
            h = rg_db_hash(h, buf, n);
        *ok = storage_file_get_error(file) == FSE_OK;
    }
    storage_file_close(file);
    storage_file_free(file);
    return h;
}

RadioGeddonDb* radiogeddon_db_load(
    Storage* storage,
    RadioGeddonDbStatus* status,
    RadioGeddonProgressCallback progress,
    void* context) {
    *status = RadioGeddonDbOk;
    // Counting needs a name buffer only: allocated and freed before the
    // index, so it leaves the heap as it found it.
    char* name = malloc(DB_NAME_BUF);
    size_t files = 0, name_bytes = 0;
    radiogeddon_db_count(storage, name, &files, &name_bytes);
    free(name);

    // A protocol table of 8 bytes an entry (most libraries hold a handful of
    // protocols); a library with more distinct names than that holds is read
    // again with one that fits a name for every entry, which never fills.
    RadioGeddonDb* db = NULL;
    uint8_t* buf = NULL;
    FuriString* path = NULL;
    size_t cap = 0;
    for(size_t per_entry = 8u; !db; per_entry = RG_DB_PROTO_MAX) {
        // Size the index to the listing, within what the heap can spare and
        // DB_INDEX_MAX_BYTES.
        size_t avail = memmgr_heap_get_max_free_block();
        size_t work = DB_NAME_BUF + DB_HASH_CHUNK;
        size_t budget = avail > DB_HEAP_SPARE + work ? avail - DB_HEAP_SPARE - work : 0;
        if(budget > DB_INDEX_MAX_BYTES) budget = DB_INDEX_MAX_BYTES;
        size_t avg_name = files ? name_bytes / files + 1 : 1;
        size_t per_file = sizeof(RgDbEntry) + sizeof(uint16_t) + avg_name + 1u;
        cap = files < DB_MAX_FILES ? files : DB_MAX_FILES;
        // The protocol table for that many first (it only shrinks with
        // cap), then as many entries as fit beside it.
        size_t fixed = db_align(sizeof(RadioGeddonDb)) + 64u + rg_db_protos_bytes(cap, per_entry);
        size_t fit = budget > fixed ? (budget - fixed) / per_file : 0;
        if(cap > fit) cap = fit;
        size_t protos = rg_db_protos_bytes(cap, per_entry);
        if(files > 0 && cap == 0) {
            *status = RadioGeddonDbNoMemory;
            return NULL;
        }
        size_t pool = cap == files ? name_bytes : cap * avg_name + 64;
        if(pool > name_bytes) pool = name_bytes;
        if(pool > UINT16_MAX) pool = UINT16_MAX;

        // One block: the list's state, its entries, view, names and
        // protocol names (beta 10: four blocks, 32 bytes of headers and a
        // hole each time one of them was freed first).
        DbLayout l;
        db_layout(&l, cap, pool, protos);
        uint8_t* block = malloc(l.total);
        memset(block, 0, l.entries);
        db = (RadioGeddonDb*)block;
        db->view = cap ? (uint16_t*)(block + l.view) : NULL;
        rg_db_init(
            &db->db,
            cap ? (RgDbEntry*)(block + l.entries) : NULL,
            cap,
            pool ? (char*)(block + l.names) : NULL,
            pool,
            protos ? (char*)(block + l.protos) : NULL,
            protos);
        db->total_files = files;
        db->query.sort = RgDbSortDate;
        db->query.show = RgDbShowAll;

        // The work buffers, after the index: freed at the end, they leave
        // the index alone at the bottom of the space it was given.
        buf = malloc(work);
        name = (char*)buf + DB_HASH_CHUNK;
        path = furi_string_alloc();
        File* dir = storage_file_alloc(storage);
        if(cap && storage_dir_open(dir, RADIOGEDDON_SIGNALS_FOLDER)) {
            FileInfo info;
            while(storage_dir_read(dir, &info, name, DB_NAME_BUF)) {
                if(!radiogeddon_db_is_sub(&info, name)) continue;
                RgDbEntry* e = rg_db_add(&db->db, name);
                if(!e) {
                    db->truncated = true;
                    continue;
                }
                e->size = info.size > UINT32_MAX ? UINT32_MAX : (uint32_t)info.size;
            }
        }
        storage_dir_close(dir);
        storage_file_free(dir);
        if(db->db.count < files) db->truncated = true;

        // Then read each file's head (with the directory closed again).
        for(size_t i = 0; i < db->db.count; i++) {
            RgDbEntry* e = &db->db.entries[i];
            radiogeddon_db_path(db, e, path);
            if(!radiogeddon_db_read_entry(storage, &db->db, e, furi_string_get_cstr(path), buf)) {
                if(per_entry < RG_DB_PROTO_MAX) {
                    // The table is full: again with the one that fits all.
                    furi_string_free(path);
                    free(buf);
                    free(block);
                    db = NULL;
                    break;
                }
                // Not reached (a name per entry fits); kept as a bound.
                db->db.count = i;
                db->truncated = true;
                break;
            }
            if(progress) progress(context, i + 1, db->db.count);
        }
    }

    // Confirm RAW duplicates by content, only where sizes collide. The list's
    // view array is free until radiogeddon_db_apply fills it: it holds the
    // candidates, then lends the duplicate grouping its work space.
    size_t candidates = cap ? rg_db_hash_candidates(&db->db, db->view) : 0;
    for(size_t i = 0; i < candidates; i++) {
        RgDbEntry* e = &db->db.entries[db->view[i]];
        radiogeddon_db_path(db, e, path);
        bool ok;
        uint32_t h = radiogeddon_db_hash_file(storage, furi_string_get_cstr(path), buf, &ok);
        if(ok) rg_db_set_content_hash(e, h);
    }
    if(cap) rg_db_mark_duplicates(&db->db, db->view);
    radiogeddon_db_apply(db);

    FURI_LOG_I(
        TAG,
        "%u of %u files indexed, %u protocol bytes",
        (unsigned)db->db.count,
        (unsigned)files,
        (unsigned)db->db.protos_len);
    furi_string_free(path);
    free(buf);
    return db;
}

void radiogeddon_db_free(RadioGeddonDb* db) {
    free(db); // the whole index is one block
}

void radiogeddon_db_apply(RadioGeddonDb* db) {
    db->view_count = db->db.cap ? rg_db_select(&db->db, &db->query, db->view, db->db.cap) : 0;
}

const RgDbEntry* radiogeddon_db_at(const RadioGeddonDb* db, size_t position) {
    if(position >= db->view_count) return NULL;
    return &db->db.entries[db->view[position]];
}

void radiogeddon_db_path(const RadioGeddonDb* db, const RgDbEntry* e, FuriString* out) {
    furi_string_printf(out, "%s/%s", RADIOGEDDON_SIGNALS_FOLDER, rg_db_name(&db->db, e));
}

void radiogeddon_db_format_time(uint32_t timestamp, char* out, size_t size) {
    if(timestamp == 0) {
        snprintf(out, size, "--");
        return;
    }
    DateTime dt;
    datetime_timestamp_to_datetime(timestamp, &dt);
    snprintf(out, size, "%02u-%02u %02u:%02u", dt.month, dt.day, dt.hour, dt.minute);
}
