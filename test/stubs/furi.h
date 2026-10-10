/**
 * Minimal host stand-ins for the Furi API used by helpers/radiogeddon_recorder.c
 * and helpers/radiogeddon_db.c, so they can be tested on a PC (test_recorder.c,
 * test_dbload.c). Threads are pthreads; ticks are milliseconds; thread flags
 * are a mutex and a condition variable. Only what those modules call is
 * provided. With STUB_TRACK_ALLOC, malloc/calloc/free in the code under test
 * are counted (live and peak bytes), so tests can check memory lifecycles.
 */
#pragma once

#define _POSIX_C_SOURCE 200809L
#include <pthread.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define FURI_LOG_E(tag, ...) \
    do {                     \
        (void)(tag);         \
    } while(0)
#define FURI_LOG_W FURI_LOG_E
#define FURI_LOG_I FURI_LOG_E

typedef int32_t (*FuriThreadCallback)(void* context);

typedef struct FuriThread {
    pthread_t thread;
    FuriThreadCallback callback;
    void* context;
    pthread_mutex_t lock;
    pthread_cond_t cond;
    uint32_t flags;
} FuriThread;

typedef FuriThread* FuriThreadId;

typedef enum {
    FuriFlagWaitAny = 0,
} FuriFlag;

FuriThread* furi_thread_alloc_ex(
    const char* name,
    uint32_t stack_size,
    FuriThreadCallback callback,
    void* context);
void furi_thread_start(FuriThread* thread);
bool furi_thread_join(FuriThread* thread);
void furi_thread_free(FuriThread* thread);
FuriThreadId furi_thread_get_id(FuriThread* thread);
uint32_t furi_thread_flags_set(FuriThreadId id, uint32_t flags);
uint32_t furi_thread_flags_wait(uint32_t flags, uint32_t options, uint32_t timeout);

uint32_t furi_get_tick(void);
uint32_t furi_kernel_get_tick_frequency(void);
#define furi_ms_to_ticks(ms) (ms)

size_t memmgr_heap_get_max_free_block(void);

/* Strings: just what the code under test uses. */
typedef struct FuriString FuriString;
FuriString* furi_string_alloc(void);
void furi_string_free(FuriString* s);
void furi_string_printf(FuriString* s, const char* format, ...);
const char* furi_string_get_cstr(const FuriString* s);

/* Test controls. */
extern size_t stub_heap_free; /* what memmgr_heap_get_max_free_block() reports */

#ifdef STUB_TRACK_ALLOC
void* stub_malloc(size_t size);
void* stub_calloc(size_t count, size_t size);
void stub_free(void* p);
#define malloc(n)    stub_malloc(n)
#define calloc(n, s) stub_calloc(n, s)
#define free(p)      stub_free(p)
extern size_t stub_live_bytes; /* allocated now, by the code under test */
extern size_t stub_live_blocks;
extern size_t stub_peak_bytes; /* highest stub_live_bytes since the last reset */
void stub_alloc_reset_peak(void);
#endif
