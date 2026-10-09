#include "radiogeddon_db.h"
#include "radiogeddon_storage.h"

#include <datetime/datetime.h>

#define TAG "RadioGeddonDb"

/* Never index more files than this, whatever the heap. */
#define DB_MAX_FILES  500u
/* Free heap left untouched for the screens opened from the Database. */
#define DB_HEAP_SPARE (24u * 1024u)
#define DB_NAME_BUF   128u
#define DB_HASH_CHUNK 512u

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

/* Read the head of one file into its entry. */
static void
    radiogeddon_db_read_entry(Storage* storage, RgDbEntry* e, const char* path, uint8_t* buf) {
    uint32_t ts = 0;
    if(storage_common_timestamp(storage, path, &ts) == FSE_OK) e->mtime = ts;
    File* file = storage_file_alloc(storage);
    size_t n = 0;
    if(storage_file_open(file, path, FSAM_READ, FSOM_OPEN_EXISTING)) {
        n = storage_file_read(file, buf, RG_DB_HEAD_BYTES);
    }
    storage_file_close(file);
    storage_file_free(file);
    rg_db_parse_header(e, (const char*)buf, n, n < RG_DB_HEAD_BYTES || n == e->size);
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

RadioGeddonDb* radiogeddon_db_load(Storage* storage, RadioGeddonDbStatus* status) {
    *status = RadioGeddonDbOk;
    char* name = malloc(DB_NAME_BUF);
    size_t files = 0, name_bytes = 0;
    radiogeddon_db_count(storage, name, &files, &name_bytes);

    // Size the index to the listing, within what the heap can spare.
    size_t avail = memmgr_heap_get_max_free_block();
    size_t budget = avail > DB_HEAP_SPARE + sizeof(RadioGeddonDb) + DB_HASH_CHUNK ?
                        avail - DB_HEAP_SPARE - sizeof(RadioGeddonDb) - DB_HASH_CHUNK :
                        0;
    size_t avg_name = files ? name_bytes / files + 1 : 1;
    size_t per_file = sizeof(RgDbEntry) + sizeof(uint16_t) + avg_name;
    size_t cap = files < DB_MAX_FILES ? files : DB_MAX_FILES;
    if(cap > budget / per_file) cap = budget / per_file;
    if(files > 0 && cap == 0) {
        free(name);
        *status = RadioGeddonDbNoMemory;
        return NULL;
    }
    size_t pool = cap == files ? name_bytes : cap * avg_name + 64;
    if(pool > name_bytes) pool = name_bytes;
    if(pool > UINT16_MAX) pool = UINT16_MAX;

    RadioGeddonDb* db = malloc(sizeof(RadioGeddonDb));
    memset(db, 0, sizeof(RadioGeddonDb));
    RgDbEntry* entries = cap ? malloc(sizeof(RgDbEntry) * cap) : NULL;
    char* names = pool ? malloc(pool) : NULL;
    db->view = cap ? malloc(sizeof(uint16_t) * cap) : NULL;
    rg_db_init(&db->db, entries, cap, names, pool);
    db->total_files = files;
    db->query.sort = RgDbSortDate;
    db->query.show = RgDbShowAll;

    uint8_t* buf = malloc(DB_HASH_CHUNK);
    FuriString* path = furi_string_alloc();
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
        radiogeddon_db_read_entry(storage, e, furi_string_get_cstr(path), buf);
    }

    // Confirm RAW duplicates by content, only where sizes collide.
    for(size_t i = 0; i < db->db.count; i++) {
        if(!rg_db_needs_content_hash(&db->db, i)) continue;
        RgDbEntry* e = &db->db.entries[i];
        radiogeddon_db_path(db, e, path);
        bool ok;
        uint32_t h = radiogeddon_db_hash_file(storage, furi_string_get_cstr(path), buf, &ok);
        if(ok) rg_db_set_content_hash(e, h);
    }
    rg_db_mark_duplicates(&db->db);
    radiogeddon_db_apply(db);

    FURI_LOG_I(
        TAG,
        "%u of %u files indexed, %u bytes",
        (unsigned)db->db.count,
        (unsigned)files,
        (unsigned)(cap * (sizeof(RgDbEntry) + sizeof(uint16_t)) + pool));
    furi_string_free(path);
    free(buf);
    free(name);
    return db;
}

void radiogeddon_db_free(RadioGeddonDb* db) {
    if(!db) return;
    free(db->db.entries);
    free(db->db.names);
    free(db->view);
    free(db);
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
