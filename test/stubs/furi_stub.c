/* Host implementations of the stubs in test/stubs (see furi.h there). */
#define _POSIX_C_SOURCE 200809L
#include <dirent.h>
#include <ctype.h>
#include <errno.h>
#include <stdarg.h>
#include <sys/stat.h>
#include <time.h>

#include "furi.h"
#include "storage/storage.h"
#include "datetime/datetime.h"
#include "furi_hal_rtc.h"
#include <unistd.h>

size_t stub_heap_free = 64 * 1024;
bool stub_open_fails = false;
unsigned stub_write_delay_us = 0;
long stub_write_fail_after = -1;
unsigned stub_write_calls = 0;
unsigned stub_files_open = 0;
static long stub_written = 0;

/* ---- Allocation tracking ------------------------------------------------- */

#ifdef STUB_TRACK_ALLOC
size_t stub_live_bytes = 0;
size_t stub_live_blocks = 0;
size_t stub_peak_bytes = 0;
static pthread_mutex_t stub_alloc_lock = PTHREAD_MUTEX_INITIALIZER;

typedef union {
    size_t size;
    max_align_t align;
} StubBlock;

void* stub_malloc(size_t size) {
    StubBlock* b = (malloc)(sizeof(StubBlock) + size);
    if(!b) abort(); /* the firmware stops on a failed allocation too */
    b->size = size;
    pthread_mutex_lock(&stub_alloc_lock);
    stub_live_bytes += size;
    stub_live_blocks++;
    if(stub_live_bytes > stub_peak_bytes) stub_peak_bytes = stub_live_bytes;
    pthread_mutex_unlock(&stub_alloc_lock);
    return b + 1;
}

void* stub_calloc(size_t count, size_t size) {
    void* p = stub_malloc(count * size);
    memset(p, 0, count * size);
    return p;
}

void stub_free(void* p) {
    if(!p) return;
    StubBlock* b = (StubBlock*)p - 1;
    pthread_mutex_lock(&stub_alloc_lock);
    stub_live_bytes -= b->size;
    stub_live_blocks--;
    pthread_mutex_unlock(&stub_alloc_lock);
    (free)(b);
}

void stub_alloc_reset_peak(void) {
    pthread_mutex_lock(&stub_alloc_lock);
    stub_peak_bytes = stub_live_bytes;
    pthread_mutex_unlock(&stub_alloc_lock);
}
#endif

/* ---- Strings ------------------------------------------------------------- */

struct FuriString {
    char* text;
    size_t len;
    size_t cap;
};

/* Room for @p need characters plus the terminator (no realloc, so the
 * allocation counter sees every block). */
static void stub_string_reserve(FuriString* s, size_t need) {
    if(need + 1 <= s->cap) return;
    size_t cap = s->cap * 2 > need + 1 ? s->cap * 2 : need + 1;
    char* text = malloc(cap);
    memcpy(text, s->text, s->len + 1);
    free(s->text);
    s->text = text;
    s->cap = cap;
}

FuriString* furi_string_alloc(void) {
    FuriString* s = malloc(sizeof(FuriString));
    s->cap = 16;
    s->len = 0;
    s->text = malloc(s->cap);
    s->text[0] = '\0';
    return s;
}

FuriString*(furi_string_alloc_set_str)(const char cstr[]) {
    FuriString* s = furi_string_alloc();
    furi_string_set_str(s, cstr);
    return s;
}

FuriString*(furi_string_alloc_set)(const FuriString* source) {
    return furi_string_alloc_set_str(source->text);
}

FuriString* furi_string_alloc_vprintf(const char format[], va_list args) {
    FuriString* s = furi_string_alloc();
    va_list copy;
    va_copy(copy, args);
    int n = vsnprintf(NULL, 0, format, copy);
    va_end(copy);
    if(n < 0) n = 0;
    stub_string_reserve(s, (size_t)n);
    vsnprintf(s->text, s->cap, format, args);
    s->len = (size_t)n;
    return s;
}

FuriString* furi_string_alloc_printf(const char format[], ...) {
    va_list args;
    va_start(args, format);
    FuriString* s = furi_string_alloc_vprintf(format, args);
    va_end(args);
    return s;
}

void furi_string_free(FuriString* s) {
    free(s->text);
    free(s);
}

int furi_string_printf(FuriString* s, const char* format, ...) {
    va_list args;
    va_start(args, format);
    int n = vsnprintf(NULL, 0, format, args);
    va_end(args);
    if(n < 0) n = 0;
    stub_string_reserve(s, (size_t)n);
    va_start(args, format);
    vsnprintf(s->text, s->cap, format, args);
    va_end(args);
    s->len = (size_t)n;
    return n;
}

const char* furi_string_get_cstr(const FuriString* s) {
    return s->text;
}

size_t furi_string_size(const FuriString* s) {
    return s->len;
}

void furi_string_reset(FuriString* s) {
    s->len = 0;
    s->text[0] = '\0';
}

void furi_string_push_back(FuriString* s, char c) {
    stub_string_reserve(s, s->len + 1);
    s->text[s->len++] = c;
    s->text[s->len] = '\0';
}

char furi_string_get_char(const FuriString* s, size_t index) {
    furi_check(index < s->len);
    return s->text[index];
}

void furi_string_set_char(FuriString* s, size_t index, const char c) {
    furi_check(index < s->len);
    s->text[index] = c;
}

void(furi_string_set_str)(FuriString* s, const char cstr[]) {
    size_t n = strlen(cstr);
    stub_string_reserve(s, n);
    memmove(s->text, cstr, n + 1);
    s->len = n;
}

void(furi_string_set)(FuriString* s, FuriString* source) {
    if(s != source) furi_string_set_str(s, source->text);
}

void(furi_string_cat_str)(FuriString* s, const char cstr[]) {
    size_t n = strlen(cstr);
    stub_string_reserve(s, s->len + n);
    memcpy(s->text + s->len, cstr, n + 1);
    s->len += n;
}

void(furi_string_cat)(FuriString* s, const FuriString* other) {
    furi_string_cat_str(s, other->text);
}

int(furi_string_cmp_str)(const FuriString* a, const char cstr[]) {
    return strcmp(a->text, cstr);
}

int(furi_string_cmp)(const FuriString* a, const FuriString* b) {
    return strcmp(a->text, b->text);
}

int(furi_string_cmpi_str)(const FuriString* a, const char cstr[]) {
    const unsigned char* x = (const unsigned char*)a->text;
    const unsigned char* y = (const unsigned char*)cstr;
    while(*x && tolower(*x) == tolower(*y)) {
        x++;
        y++;
    }
    return tolower(*x) - tolower(*y);
}

int(furi_string_cmpi)(const FuriString* a, const FuriString* b) {
    return furi_string_cmpi_str(a, b->text);
}

bool(furi_string_equal_str)(const FuriString* a, const char cstr[]) {
    return strcmp(a->text, cstr) == 0;
}

bool(furi_string_equal)(const FuriString* a, const FuriString* b) {
    return strcmp(a->text, b->text) == 0;
}

size_t furi_string_search_str(const FuriString* s, const char needle[], size_t start) {
    if(start > s->len) return FURI_STRING_FAILURE;
    const char* hit = strstr(s->text + start, needle);
    return hit ? (size_t)(hit - s->text) : FURI_STRING_FAILURE;
}

void furi_string_replace_at(FuriString* s, size_t pos, size_t len, const char replace[]) {
    furi_check(pos <= s->len && len <= s->len - pos);
    size_t n = strlen(replace);
    size_t tail = s->len - pos - len;
    stub_string_reserve(s, s->len - len + n);
    memmove(s->text + pos + n, s->text + pos + len, tail + 1);
    memcpy(s->text + pos, replace, n);
    s->len = s->len - len + n;
}

void furi_string_left(FuriString* s, size_t index) {
    if(index < s->len) {
        s->len = index;
        s->text[index] = '\0';
    }
}

void datetime_timestamp_to_datetime(uint32_t timestamp, DateTime* dt) {
    time_t t = (time_t)timestamp;
    struct tm tm;
    gmtime_r(&t, &tm);
    dt->hour = (uint8_t)tm.tm_hour;
    dt->minute = (uint8_t)tm.tm_min;
    dt->second = (uint8_t)tm.tm_sec;
    dt->day = (uint8_t)tm.tm_mday;
    dt->month = (uint8_t)(tm.tm_mon + 1);
    dt->year = (uint16_t)(tm.tm_year + 1900);
    dt->weekday = (uint8_t)(tm.tm_wday == 0 ? 7 : tm.tm_wday);
}

FuriThread* furi_thread_alloc_ex(
    const char* name,
    uint32_t stack_size,
    FuriThreadCallback callback,
    void* context) {
    (void)name;
    (void)stack_size;
    FuriThread* t = calloc(1, sizeof(FuriThread));
    t->callback = callback;
    t->context = context;
    pthread_mutex_init(&t->lock, NULL);
    pthread_cond_init(&t->cond, NULL);
    return t;
}

static _Thread_local FuriThread* stub_current;

static void* stub_thread_entry(void* arg) {
    FuriThread* t = arg;
    stub_current = t;
    t->callback(t->context);
    return NULL;
}

void furi_thread_start(FuriThread* t) {
    pthread_create(&t->thread, NULL, stub_thread_entry, t);
}

bool furi_thread_join(FuriThread* t) {
    return pthread_join(t->thread, NULL) == 0;
}

void furi_thread_free(FuriThread* t) {
    pthread_mutex_destroy(&t->lock);
    pthread_cond_destroy(&t->cond);
    free(t);
}

FuriThreadId furi_thread_get_id(FuriThread* t) {
    return t;
}

uint32_t furi_thread_flags_set(FuriThreadId t, uint32_t flags) {
    pthread_mutex_lock(&t->lock);
    t->flags |= flags;
    pthread_cond_broadcast(&t->cond);
    pthread_mutex_unlock(&t->lock);
    return flags;
}

uint32_t furi_thread_flags_wait(uint32_t flags, uint32_t options, uint32_t timeout) {
    (void)options;
    FuriThread* t = stub_current;
    struct timespec until;
    clock_gettime(CLOCK_REALTIME, &until);
    until.tv_nsec += (long)(timeout % 1000) * 1000000L;
    until.tv_sec += timeout / 1000 + until.tv_nsec / 1000000000L;
    until.tv_nsec %= 1000000000L;
    pthread_mutex_lock(&t->lock);
    while(!(t->flags & flags)) {
        if(pthread_cond_timedwait(&t->cond, &t->lock, &until) == ETIMEDOUT) break;
    }
    uint32_t got = t->flags & flags;
    t->flags &= ~flags;
    pthread_mutex_unlock(&t->lock);
    return got;
}

uint32_t furi_get_tick(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint32_t)(ts.tv_sec * 1000u + ts.tv_nsec / 1000000);
}

uint32_t furi_kernel_get_tick_frequency(void) {
    return 1000;
}

size_t memmgr_heap_get_max_free_block(void) {
    return stub_heap_free;
}

File* storage_file_alloc(Storage* storage) {
    (void)storage;
    return calloc(1, sizeof(File));
}

static bool stub_exists(const char* path) {
    struct stat st;
    return stat(path, &st) == 0;
}

bool storage_file_open(File* file, const char* path, FS_AccessMode access, FS_OpenMode mode) {
    if(stub_open_fails) return false;
    bool exists = stub_exists(path);
    const char* how = NULL;
    switch(mode) {
    case FSOM_OPEN_EXISTING:
        if(exists) how = access == FSAM_READ ? "rb" : "r+b";
        break;
    case FSOM_OPEN_ALWAYS:
        if(!exists) {
            FILE* f = fopen(path, "wb");
            if(f) fclose(f);
        }
        how = access == FSAM_READ ? "rb" : "r+b";
        break;
    case FSOM_OPEN_APPEND:
        how = "a+b";
        break;
    case FSOM_CREATE_NEW:
        if(!exists) how = "w+b";
        break;
    case FSOM_CREATE_ALWAYS:
        how = "w+b";
        break;
    }
    file->fp = how ? fopen(path, how) : NULL;
    if(!file->fp) return false;
    if(access & FSAM_WRITE) stub_written = 0;
    stub_files_open++;
    return true;
}

bool storage_file_is_open(File* file) {
    return file->fp != NULL;
}

bool storage_file_seek(File* file, uint32_t offset, bool from_start) {
    if(!file->fp) return false;
    return fseeko(file->fp, (off_t)offset, from_start ? SEEK_SET : SEEK_CUR) == 0;
}

uint64_t storage_file_tell(File* file) {
    return file->fp ? (uint64_t)ftello(file->fp) : 0;
}

bool storage_file_truncate(File* file) {
    if(!file->fp) return false;
    fflush(file->fp);
    return ftruncate(fileno(file->fp), ftello(file->fp)) == 0;
}

uint64_t storage_file_size(File* file) {
    if(!file->fp) return 0;
    fflush(file->fp);
    struct stat st;
    return fstat(fileno(file->fp), &st) == 0 ? (uint64_t)st.st_size : 0;
}

bool storage_file_sync(File* file) {
    return file->fp && fflush(file->fp) == 0;
}

bool storage_file_eof(File* file) {
    return !file->fp || storage_file_tell(file) >= storage_file_size(file);
}

FS_Error storage_common_remove(Storage* storage, const char* path) {
    (void)storage;
    if(remove(path) == 0) return FSE_OK;
    return errno == ENOENT ? FSE_NOT_EXIST : FSE_INTERNAL;
}

bool stub_rename_keeps_target = false;

FS_Error storage_common_rename(Storage* storage, const char* old_path, const char* new_path) {
    (void)storage;
    if(!stub_exists(old_path)) return FSE_NOT_EXIST;
    if(stub_rename_keeps_target && stub_exists(new_path)) return FSE_EXIST;
    return rename(old_path, new_path) == 0 ? FSE_OK : FSE_INTERNAL;
}

FS_Error storage_common_mkdir(Storage* storage, const char* path) {
    (void)storage;
    if(mkdir(path, 0777) == 0) return FSE_OK;
    return errno == EEXIST ? FSE_EXIST : FSE_INTERNAL;
}

bool storage_common_exists(Storage* storage, const char* path) {
    (void)storage;
    return stub_exists(path);
}

void storage_get_next_filename(
    Storage* storage,
    const char* dirname,
    const char* filename,
    const char* fileextension,
    FuriString* nextfilename,
    uint8_t max_len) {
    (void)max_len;
    char path[512];
    furi_string_set_str(nextfilename, filename);
    for(unsigned i = 1; i < 1000; i++) {
        snprintf(
            path,
            sizeof(path),
            "%s/%s%s",
            dirname,
            furi_string_get_cstr(nextfilename),
            fileextension);
        if(!storage_common_exists(storage, path)) return;
        furi_string_printf(nextfilename, "%s%u", filename, i);
    }
}

DateTime stub_datetime = {12, 34, 56, 9, 10, 2026, 5};

void furi_hal_rtc_get_datetime(DateTime* datetime) {
    *datetime = stub_datetime;
}

size_t storage_file_read(File* file, void* buf, size_t size) {
    if(!file->fp) return 0;
    size_t n = fread(buf, 1, size, file->fp);
    if(ferror(file->fp)) file->error = true;
    return n;
}

FS_Error storage_file_get_error(File* file) {
    return file->error ? FSE_INTERNAL : FSE_OK;
}

size_t storage_file_write(File* file, const void* buf, size_t size) {
    stub_write_calls++;
    if(stub_write_delay_us) {
        struct timespec ts = {0, (long)stub_write_delay_us * 1000L};
        nanosleep(&ts, NULL);
    }
    size_t n = size;
    if(stub_write_fail_after >= 0 && stub_written + (long)n > stub_write_fail_after) {
        n = stub_written < stub_write_fail_after ? (size_t)(stub_write_fail_after - stub_written) :
                                                   0;
    }
    n = fwrite(buf, 1, n, file->fp);
    fflush(file->fp); // visible to a reader at once, like the card
    stub_written += (long)n;
    return n;
}

bool storage_file_close(File* file) {
    if(file->fp) stub_files_open--;
    bool ok = file->fp && fclose(file->fp) == 0;
    file->fp = NULL;
    return ok;
}

bool storage_dir_open(File* file, const char* path) {
    if(stub_open_fails) return false;
    file->dir = opendir(path);
    if(!file->dir) return false;
    snprintf(file->dir_path, sizeof(file->dir_path), "%s", path);
    stub_files_open++;
    return true;
}

bool storage_dir_read(File* file, FileInfo* info, char* name, uint16_t name_length) {
    if(!file->dir) return false;
    struct dirent* d;
    while((d = readdir((DIR*)file->dir)) != NULL) {
        if(strcmp(d->d_name, ".") == 0 || strcmp(d->d_name, "..") == 0) continue;
        char full[512];
        snprintf(full, sizeof(full), "%s/%s", file->dir_path, d->d_name);
        struct stat st;
        if(stat(full, &st) != 0) continue;
        info->flags = S_ISDIR(st.st_mode) ? FSF_DIRECTORY : 0;
        info->size = S_ISDIR(st.st_mode) ? 0 : (uint64_t)st.st_size;
        snprintf(name, name_length, "%s", d->d_name);
        return true;
    }
    return false;
}

bool storage_dir_close(File* file) {
    if(!file->dir) return false;
    closedir((DIR*)file->dir);
    file->dir = NULL;
    stub_files_open--;
    return true;
}

bool file_info_is_dir(const FileInfo* info) {
    return (info->flags & FSF_DIRECTORY) != 0;
}

FS_Error storage_common_timestamp(Storage* storage, const char* path, uint32_t* timestamp) {
    (void)storage;
    struct stat st;
    if(stat(path, &st) != 0) return FSE_NOT_EXIST;
    *timestamp = (uint32_t)st.st_mtime;
    return FSE_OK;
}

void storage_file_free(File* file) {
    /* As in the firmware: freeing an open file closes it. */
    if(file->fp) storage_file_close(file);
    if(file->dir) storage_dir_close(file);
    free(file);
}
