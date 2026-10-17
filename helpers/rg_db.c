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

void rg_db_init(
    RgDb* db,
    RgDbEntry* entries,
    size_t cap,
    char* names,
    size_t names_cap,
    char* protos,
    size_t protos_cap) {
    db->entries = entries;
    db->count = 0;
    db->cap = cap;
    db->names = names;
    db->names_len = 0;
    db->names_cap = names_cap;
    db->protos = protos;
    db->protos_len = 0;
    db->protos_cap = protos_cap > RG_DB_NO_PROTO ? RG_DB_NO_PROTO : protos_cap;
}

size_t rg_db_protos_bytes(size_t cap, size_t per_entry) {
    return cap < RG_DB_PROTOS_MAX / per_entry ? cap * per_entry : RG_DB_PROTOS_MAX;
}

bool rg_db_set_protocol(RgDb* db, RgDbEntry* e, const char* protocol) {
    e->proto_off = RG_DB_NO_PROTO;
    if(!protocol[0]) return true;
    // Distinct names are few (one per protocol): a linear search is enough.
    size_t off = 0;
    while(off < db->protos_len && strcmp(db->protos + off, protocol) != 0)
        off += strlen(db->protos + off) + 1;
    if(off >= db->protos_len) {
        size_t len = strlen(protocol);
        if(len > RG_DB_PROTO_MAX - 1) len = RG_DB_PROTO_MAX - 1;
        if(off + len + 1 > db->protos_cap) return false;
        memcpy(db->protos + off, protocol, len);
        db->protos[off + len] = '\0';
        db->protos_len = off + len + 1;
    }
    e->proto_off = (uint16_t)off;
    return true;
}

const char* rg_db_protocol(const RgDb* db, const RgDbEntry* e) {
    return e->proto_off == RG_DB_NO_PROTO ? "" : db->protos + e->proto_off;
}

/* ---- In-place heap sort of entry indices ---------------------------------
 * O(n log n) compares, no allocation and no recursion. Not stable by itself:
 * every comparator below ends with the index, so the order is total and
 * matches what a stable sort of the indices in ascending order gives. */

typedef int (*RgDbCmp)(const RgDb* db, uint16_t a, uint16_t b, int arg);

static void rg_db_sift(const RgDb* db, uint16_t* v, size_t root, size_t n, RgDbCmp cmp, int arg) {
    uint16_t top = v[root];
    while(true) {
        size_t child = 2 * root + 1;
        if(child >= n) break;
        if(child + 1 < n && cmp(db, v[child], v[child + 1], arg) < 0) child++;
        if(cmp(db, top, v[child], arg) >= 0) break;
        v[root] = v[child];
        root = child;
    }
    v[root] = top;
}

static void rg_db_heapsort(const RgDb* db, uint16_t* v, size_t n, RgDbCmp cmp, int arg) {
    if(n < 2) return;
    for(size_t i = n / 2; i-- > 0;)
        rg_db_sift(db, v, i, n, cmp, arg);
    for(size_t end = n - 1; end > 0; end--) {
        uint16_t t = v[0];
        v[0] = v[end];
        v[end] = t;
        rg_db_sift(db, v, 0, end, cmp, arg);
    }
}

static int rg_db_cmp_u32(uint32_t a, uint32_t b) {
    return a == b ? 0 : (a < b ? -1 : 1);
}

RgDbEntry* rg_db_add(RgDb* db, const char* name) {
    size_t len = strlen(name) + 1;
    if(db->count >= db->cap || db->names_len + len > db->names_cap) return NULL;
    if(db->names_len > UINT16_MAX) return NULL;
    RgDbEntry* e = &db->entries[db->count++];
    memset(e, 0, sizeof(*e));
    e->proto_off = RG_DB_NO_PROTO;
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
static bool rg_db_parse_line(
    RgDbEntry* e,
    char* protocol,
    const char* line,
    size_t len,
    bool* typed,
    bool* first) {
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
        memcpy(protocol, v, n);
        protocol[n] = '\0';
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

void rg_db_parse_header(
    RgDbEntry* e,
    const char* text,
    size_t len,
    bool whole_file,
    char protocol[RG_DB_PROTO_MAX]) {
    e->kind = RgDbKindCorrupt;
    protocol[0] = '\0';
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
        if(!rg_db_parse_line(e, protocol, text + start, end - start, &typed, &first)) break;
        start = end + 1;
    }
    if(!typed) {
        e->kind = RgDbKindCorrupt;
        protocol[0] = '\0';
        e->flags &= (uint8_t)~RG_DB_FLAG_HAS_KEY;
        return;
    }
    if(protocol[0] == '\0') {
        e->kind = RgDbKindUnknown;
    } else if(strcmp(protocol, "RAW") == 0) {
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

/* RAW entries by size, then index. */
static int rg_db_cmp_size(const RgDb* db, uint16_t a, uint16_t b, int arg) {
    (void)arg;
    int c = rg_db_cmp_u32(db->entries[a].size, db->entries[b].size);
    return c ? c : rg_db_cmp_u32(a, b);
}

static int rg_db_cmp_index(const RgDb* db, uint16_t a, uint16_t b, int arg) {
    (void)db;
    (void)arg;
    return rg_db_cmp_u32(a, b);
}

size_t rg_db_hash_candidates(const RgDb* db, uint16_t* out) {
    size_t n = 0;
    for(size_t i = 0; i < db->count; i++)
        if(db->entries[i].kind == RgDbKindRaw) out[n++] = (uint16_t)i;
    rg_db_heapsort(db, out, n, rg_db_cmp_size, 0);
    // Keep the runs of two or more equal sizes.
    size_t kept = 0;
    for(size_t i = 0; i < n;) {
        size_t j = i + 1;
        while(j < n && db->entries[out[j]].size == db->entries[out[i]].size)
            j++;
        if(j - i > 1)
            for(size_t k = i; k < j; k++)
                out[kept++] = out[k];
        i = j;
    }
    // Back to index order: files are then read in folder order.
    rg_db_heapsort(db, out, kept, rg_db_cmp_index, 0);
    return kept;
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
               a->proto_off == b->proto_off; // one table entry per distinct name
    }
    if(a->kind == RgDbKindRaw) {
        return (a->flags & b->flags & RG_DB_FLAG_HASHED) && a->size == b->size &&
               a->hash == b->hash;
    }
    return false;
}

/* Only these can be the same signal as another entry (rg_db_same_signal). */
static bool rg_db_dup_eligible(const RgDbEntry* e) {
    if(e->kind == RgDbKindProtocol) return e->flags & RG_DB_FLAG_HAS_KEY;
    if(e->kind == RgDbKindRaw) return e->flags & RG_DB_FLAG_HASHED;
    return false;
}

/* Equal (0, before the index) exactly when rg_db_same_signal is true. */
static int rg_db_cmp_signal_key(const RgDb* db, uint16_t ia, uint16_t ib) {
    const RgDbEntry* a = &db->entries[ia];
    const RgDbEntry* b = &db->entries[ib];
    int c = rg_db_cmp_u32(a->kind, b->kind);
    if(!c) c = rg_db_cmp_u32(a->hash, b->hash);
    if(c) return c;
    if(a->kind == RgDbKindRaw) return rg_db_cmp_u32(a->size, b->size);
    c = rg_db_cmp_u32(a->frequency, b->frequency);
    if(!c) c = rg_db_cmp_u32(a->bits, b->bits);
    if(!c) c = rg_db_cmp_u32(a->proto_off, b->proto_off); // equal names, equal offsets
    return c;
}

static int rg_db_cmp_signal(const RgDb* db, uint16_t a, uint16_t b, int arg) {
    (void)arg;
    int c = rg_db_cmp_signal_key(db, a, b);
    return c ? c : rg_db_cmp_u32(a, b);
}

void rg_db_mark_duplicates(RgDb* db, uint16_t* scratch) {
    // Each run of equal keys is one group, every member a duplicate of the others.
    uint16_t* v = scratch;
    size_t n = 0;
    for(size_t i = 0; i < db->count; i++) {
        RgDbEntry* e = &db->entries[i];
        e->flags &= (uint8_t)~RG_DB_FLAG_DUPLICATE;
        e->dup_count = 0;
        if(rg_db_dup_eligible(e)) v[n++] = (uint16_t)i;
    }
    rg_db_heapsort(db, v, n, rg_db_cmp_signal, 0);
    for(size_t i = 0; i < n;) {
        size_t j = i + 1;
        while(j < n && rg_db_cmp_signal_key(db, v[i], v[j]) == 0)
            j++;
        size_t others = j - i - 1;
        uint16_t count = others > UINT16_MAX ? UINT16_MAX : (uint16_t)others;
        for(size_t k = i; k < j && others; k++) {
            db->entries[v[k]].flags |= RG_DB_FLAG_DUPLICATE;
            db->entries[v[k]].dup_count = count;
        }
        i = j;
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
        if(e->kind != RgDbKindProtocol || rg_db_casecmp(rg_db_protocol(db, e), q->protocol) != 0)
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
        c = rg_db_casecmp(rg_db_protocol(db, a), rg_db_protocol(db, b));
        break;
    }
    default:
        break;
    }
    if(c == 0) c = rg_db_casecmp(rg_db_name(db, a), rg_db_name(db, b));
    return c;
}

static int rg_db_cmp_view(const RgDb* db, uint16_t a, uint16_t b, int sort) {
    int c = rg_db_compare(db, &db->entries[a], &db->entries[b], (RgDbSort)sort);
    return c ? c : rg_db_cmp_u32(a, b);
}

size_t rg_db_select(const RgDb* db, const RgDbQuery* q, uint16_t* out, size_t max) {
    size_t n = 0;
    for(size_t i = 0; i < db->count && n < max; i++) {
        if(rg_db_shown(db, &db->entries[i], q)) out[n++] = (uint16_t)i;
    }
    // Heap sort with the index as the last tie-break: the same order the
    // stable insertion sort before beta 10 gave, in O(n log n) compares (it
    // runs on every sort, filter and search change in the list).
    rg_db_heapsort(db, out, n, rg_db_cmp_view, (int)q->sort);
    return n;
}

size_t rg_db_protocols(const RgDb* db, const char** out, size_t max) {
    size_t n = 0;
    for(size_t i = 0; i < db->count; i++) {
        const RgDbEntry* e = &db->entries[i];
        if(e->kind != RgDbKindProtocol) continue;
        const char* p = rg_db_protocol(db, e);
        bool seen = false;
        for(size_t k = 0; k < n && !seen; k++)
            seen = rg_db_casecmp(out[k], p) == 0;
        if(seen || n >= max) continue;
        // Insert in order.
        size_t j = n++;
        while(j > 0 && rg_db_casecmp(out[j - 1], p) > 0) {
            out[j] = out[j - 1];
            j--;
        }
        out[j] = p;
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
