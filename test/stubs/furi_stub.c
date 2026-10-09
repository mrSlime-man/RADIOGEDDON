/* Host implementations of the stubs in test/stubs (see furi.h there). */
#include "furi.h"
#include "storage/storage.h"

#include <errno.h>
#include <time.h>

size_t stub_heap_free = 64 * 1024;
bool stub_open_fails = false;
unsigned stub_write_delay_us = 0;
long stub_write_fail_after = -1;
unsigned stub_write_calls = 0;
static long stub_written = 0;

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

bool storage_file_open(File* file, const char* path, FS_AccessMode access, FS_OpenMode mode) {
    (void)access;
    (void)mode;
    if(stub_open_fails) return false;
    file->fp = fopen(path, "wb");
    stub_written = 0;
    return file->fp != NULL;
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
    bool ok = file->fp && fclose(file->fp) == 0;
    file->fp = NULL;
    return ok;
}

void storage_file_free(File* file) {
    free(file);
}
