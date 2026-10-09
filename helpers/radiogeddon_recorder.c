#include "radiogeddon_recorder.h"
#include "rg_ring.h"
#include "rg_rawfmt.h"

#define TAG "RadioGeddonRec"

/* Ring size limits in samples (4 bytes each). */
#define RECORDER_RING_MIN   1024u
#define RECORDER_RING_MAX   8192u
/* Heap left free beyond the ring, write buffer and stack. */
#define RECORDER_HEAP_SPARE (12u * 1024u)
#define RECORDER_WBUF       2048u
#define RECORDER_CHUNK      64u
#define RECORDER_STACK      1536u
/* Writer wake-up period, and how long buffered text may wait when idle. */
#define RECORDER_POLL_MS    20u
#define RECORDER_IDLE_FLUSH 250u
#define RECORDER_FLAG_WAKE  (1u << 0)
/* The tail is formatted on the finishing thread: header-sized scratch. */
#define RECORDER_TEXT_MAX   192u

struct RadioGeddonRecorder {
    RgRing ring;
    int32_t* ring_buf;
    RgRawFmt fmt;
    char* wbuf;
    size_t wlen;
    int32_t chunk[RECORDER_CHUNK];

    File* file;
    FuriThread* thread;
    bool stop; /* set (atomically) once no more samples will arrive */
    bool failed; /* a write failed; set by the writer, read anywhere */

    uint32_t radio_overruns;
    uint32_t start_tick;
    uint32_t stop_tick;
};

static uint32_t radiogeddon_recorder_ms(uint32_t ticks) {
    return (uint32_t)((uint64_t)ticks * 1000u / furi_kernel_get_tick_frequency());
}

RadioGeddonRecorder* radiogeddon_recorder_alloc(void) {
    size_t avail = memmgr_heap_get_max_free_block();
    size_t fixed =
        sizeof(RadioGeddonRecorder) + RECORDER_WBUF + RECORDER_STACK + RECORDER_HEAP_SPARE;
    if(avail <= fixed) return NULL;
    uint32_t cap = rg_ring_capacity_for(avail - fixed, RECORDER_RING_MIN, RECORDER_RING_MAX);
    if(cap == 0) return NULL;

    RadioGeddonRecorder* rec = malloc(sizeof(RadioGeddonRecorder));
    memset(rec, 0, sizeof(RadioGeddonRecorder));
    rec->ring_buf = malloc(sizeof(int32_t) * cap);
    rec->wbuf = malloc(RECORDER_WBUF);
    rg_ring_init(&rec->ring, rec->ring_buf, cap);
    rg_rawfmt_init(&rec->fmt, RG_RAWFMT_LINE_VALUES);
    rec->start_tick = furi_get_tick();
    return rec;
}

static bool radiogeddon_recorder_flush(RadioGeddonRecorder* rec) {
    if(rec->wlen == 0) return true;
    bool ok = !__atomic_load_n(&rec->failed, __ATOMIC_ACQUIRE) &&
              storage_file_write(rec->file, rec->wbuf, rec->wlen) == rec->wlen;
    rec->wlen = 0;
    if(!ok) __atomic_store_n(&rec->failed, true, __ATOMIC_RELEASE);
    return ok;
}

static int32_t radiogeddon_recorder_writer(void* context) {
    RadioGeddonRecorder* rec = context;
    uint32_t last_write = furi_get_tick();
    while(true) {
        // Read the stop flag before popping: once it is set no push is
        // pending, so an empty pop after it means everything is drained.
        bool stopping = __atomic_load_n(&rec->stop, __ATOMIC_ACQUIRE);
        size_t n = rg_ring_pop(&rec->ring, rec->chunk, RECORDER_CHUNK);
        if(n > 0) {
            if(__atomic_load_n(&rec->failed, __ATOMIC_ACQUIRE)) continue; // drain only
            size_t done = 0;
            while(done < n) {
                done += rg_rawfmt_values(
                    &rec->fmt, rec->chunk + done, n - done, rec->wbuf, RECORDER_WBUF, &rec->wlen);
                if(RECORDER_WBUF - rec->wlen < RG_RAWFMT_VALUE_MAX) {
                    radiogeddon_recorder_flush(rec);
                    last_write = furi_get_tick();
                }
            }
            continue;
        }
        if(stopping) break;
        // Idle: let buffered text reach the card now and then, so a long quiet
        // stretch does not leave it all in RAM.
        if(rec->wlen > 0 &&
           furi_get_tick() - last_write >= furi_ms_to_ticks(RECORDER_IDLE_FLUSH)) {
            radiogeddon_recorder_flush(rec);
            last_write = furi_get_tick();
        }
        furi_thread_flags_wait(RECORDER_FLAG_WAKE, FuriFlagWaitAny, RECORDER_POLL_MS);
    }
    return 0;
}

RadioGeddonRecordError radiogeddon_recorder_open(
    RadioGeddonRecorder* rec,
    Storage* storage,
    const char* path,
    uint32_t frequency,
    const char* preset) {
    rec->file = storage_file_alloc(storage);
    if(!storage_file_open(rec->file, path, FSAM_WRITE, FSOM_CREATE_ALWAYS)) {
        FURI_LOG_E(TAG, "Cannot create %s", path);
        storage_file_free(rec->file);
        rec->file = NULL;
        return RadioGeddonRecordOpenFailed;
    }
    rec->wlen = rg_rawfmt_header(rec->wbuf, RECORDER_WBUF, frequency, preset);
    if(!radiogeddon_recorder_flush(rec)) return RadioGeddonRecordWriteFailed;

    rec->thread = furi_thread_alloc_ex(TAG, RECORDER_STACK, radiogeddon_recorder_writer, rec);
    furi_thread_start(rec->thread);
    return RadioGeddonRecordOk;
}

void radiogeddon_recorder_push(RadioGeddonRecorder* rec, bool level, uint32_t duration) {
    if(duration == 0) return; // carries no time; the file format skips it too
    rg_ring_push(&rec->ring, rg_ring_sample(level, duration));
}

void radiogeddon_recorder_note_overrun(RadioGeddonRecorder* rec) {
    __atomic_store_n(&rec->radio_overruns, rec->radio_overruns + 1, __ATOMIC_RELAXED);
}

bool radiogeddon_recorder_failed(RadioGeddonRecorder* rec) {
    return __atomic_load_n(&rec->failed, __ATOMIC_ACQUIRE);
}

void radiogeddon_recorder_stats(RadioGeddonRecorder* rec, RadioGeddonRecordStats* out) {
    RgRingStats rs;
    rg_ring_stats(&rec->ring, &rs);
    uint32_t cap = rg_ring_capacity(&rec->ring);
    out->samples = rs.pushed;
    out->lost = rs.dropped;
    out->gaps = rs.gaps;
    out->first_gap = rs.first_gap;
    out->radio_overruns = __atomic_load_n(&rec->radio_overruns, __ATOMIC_RELAXED);
    uint32_t end = rec->stop_tick ? rec->stop_tick : furi_get_tick();
    out->elapsed_ms = radiogeddon_recorder_ms(end - rec->start_tick);
    out->buffer = cap;
    out->fill_pct = (uint8_t)((uint64_t)rs.used * 100u / cap);
    out->peak_pct = (uint8_t)((uint64_t)rs.peak * 100u / cap);
    out->error = radiogeddon_recorder_failed(rec) ? RadioGeddonRecordWriteFailed :
                                                    RadioGeddonRecordOk;
}

void radiogeddon_recorder_finish(RadioGeddonRecorder* rec, RadioGeddonRecordStats* out) {
    rec->stop_tick = furi_get_tick();
    if(rec->thread) {
        __atomic_store_n(&rec->stop, true, __ATOMIC_RELEASE);
        furi_thread_flags_set(furi_thread_get_id(rec->thread), RECORDER_FLAG_WAKE);
        furi_thread_join(rec->thread);
        furi_thread_free(rec->thread);
        rec->thread = NULL;
    }

    RadioGeddonRecordStats stats;
    radiogeddon_recorder_stats(rec, &stats);
    if(rec->file) {
        // Close the last line and note any losses, then close the file.
        if(RECORDER_WBUF - rec->wlen < RECORDER_TEXT_MAX) radiogeddon_recorder_flush(rec);
        rg_rawfmt_end_line(&rec->fmt, rec->wbuf, RECORDER_WBUF, &rec->wlen);
        rec->wlen += rg_rawfmt_lost(
            rec->wbuf + rec->wlen,
            RECORDER_WBUF - rec->wlen,
            stats.lost,
            stats.gaps,
            stats.first_gap);
        radiogeddon_recorder_flush(rec);
        if(!storage_file_close(rec->file)) {
            __atomic_store_n(&rec->failed, true, __ATOMIC_RELEASE);
        }
        storage_file_free(rec->file);
        rec->file = NULL;
        if(radiogeddon_recorder_failed(rec)) stats.error = RadioGeddonRecordWriteFailed;
    }
    if(out) *out = stats;

    free(rec->wbuf);
    free(rec->ring_buf);
    free(rec);
}

const char* radiogeddon_recorder_error_text(RadioGeddonRecordError error) {
    switch(error) {
    case RadioGeddonRecordOk:
        return "OK";
    case RadioGeddonRecordNoMemory:
        return "Not enough memory";
    case RadioGeddonRecordOpenFailed:
        return "Cannot create file";
    case RadioGeddonRecordWriteFailed:
        return "SD card write failed";
    }
    return "Error";
}
