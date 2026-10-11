#include "rg_session.h"
#include "../radiogeddon_edition.h"

#if RG_FEATURE_SESSIONS

#include <stdio.h>
#include <string.h>

#define RG_SESSION_HEADER "# RadioGeddon session"

static void rg_session_copy(char* dst, size_t size, const char* src, size_t len) {
    if(len >= size) len = size - 1;
    memcpy(dst, src, len);
    dst[len] = '\0';
}

void rg_session_init(RgSession* s, const char* name, const char* created) {
    memset(s, 0, sizeof(*s));
    if(name) rg_session_copy(s->name, sizeof(s->name), name, strlen(name));
    if(created) rg_session_copy(s->created, sizeof(s->created), created, strlen(created));
}

bool rg_session_name_valid(const char* name) {
    size_t n = strlen(name);
    if(n == 0 || n >= RG_SESSION_NAME_MAX) return false;
    bool ink = false;
    for(size_t i = 0; i < n; i++) {
        unsigned char c = (unsigned char)name[i];
        if(c < 0x20 || c > 0x7E || strchr("/\\:*?\"<>|", c)) return false;
        if(c != ' ') ink = true;
    }
    return ink && name[0] != '.';
}

bool rg_session_signal_valid(const char* file) {
    size_t n = strlen(file);
    if(n < 5 || n >= RG_SESSION_SIGNAL_MAX || strcmp(file + n - 4, ".sub") != 0) return false;
    for(size_t i = 0; i < n; i++) {
        unsigned char c = (unsigned char)file[i];
        if(c < 0x20 || c == '/' || c == '\\') return false;
    }
    return true;
}

int rg_session_find(const RgSession* s, const char* file) {
    for(size_t i = 0; i < s->count; i++)
        if(strcmp(s->signal[i], file) == 0) return (int)i;
    return -1;
}

RgSessionAdd rg_session_add(RgSession* s, const char* file) {
    if(!rg_session_signal_valid(file)) return RgSessionAddInvalid;
    if(rg_session_find(s, file) >= 0) return RgSessionAddAlready;
    if(s->count >= RG_SESSION_SIGNALS) return RgSessionAddFull;
    rg_session_copy(s->signal[s->count], RG_SESSION_SIGNAL_MAX, file, strlen(file));
    s->count++;
    return RgSessionAddOk;
}

bool rg_session_remove(RgSession* s, const char* file) {
    int i = rg_session_find(s, file);
    if(i < 0) return false;
    for(size_t k = (size_t)i; k + 1 < s->count; k++)
        memcpy(s->signal[k], s->signal[k + 1], RG_SESSION_SIGNAL_MAX);
    s->count--;
    memset(s->signal[s->count], 0, RG_SESSION_SIGNAL_MAX);
    return true;
}

bool rg_session_rename_signal(RgSession* s, const char* from, const char* to) {
    int i = rg_session_find(s, from);
    if(i < 0 || !rg_session_signal_valid(to)) return false;
    if(rg_session_find(s, to) >= 0) {
        // Both names listed: keep one entry.
        rg_session_remove(s, from);
        return true;
    }
    rg_session_copy(s->signal[i], RG_SESSION_SIGNAL_MAX, to, strlen(to));
    return true;
}

size_t rg_session_write(const RgSession* s, char* out, size_t size) {
    size_t len = 0;
    int n = snprintf(
        out,
        size,
        RG_SESSION_HEADER "\nVersion: %u\nName: %s\n",
        (unsigned)RG_SESSION_VERSION,
        s->name);
    if(n < 0 || (size_t)n >= size) return 0;
    len = (size_t)n;
    if(s->created[0]) {
        n = snprintf(out + len, size - len, "Created: %s\n", s->created);
        if(n < 0 || (size_t)n >= size - len) return 0;
        len += (size_t)n;
    }
    for(size_t i = 0; i < s->count; i++) {
        n = snprintf(out + len, size - len, "Signal: %s\n", s->signal[i]);
        if(n < 0 || (size_t)n >= size - len) return 0;
        len += (size_t)n;
    }
    return len;
}

/* "Key: value" -> value (trimmed), or NULL if the line has another key. */
static const char* rg_session_value(const char* line, size_t len, const char* key, size_t* vlen) {
    size_t k = strlen(key);
    if(len < k + 1 || memcmp(line, key, k) != 0 || line[k] != ':') return NULL;
    const char* v = line + k + 1;
    const char* end = line + len;
    while(v < end && (*v == ' ' || *v == '\t'))
        v++;
    while(end > v && (end[-1] == ' ' || end[-1] == '\t' || end[-1] == '\r'))
        end--;
    *vlen = (size_t)(end - v);
    return v;
}

RgSessionParse rg_session_parse(RgSession* s, const char* text, size_t len) {
    rg_session_init(s, NULL, NULL);
    bool header = false;
    size_t pos = 0;
    while(pos < len) {
        size_t end = pos;
        while(end < len && text[end] != '\n')
            end++;
        const char* line = text + pos;
        size_t n = end - pos;
        if(n && line[n - 1] == '\r') n--;
        size_t vlen = 0;
        const char* name = header ? rg_session_value(line, n, "Name", &vlen) : NULL;
        const char* created = header && !name ? rg_session_value(line, n, "Created", &vlen) : NULL;
        const char* v = header && !name && !created ? rg_session_value(line, n, "Signal", &vlen) :
                                                      NULL;
        if(!header) {
            header = n == strlen(RG_SESSION_HEADER) && memcmp(line, RG_SESSION_HEADER, n) == 0;
            if(!header) return RgSessionParseNotSession;
        } else if(name) {
            rg_session_copy(s->name, sizeof(s->name), name, vlen);
        } else if(created) {
            rg_session_copy(s->created, sizeof(s->created), created, vlen);
        } else if(v) {
            char file[RG_SESSION_SIGNAL_MAX];
            if(vlen < sizeof(file)) {
                rg_session_copy(file, sizeof(file), v, vlen);
                rg_session_add(s, file); // invalid, duplicate or overflow: skipped
            }
        }
        pos = end + 1;
    }
    if(!header) return RgSessionParseNotSession;
    return rg_session_name_valid(s->name) ? RgSessionParseOk : RgSessionParseNoName;
}

/* ---- transactional save ---------------------------------------------------- */

static void rg_session_path(char* out, size_t size, const char* path, const char* ext) {
    snprintf(out, size, "%s%s", path, ext);
}

bool rg_session_save(const RgSessionFs* fs, const char* path, const RgSession* s, char* scratch) {
    char tmp[128], bak[128];
    rg_session_path(tmp, sizeof(tmp), path, ".tmp");
    rg_session_path(bak, sizeof(bak), path, ".bak");
    char* text = scratch;
    char* back = scratch + RG_SESSION_TEXT_MAX;
    size_t len = rg_session_write(s, text, RG_SESSION_TEXT_MAX);
    if(len == 0) return false;
    // 1. The new text, complete and read back, beside the old file.
    if(fs->exists(fs->ctx, tmp)) fs->remove(fs->ctx, tmp);
    if(!fs->write(fs->ctx, tmp, text, len)) {
        fs->remove(fs->ctx, tmp);
        return false;
    }
    long got = fs->read(fs->ctx, tmp, back, RG_SESSION_TEXT_MAX);
    if(got != (long)len || memcmp(back, text, len) != 0) {
        fs->remove(fs->ctx, tmp);
        return false;
    }
    // 2. Old file to the backup, new one into place, backup gone.
    if(fs->exists(fs->ctx, bak)) fs->remove(fs->ctx, bak);
    bool had_old = fs->exists(fs->ctx, path);
    if(had_old && !fs->rename(fs->ctx, path, bak)) {
        fs->remove(fs->ctx, tmp);
        return false;
    }
    if(!fs->rename(fs->ctx, tmp, path)) {
        // Put the old file back; if even that fails, load() restores it.
        if(had_old) fs->rename(fs->ctx, bak, path);
        fs->remove(fs->ctx, tmp);
        return false;
    }
    if(had_old) fs->remove(fs->ctx, bak);
    return true;
}

static bool
    rg_session_read_file(const RgSessionFs* fs, const char* path, RgSession* s, char* scratch) {
    long n = fs->read(fs->ctx, path, scratch, RG_SESSION_TEXT_MAX);
    if(n <= 0) return false;
    return rg_session_parse(s, scratch, (size_t)n) == RgSessionParseOk;
}

RgSessionLoad
    rg_session_load(const RgSessionFs* fs, const char* path, RgSession* s, char* scratch) {
    char tmp[128], bak[128];
    rg_session_path(tmp, sizeof(tmp), path, ".tmp");
    rg_session_path(bak, sizeof(bak), path, ".bak");
    // A .tmp is only ever a save that did not finish: the file or the backup
    // holds the last complete version.
    if(fs->exists(fs->ctx, tmp)) fs->remove(fs->ctx, tmp);
    bool have = fs->exists(fs->ctx, path);
    if(have && rg_session_read_file(fs, path, s, scratch)) {
        if(fs->exists(fs->ctx, bak)) fs->remove(fs->ctx, bak);
        return RgSessionLoadOk;
    }
    if(fs->exists(fs->ctx, bak) && rg_session_read_file(fs, bak, s, scratch)) {
        if(have) fs->remove(fs->ctx, path);
        if(fs->rename(fs->ctx, bak, path)) return RgSessionLoadRecovered;
        return RgSessionLoadRecovered; // read from the backup even if it stays there
    }
    rg_session_init(s, NULL, NULL);
    return have ? RgSessionLoadBad : RgSessionLoadMissing;
}

/* ---- grouping suggestions ---------------------------------------------------- */

#define RG_SESSION_OPEN_GROUPS 32u

typedef struct {
    uint32_t frequency;
    uint32_t last_time;
    uint16_t first;
    uint16_t bits;
    uint8_t count;
    const char* protocol;
} RgSessionWork;

static bool rg_session_joins(const RgSessionWork* g, const RgSessionItem* it) {
    uint32_t df = it->frequency > g->frequency ? it->frequency - g->frequency :
                                                 g->frequency - it->frequency;
    if(!it->frequency || df > RG_SESSION_GROUP_HZ) return false;
    if(strcmp(it->protocol ? it->protocol : "", g->protocol ? g->protocol : "") != 0) return false;
    if(it->bits != g->bits) return false;
    if(it->mtime && g->last_time) {
        uint32_t dt = it->mtime > g->last_time ? it->mtime - g->last_time :
                                                 g->last_time - it->mtime;
        if(dt > RG_SESSION_GROUP_SECONDS) return false;
    }
    return true;
}

size_t rg_session_suggest(
    const RgSessionItem* items,
    size_t count,
    int8_t* group_of,
    RgSessionGroup* groups) {
    RgSessionWork work[RG_SESSION_OPEN_GROUPS];
    size_t open = 0;
    for(size_t i = 0; i < count; i++) {
        group_of[i] = -1;
        const RgSessionItem* it = &items[i];
        if(!it->frequency) continue; // no frequency: nothing to group on
        size_t g;
        for(g = 0; g < open; g++)
            if(rg_session_joins(&work[g], it)) break;
        if(g == open) {
            if(open == RG_SESSION_OPEN_GROUPS) continue;
            work[open].frequency = it->frequency;
            work[open].first = (uint16_t)i;
            work[open].bits = it->bits;
            work[open].count = 0;
            work[open].last_time = 0;
            work[open].protocol = it->protocol ? it->protocol : "";
            open++;
        }
        work[g].last_time = it->mtime ? it->mtime : work[g].last_time;
        if(work[g].count < UINT8_MAX) work[g].count++;
        group_of[i] = (int8_t)g;
    }
    // Keep groups of two or more, largest first (stable), at most the limit.
    int8_t remap[RG_SESSION_OPEN_GROUPS];
    for(size_t g = 0; g < open; g++)
        remap[g] = -1;
    size_t out = 0;
    for(unsigned size = 255; size >= 2 && out < RG_SESSION_MAX_GROUPS; size--) {
        for(size_t g = 0; g < open && out < RG_SESSION_MAX_GROUPS; g++) {
            if(work[g].count != size) continue;
            RgSessionGroup* dst = &groups[out];
            dst->count = work[g].count;
            dst->first = work[g].first;
            dst->frequency = work[g].frequency;
            dst->bits = work[g].bits;
            rg_session_copy(
                dst->protocol, sizeof(dst->protocol), work[g].protocol, strlen(work[g].protocol));
            remap[g] = (int8_t)out++;
        }
    }
    for(size_t i = 0; i < count; i++)
        if(group_of[i] >= 0) group_of[i] = remap[(size_t)group_of[i]];
    return out;
}

#endif
