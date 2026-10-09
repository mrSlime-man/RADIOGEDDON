#include "rg_db.h"

#include <string.h>

static char rg_db_lower(char c) {
    return (c >= 'A' && c <= 'Z') ? (char)(c - 'A' + 'a') : c;
}

static int rg_db_casecmp(const char* a, const char* b) {
    while(*a && rg_db_lower(*a) == rg_db_lower(*b)) {
        a++;
        b++;
    }
    return (int)(unsigned char)rg_db_lower(*a) - (int)(unsigned char)rg_db_lower(*b);
}

void rg_db_init(RgDb* db, RgDbEntry* entries, size_t cap, char* names, size_t names_cap) {
    db->entries = entries;
    db->count = 0;
    db->cap = cap;
    db->names = names;
    db->names_len = 0;
    db->names_cap = names_cap;
}

RgDbEntry* rg_db_add(RgDb* db, const char* name) {
    size_t len = strlen(name) + 1;
    if(db->count >= db->cap || db->names_len + len > db->names_cap) return NULL;
    if(db->names_len > UINT16_MAX) return NULL;
    RgDbEntry* e = &db->entries[db->count++];
    memset(e, 0, sizeof(*e));
    e->name_off = (uint16_t)db->names_len;
    memcpy(db->names + db->names_len, name, len);
    db->names_len += len;
    return e;
}

const char* rg_db_name(const RgDb* db, const RgDbEntry* e) {
    return db->names + e->name_off;
}

uint32_t rg_db_hash(uint32_t hash, const void* data, size_t len) {
    const uint8_t* p = data;
    for(size_t i = 0; i < len; i++) {
        hash ^= p[i];
        hash *= 16777619u;
    }
    return hash;
}

/* ---- Header parsing ------------------------------------------------------ */

static bool rg_db_key_is(const char* line, size_t key_len, const char* key) {
    return strlen(key) == key_len && memcmp(line, key, key_len) == 0;
}

static uint32_t rg_db_parse_u32(const char* v, size_t len, bool* ok) {
    uint64_t n = 0;
    *ok = len > 0;
    for(size_t i = 0; i < len; i++) {
        if(v[i] < '0' || v[i] > '9' || n > UINT32_MAX / 10u) {
            *ok = false;
            return 0;
        }
        n = n * 10u + (uint32_t)(v[i] - '0');
    }
    if(n > UINT32_MAX) *ok = false;
    return *ok ? (uint32_t)n : 0;
}

/* One "Key: value" line; returns false when the header ends (RAW data). */
static bool
    rg_db_parse_line(RgDbEntry* e, const char* line, size_t len, bool* typed, bool* first) {
    while(len && (line[len - 1] == '\r' || line[len - 1] == ' '))
        len--;
    if(len == 0 || line[0] == '#') return true;
    const char* colon = memchr(line, ':', len);
    if(!colon) {
        if(!*first) return true; // stray text inside a header is ignored
        *first = false;
        return false; // not a Sub-GHz file
    }
    size_t key_len = (size_t)(colon - line);
    const char* v = colon + 1;
    size_t vlen = len - key_len - 1;
    while(vlen && *v == ' ') {
        v++;
        vlen--;
    }

    if(*first) {
        *first = false;
        if(!rg_db_key_is(line, key_len, "Filetype")) return false;
        *typed = (vlen == 23 && memcmp(v, "Flipper SubGhz Key File", 23) == 0) ||
                 (vlen == 23 && memcmp(v, "Flipper SubGhz RAW File", 23) == 0);
        return *typed;
    }
    if(rg_db_key_is(line, key_len, "RAW_Data")) return false;
    if(rg_db_key_is(line, key_len, "Frequency")) {
        bool ok;
        uint32_t f = rg_db_parse_u32(v, vlen, &ok);
        e->frequency = ok ? f : 0;
    } else if(rg_db_key_is(line, key_len, "Protocol")) {
        size_t n = vlen < RG_DB_PROTO_MAX - 1 ? vlen : RG_DB_PROTO_MAX - 1;
        memcpy(e->protocol, v, n);
        e->protocol[n] = '\0';
    } else if(rg_db_key_is(line, key_len, "Bit")) {
        bool ok;
        uint32_t b = rg_db_parse_u32(v, vlen, &ok);
        e->bits = ok && b <= UINT16_MAX ? (uint16_t)b : 0;
    } else if(rg_db_key_is(line, key_len, "Key")) {
        // Hash the hex digits only, upper-cased, so spacing does not matter.
        uint32_t h = RG_DB_HASH_INIT;
        for(size_t i = 0; i < vlen; i++) {
            char c = v[i];
            if(c == ' ') continue;
            if(c >= 'a' && c <= 'f') c = (char)(c - 'a' + 'A');
            h = rg_db_hash(h, &c, 1);
        }
        e->hash = h;
        e->flags |= RG_DB_FLAG_HAS_KEY;
    }
    return true;
}

void rg_db_parse_header(RgDbEntry* e, const char* text, size_t len, bool whole_file) {
    e->kind = RgDbKindCorrupt;
    e->protocol[0] = '\0';
    e->frequency = 0;
    e->bits = 0;
    e->hash = 0;
    e->flags &= (uint8_t) ~(RG_DB_FLAG_HAS_KEY | RG_DB_FLAG_HASHED);

    bool typed = false, first = true;
    size_t start = 0;
    while(start < len) {
        const char* nl = memchr(text + start, '\n', len - start);
        size_t end = nl ? (size_t)(nl - text) : len;
        if(!nl && !whole_file) break; // possibly cut off
        if(!rg_db_parse_line(e, text + start, end - start, &typed, &first)) break;
        start = end + 1;
    }
    if(!typed) {
        e->kind = RgDbKindCorrupt;
        e->protocol[0] = '\0';
        e->flags &= (uint8_t)~RG_DB_FLAG_HAS_KEY;
        return;
    }
    if(e->protocol[0] == '\0') {
        e->kind = RgDbKindUnknown;
    } else if(strcmp(e->protocol, "RAW") == 0) {
        e->kind = RgDbKindRaw;
        e->flags &= (uint8_t)~RG_DB_FLAG_HAS_KEY;
        e->hash = 0;
    } else {
        e->kind = RgDbKindProtocol;
    }
}

/* ---- Duplicates ---------------------------------------------------------- */

bool rg_db_needs_content_hash(const RgDb* db, size_t index) {
    const RgDbEntry* e = &db->entries[index];
    if(e->kind != RgDbKindRaw) return false;
    for(size_t j = 0; j < db->count; j++) {
        const RgDbEntry* o = &db->entries[j];
        if(j != index && o->kind == RgDbKindRaw && o->size == e->size) return true;
    }
    return false;
}

void rg_db_set_content_hash(RgDbEntry* e, uint32_t hash) {
    e->hash = hash;
    e->flags |= RG_DB_FLAG_HASHED;
}

bool rg_db_same_signal(const RgDbEntry* a, const RgDbEntry* b) {
    if(a->kind != b->kind) return false;
    if(a->kind == RgDbKindProtocol) {
        return (a->flags & b->flags & RG_DB_FLAG_HAS_KEY) && a->hash == b->hash &&
               a->frequency == b->frequency && a->bits == b->bits &&
               strcmp(a->protocol, b->protocol) == 0;
    }
    if(a->kind == RgDbKindRaw) {
        return (a->flags & b->flags & RG_DB_FLAG_HASHED) && a->size == b->size &&
               a->hash == b->hash;
    }
    return false;
}

void rg_db_mark_duplicates(RgDb* db) {
    for(size_t i = 0; i < db->count; i++) {
        db->entries[i].flags &= (uint8_t)~RG_DB_FLAG_DUPLICATE;
        db->entries[i].dup_count = 0;
    }
    for(size_t i = 0; i < db->count; i++) {
        for(size_t j = i + 1; j < db->count; j++) {
            if(!rg_db_same_signal(&db->entries[i], &db->entries[j])) continue;
            db->entries[i].flags |= RG_DB_FLAG_DUPLICATE;
            db->entries[j].flags |= RG_DB_FLAG_DUPLICATE;
            if(db->entries[i].dup_count < UINT16_MAX) db->entries[i].dup_count++;
            if(db->entries[j].dup_count < UINT16_MAX) db->entries[j].dup_count++;
        }
    }
}

/* ---- Query ---------------------------------------------------------------- */

bool rg_db_name_matches(const char* name, const char* search) {
    if(!search[0]) return true;
    for(const char* p = name; *p; p++) {
        const char* a = p;
        const char* b = search;
        while(*a && *b && rg_db_lower(*a) == rg_db_lower(*b)) {
            a++;
            b++;
        }
        if(!*b) return true;
    }
    return false;
}

static bool rg_db_shown(const RgDb* db, const RgDbEntry* e, const RgDbQuery* q) {
    switch(q->show) {
    case RgDbShowRaw:
        if(e->kind != RgDbKindRaw) return false;
        break;
    case RgDbShowDecoded:
        if(e->kind != RgDbKindProtocol) return false;
        break;
    case RgDbShowProtocol:
        if(e->kind != RgDbKindProtocol || rg_db_casecmp(e->protocol, q->protocol) != 0)
            return false;
        break;
    case RgDbShowDuplicates:
        if(!(e->flags & RG_DB_FLAG_DUPLICATE)) return false;
        break;
    case RgDbShowDamaged:
        if(e->kind != RgDbKindCorrupt && e->kind != RgDbKindUnknown) return false;
        break;
    default:
        break;
    }
    return rg_db_name_matches(rg_db_name(db, e), q->search);
}

/* Ties fall back to the name. In protocol order, damaged files go last. */
static int rg_db_compare(const RgDb* db, const RgDbEntry* a, const RgDbEntry* b, RgDbSort sort) {
    int c = 0;
    switch(sort) {
    case RgDbSortDate:
        c = a->mtime == b->mtime ? 0 : (a->mtime > b->mtime ? -1 : 1);
        break;
    case RgDbSortFrequency:
        c = a->frequency == b->frequency ? 0 : (a->frequency < b->frequency ? -1 : 1);
        if(c == 0) break;
        // Unknown frequency (0) last.
        if(a->frequency == 0) c = 1;
        if(b->frequency == 0) c = -1;
        break;
    case RgDbSortProtocol: {
        bool da = a->kind < RgDbKindRaw, dbk = b->kind < RgDbKindRaw;
        if(da != dbk) return da ? 1 : -1;
        c = rg_db_casecmp(a->protocol, b->protocol);
        break;
    }
    default:
        break;
    }
    if(c == 0) c = rg_db_casecmp(rg_db_name(db, a), rg_db_name(db, b));
    return c;
}

size_t rg_db_select(const RgDb* db, const RgDbQuery* q, uint16_t* out, size_t max) {
    size_t n = 0;
    for(size_t i = 0; i < db->count && n < max; i++) {
        if(rg_db_shown(db, &db->entries[i], q)) out[n++] = (uint16_t)i;
    }
    // Insertion sort: stable, no allocation, fine for a few hundred files.
    for(size_t i = 1; i < n; i++) {
        uint16_t v = out[i];
        size_t j = i;
        while(j > 0 && rg_db_compare(db, &db->entries[out[j - 1]], &db->entries[v], q->sort) > 0) {
            out[j] = out[j - 1];
            j--;
        }
        out[j] = v;
    }
    return n;
}

size_t rg_db_protocols(const RgDb* db, const char** out, size_t max) {
    size_t n = 0;
    for(size_t i = 0; i < db->count; i++) {
        const RgDbEntry* e = &db->entries[i];
        if(e->kind != RgDbKindProtocol) continue;
        bool seen = false;
        for(size_t k = 0; k < n && !seen; k++)
            seen = rg_db_casecmp(out[k], e->protocol) == 0;
        if(seen || n >= max) continue;
        // Insert in order.
        size_t j = n++;
        while(j > 0 && rg_db_casecmp(out[j - 1], e->protocol) > 0) {
            out[j] = out[j - 1];
            j--;
        }
        out[j] = e->protocol;
    }
    return n;
}

const char* rg_db_sort_name(RgDbSort sort) {
    switch(sort) {
    case RgDbSortDate:
        return "Date";
    case RgDbSortName:
        return "Name";
    case RgDbSortFrequency:
        return "Freq";
    case RgDbSortProtocol:
        return "Protocol";
    default:
        return "?";
    }
}

const char* rg_db_show_name(RgDbShow show) {
    switch(show) {
    case RgDbShowAll:
        return "All";
    case RgDbShowRaw:
        return "RAW";
    case RgDbShowDecoded:
        return "Decoded";
    case RgDbShowProtocol:
        return "Protocol";
    case RgDbShowDuplicates:
        return "Duplicates";
    case RgDbShowDamaged:
        return "Damaged";
    default:
        return "?";
    }
}

int32_t rg_db_find(const RgDb* db, const char* name) {
    for(size_t i = 0; i < db->count; i++) {
        if(strcmp(rg_db_name(db, &db->entries[i]), name) == 0) return (int32_t)i;
    }
    return -1;
}

/* ---- File names ----------------------------------------------------------- */

RgDbNameError rg_db_check_name(const char* name) {
    size_t len = strlen(name);
    if(len == 0) return RgDbNameEmpty;
    if(len > RG_DB_NAME_MAX) return RgDbNameTooLong;
    for(size_t i = 0; i < len; i++) {
        unsigned char c = (unsigned char)name[i];
        if(c < 0x20 || c == 0x7F || strchr("<>:\"/\\|?*", c)) return RgDbNameBadChar;
    }
    if(name[0] == '.' || name[0] == ' ' || name[len - 1] == '.' || name[len - 1] == ' ')
        return RgDbNameBadEdge;
    return RgDbNameOk;
}

const char* rg_db_name_error_text(RgDbNameError error) {
    switch(error) {
    case RgDbNameOk:
        return "";
    case RgDbNameEmpty:
        return "Enter a name";
    case RgDbNameTooLong:
        return "Name too long";
    case RgDbNameBadChar:
        return "Not allowed:\n< > : \" / \\ | ? *";
    case RgDbNameBadEdge:
        return "No . or space at\nstart or end";
    default:
        return "Invalid name";
    }
}

void rg_db_strip_ext(char* name, const char* ext) {
    size_t len = strlen(name);
    size_t elen = strlen(ext);
    if(len < elen) return;
    char* tail = name + len - elen;
    for(size_t i = 0; i < elen; i++) {
        if(rg_db_lower(tail[i]) != rg_db_lower(ext[i])) return;
    }
    *tail = '\0';
}
