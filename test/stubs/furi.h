/**
 * Minimal host stand-ins for the Furi API used by helpers/radiogeddon_recorder.c,
 * so the recorder's writer thread can be tested on a PC (test/test_recorder.c).
 * Threads are pthreads; ticks are milliseconds; thread flags are a mutex and a
 * condition variable. Only what the recorder calls is provided.
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

/* Test controls. */
extern size_t stub_heap_free;
