/**
 * Minimal host stand-ins for the Furi API used by helpers/radiogeddon_recorder.c
 * and helpers/radiogeddon_db.c, so they can be tested on a PC (test_recorder.c,
 * test_dbload.c), and for the firmware code that test_formats and
 * test_fwdecode build. Threads are pthreads; ticks are milliseconds; thread
 * flags are a mutex and a condition variable. Only what that code calls is
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

#include <assert.h>
#include "core/common_defines.h"

/* newlib's attribute macro, used in firmware headers. */
#ifndef _ATTRIBUTE
#define _ATTRIBUTE(attrs) __attribute__(attrs)
#endif

#define FURI_LOG_E(tag, ...) \
    do {                     \
        (void)(tag);         \
    } while(0)
#define FURI_LOG_W FURI_LOG_E
#define FURI_LOG_I FURI_LOG_E
#define FURI_LOG_D FURI_LOG_E
#define FURI_LOG_T FURI_LOG_E
#define FURI_LOG_RAW_D(...) \
    do {                    \
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

/* Strings: what the code under test and the firmware's FlipperFormat and
 * stream code (test_formats) use, with the firmware's semantics. */
#include <stdarg.h>
typedef struct FuriString FuriString;
#define FURI_STRING_FAILURE ((size_t) - 1)
FuriString* furi_string_alloc(void);
FuriString* furi_string_alloc_set(const FuriString* source);
FuriString* furi_string_alloc_set_str(const char cstr[]);
FuriString* furi_string_alloc_printf(const char format[], ...);
FuriString* furi_string_alloc_vprintf(const char format[], va_list args);
void furi_string_free(FuriString* s);
int furi_string_printf(FuriString* s, const char* format, ...);
const char* furi_string_get_cstr(const FuriString* s);
size_t furi_string_size(const FuriString* s);
void furi_string_reset(FuriString* s);
void furi_string_push_back(FuriString* s, char c);
char furi_string_get_char(const FuriString* s, size_t index);
void furi_string_set_char(FuriString* s, size_t index, const char c);
void furi_string_set(FuriString* s, FuriString* source);
void furi_string_set_str(FuriString* s, const char cstr[]);
void furi_string_cat(FuriString* s, const FuriString* other);
void furi_string_cat_str(FuriString* s, const char cstr[]);
int furi_string_cmp(const FuriString* a, const FuriString* b);
int furi_string_cmp_str(const FuriString* a, const char cstr[]);
int furi_string_cmpi(const FuriString* a, const FuriString* b);
int furi_string_cmpi_str(const FuriString* a, const char cstr[]);
bool furi_string_equal(const FuriString* a, const FuriString* b);
bool furi_string_equal_str(const FuriString* a, const char cstr[]);
size_t furi_string_search_str(const FuriString* s, const char needle[], size_t start);
void furi_string_replace_at(FuriString* s, size_t pos, size_t len, const char replace[]);
void furi_string_left(FuriString* s, size_t index);
void furi_string_set_n(FuriString* s, const FuriString* source, size_t offset, size_t length);
/* What the firmware's Sub-GHz decoders describe their decodes with
 * (test_fwdecode). As on the firmware, where long is 32 bits, a %lu, %ld or
 * %lX takes a 32-bit value: its code passes uint32_t and int32_t to them.
 * (Not marked as printf-like: the host compiler would flag every such use.) */
int furi_string_cat_printf(FuriString* s, const char format[], ...);
int furi_string_cat_vprintf(FuriString* s, const char format[], va_list args);

/* Records, delays and the free heap: only on paths the tests never take
 * (saving a RAW file, sending one); the stand-ins abort (subghz_stub.c). */
#define RECORD_STORAGE "storage"
void* furi_record_open(const char* name);
void furi_record_close(const char* name);
void furi_delay_ms(uint32_t milliseconds);
size_t memmgr_get_free_heap(void);

#define STUB_STR_SELECT(fs, cs, a, b) \
    _Generic((b), char*: cs, const char*: cs, FuriString*: fs, const FuriString*: fs)(a, b)
#define furi_string_alloc_set(a)                \
    _Generic(                                   \
        (a),                                    \
        char*: furi_string_alloc_set_str,       \
        const char*: furi_string_alloc_set_str, \
        FuriString*: furi_string_alloc_set,     \
        const FuriString*: furi_string_alloc_set)(a)
#define furi_string_set(a, b)   STUB_STR_SELECT(furi_string_set, furi_string_set_str, a, b)
#define furi_string_cat(a, b)   STUB_STR_SELECT(furi_string_cat, furi_string_cat_str, a, b)
#define furi_string_cmp(a, b)   STUB_STR_SELECT(furi_string_cmp, furi_string_cmp_str, a, b)
#define furi_string_cmpi(a, b)  STUB_STR_SELECT(furi_string_cmpi, furi_string_cmpi_str, a, b)
#define furi_string_equal(a, b) STUB_STR_SELECT(furi_string_equal, furi_string_equal_str, a, b)

/* Test controls. */
extern size_t stub_heap_free; /* what memmgr_heap_get_max_free_block() reports */

/* The firmware's heap hands out zeroed blocks (pvPortMalloc wipes them, in
 * furi/core/memmgr_heap.c) and its Sub-GHz decoders rely on it, starting from
 * an all-zero state. With STUB_FURI_HEAP (test_fwdecode) malloc does too. */
#ifdef STUB_FURI_HEAP
#ifdef STUB_TRACK_ALLOC
#error "STUB_FURI_HEAP and STUB_TRACK_ALLOC are exclusive"
#endif
#define malloc(n) calloc(1, (n))
#endif

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
