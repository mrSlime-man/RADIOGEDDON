/**
 * Host-side tests for the streaming RAW recorder (helpers/radiogeddon_recorder.c)
 * with its writer thread, against the stub Furi/Storage layer in test/stubs:
 * pthreads for threads, host files for the SD card, and knobs to make the
 * "card" slow or failing. A producer thread stands in for the radio worker.
 * Every recording is read back with the RAW reader (helpers/rg_raw.c).
 *
 * Timing here is the host's, not the Flipper's: these tests show the logic
 * (no blocking, ordering, loss accounting, error handling), not throughput.
 * Build & run via `make -C test check`. All samples are synthetic.
 */
#include "furi.h"
#include "storage/storage.h"
#include "../helpers/radiogeddon_recorder.h"
#include "../helpers/rg_raw.h"

#include <time.h>

static int g_failures = 0;
static int g_checks = 0;

#define CHECK(cond, msg)                                             \
    do {                                                             \
        g_checks++;                                                  \
        if(!(cond)) {                                                \
            g_failures++;                                            \
            printf("  FAIL: %s (%s:%d)\n", msg, __FILE__, __LINE__); \
        }                                                            \
    } while(0)

#define REC_PATH "build/recorder_test.sub"

static void sleep_us(unsigned us) {
    struct timespec ts = {us / 1000000u, (long)(us % 1000000u) * 1000L};
    nanosleep(&ts, NULL);
}

static void reset_stubs(void) {
    stub_heap_free = 64 * 1024;
    stub_open_fails = false;
    stub_write_delay_us = 0;
    stub_write_fail_after = -1;
    stub_write_calls = 0;
}

/* ---- Radio stand-in ------------------------------------------------------ */

typedef struct {
    RadioGeddonRecorder* rec;
    uint32_t count;
    uint32_t pause_every; /* 0 = flat out */
    unsigned pause_us;
    uint64_t push_ns_max; /* longest single push */
} Radio;

static uint64_t now_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000u + (uint64_t)ts.tv_nsec;
}

/* Sample i has duration i + 1 and alternating level, so order is checkable. */
static void* radio_thread(void* arg) {
    Radio* r = arg;
    for(uint32_t i = 0; i < r->count; i++) {
        uint64_t t0 = now_ns();
        radiogeddon_recorder_push(r->rec, (i & 1) == 0, i + 1);
        uint64_t dt = now_ns() - t0;
        if(dt > r->push_ns_max) r->push_ns_max = dt;
        if(r->pause_every && (i + 1) % r->pause_every == 0) sleep_us(r->pause_us);
    }
    return NULL;
}

static void run_radio(Radio* r) {
    pthread_t t;
    pthread_create(&t, NULL, radio_thread, r);
    pthread_join(t, NULL);
}

/* ---- Reading the result back ------------------------------------------- */

static size_t file_read(void* ctx, uint8_t* buf, size_t len) {
    return fread(buf, 1, len, (FILE*)ctx);
}

static bool file_seek(void* ctx, uint32_t offset) {
    return fseek((FILE*)ctx, (long)offset, SEEK_SET) == 0;
}

typedef struct {
    uint32_t count;
    bool ordered; /* durations strictly increasing, levels match the index parity */
    bool header_ok;
    bool corrupt;
    uint32_t lost;
} ReadBack;

static ReadBack read_back(void) {
    ReadBack rb = {0, true, false, false, 0};
    FILE* fp = fopen(REC_PATH, "rb");
    if(!fp) return rb;
    static const char expect[] = "Filetype: Flipper SubGhz RAW File\n"
                                 "Version: 1\n"
                                 "Frequency: 433920000\n"
                                 "Preset: FuriHalSubGhzPresetOok650Async\n"
                                 "Protocol: RAW\n"
                                 "RAW_Data: ";
    char head[sizeof(expect)] = {0};
    size_t hn = fread(head, 1, sizeof(expect) - 1, fp);
    rb.header_ok = hn == sizeof(expect) - 1 && memcmp(head, expect, hn) == 0;
    rewind(fp);
    static RgRawReader reader;
    RgRawSource src = {file_read, file_seek, fp};
    rg_raw_reader_init(&reader, src);
    int32_t chunk[64];
    int64_t last = 0;
    size_t n;
    while((n = rg_raw_reader_read(&reader, chunk, 64)) > 0) {
        for(size_t i = 0; i < n; i++) {
            int64_t mag = chunk[i] < 0 ? -(int64_t)chunk[i] : chunk[i];
            bool high = chunk[i] > 0;
            if(mag <= last || high != (((mag - 1) & 1) == 0)) rb.ordered = false;
            last = mag;
        }
        rb.count += (uint32_t)n;
    }
    rb.corrupt = reader.corrupt;
    rb.lost = reader.lost;
    fclose(fp);
    return rb;
}

static RadioGeddonRecorder* start(void) {
    RadioGeddonRecorder* rec = radiogeddon_recorder_alloc();
    if(!rec) return NULL;
    if(radiogeddon_recorder_open(
           rec, NULL, REC_PATH, 433920000, "FuriHalSubGhzPresetOok650Async") !=
       RadioGeddonRecordOk) {
        RadioGeddonRecordStats st;
        radiogeddon_recorder_finish(rec, &st);
        return NULL;
    }
    return rec;
}

/* ---- Tests --------------------------------------------------------------- */

static void test_stream(void) {
    printf("test_stream\n");
    reset_stubs();
    RadioGeddonRecorder* rec = start();
    CHECK(rec != NULL, "recorder starts");
    if(!rec) return;
    // About 32k samples/s: the radio worker's ceiling with its 30 us filter.
    Radio radio = {rec, 40000, 32, 1000, 0};
    run_radio(&radio);
    RadioGeddonRecordStats st;
    radiogeddon_recorder_finish(rec, &st);
    CHECK(st.error == RadioGeddonRecordOk, "no error");
    CHECK(st.samples == 40000 && st.lost == 0, "every sample kept at the radio's top rate");
    CHECK(st.buffer == 8192, "largest ring when the heap allows");
    ReadBack rb = read_back();
    CHECK(rb.header_ok, "firmware RAW header");
    CHECK(rb.count == 40000 && rb.ordered && !rb.corrupt, "file holds every sample in order");
    CHECK(rb.lost == 0, "no lost-sample comment");
}

static void test_slow_card(void) {
    printf("test_slow_card\n");
    reset_stubs();
    stub_heap_free = 22 * 1024; // room for the smallest ring only
    stub_write_delay_us = 20000; // 20 ms a write: far slower than the radio
    RadioGeddonRecorder* rec = start();
    CHECK(rec != NULL, "recorder starts with little heap");
    if(!rec) return;
    Radio radio = {rec, 60000, 64, 1000, 0};
    run_radio(&radio);
    RadioGeddonRecordStats live;
    radiogeddon_recorder_stats(rec, &live);
    RadioGeddonRecordStats st;
    radiogeddon_recorder_finish(rec, &st);
    CHECK(st.buffer == 1024, "minimum ring");
    CHECK(live.peak_pct == 100, "ring filled up");
    CHECK(st.lost > 0 && st.gaps > 1, "overruns counted as lost samples in several gaps");
    CHECK(st.samples + st.lost == 60000, "every sample accepted or counted lost");
    CHECK(radio.push_ns_max < 50000000u, "the radio never waited for the card");
    ReadBack rb = read_back();
    CHECK(rb.count == st.samples && rb.ordered && !rb.corrupt, "kept samples in order on file");
    CHECK(rb.lost == st.lost, "lost count written to the file and read back");
    printf(
        "  (%lu kept, %lu lost in %lu gaps, first after %lu; longest push %lu us)\n",
        (unsigned long)st.samples,
        (unsigned long)st.lost,
        (unsigned long)st.gaps,
        (unsigned long)st.first_gap,
        (unsigned long)(radio.push_ns_max / 1000u));
}

static void test_before_open(void) {
    printf("test_before_open\n");
    reset_stubs();
    RadioGeddonRecorder* rec = radiogeddon_recorder_alloc();
    CHECK(rec != NULL, "alloc");
    if(!rec) return;
    for(uint32_t i = 0; i < 100; i++)
        radiogeddon_recorder_push(rec, (i & 1) == 0, i + 1);
    radiogeddon_recorder_push(rec, true, 0); // no duration: skipped
    CHECK(
        radiogeddon_recorder_open(
            rec, NULL, REC_PATH, 433920000, "FuriHalSubGhzPresetOok650Async") ==
            RadioGeddonRecordOk,
        "open after capture started");
    RadioGeddonRecordStats st;
    radiogeddon_recorder_finish(rec, &st);
    ReadBack rb = read_back();
    CHECK(st.samples == 100 && rb.count == 100 && rb.ordered, "samples from before open kept");
}

static void test_idle_flush(void) {
    printf("test_idle_flush\n");
    reset_stubs();
    RadioGeddonRecorder* rec = start();
    if(!rec) return;
    for(uint32_t i = 0; i < 10; i++)
        radiogeddon_recorder_push(rec, (i & 1) == 0, i + 1);
    sleep_us(600000); // longer than the idle flush delay
    ReadBack mid = read_back();
    CHECK(mid.count == 10, "a quiet recording still reaches the card");
    RadioGeddonRecordStats st;
    radiogeddon_recorder_finish(rec, &st);
    CHECK(st.elapsed_ms >= 550, "elapsed time measured");
    CHECK(read_back().count == 10, "final file unchanged");
}

static void test_failures(void) {
    printf("test_failures\n");
    reset_stubs();
    stub_heap_free = 10 * 1024;
    CHECK(radiogeddon_recorder_alloc() == NULL, "too little heap: refused");

    reset_stubs();
    stub_open_fails = true;
    RadioGeddonRecorder* rec = radiogeddon_recorder_alloc();
    radiogeddon_recorder_push(rec, true, 100);
    CHECK(
        radiogeddon_recorder_open(rec, NULL, REC_PATH, 1, "x") == RadioGeddonRecordOpenFailed,
        "open failure reported");
    RadioGeddonRecordStats st;
    radiogeddon_recorder_finish(rec, &st);
    CHECK(st.samples == 1 && st.error == RadioGeddonRecordOk, "finish without a file is clean");

    reset_stubs();
    stub_write_fail_after = 3000;
    rec = start();
    CHECK(rec != NULL, "starts (header fits before the failure)");
    if(!rec) return;
    Radio radio = {rec, 50000, 128, 200, 0};
    run_radio(&radio);
    unsigned waited = 0;
    while(!radiogeddon_recorder_failed(rec) && waited++ < 100)
        sleep_us(10000);
    CHECK(radiogeddon_recorder_failed(rec), "write failure seen while recording");
    unsigned calls = stub_write_calls;
    radiogeddon_recorder_finish(rec, &st);
    CHECK(st.error == RadioGeddonRecordWriteFailed, "final status says the write failed");
    CHECK(stub_write_calls <= calls + 1, "no more writes attempted after the failure");
    CHECK(
        strcmp(radiogeddon_recorder_error_text(st.error), "SD card write failed") == 0,
        "error text");
}

int main(void) {
    test_stream();
    test_slow_card();
    test_before_open();
    test_idle_flush();
    test_failures();
    remove(REC_PATH);

    printf("\n%d checks, %d failures\n", g_checks, g_failures);
    if(g_failures) {
        printf("RECORDER TESTS FAILED\n");
        return 1;
    }
    printf("ALL RECORDER TESTS PASSED\n");
    return 0;
}
