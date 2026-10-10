/**
 * A fake radio for the engine lifecycle tests (test_lifecycle.c): the parts
 * of helpers/radiogeddon_subghz.h the Scanner, Range Scanner and Hopper
 * engines call, with an RSSI model the test controls and counters that show
 * every session and capture was closed. The real engines run on real threads
 * against it; nothing of the firmware's radio code is involved.
 */
#include "../../helpers/radiogeddon_subghz.h"
#include "../../helpers/radiogeddon_storage.h"

#include <stdatomic.h>
#include <time.h>

/* Test controls and counters. */
_Atomic uint32_t fake_active_hz = 0; /* frequency with a strong signal, 0: none */
_Atomic int fake_scan_sessions = 0; /* scan_begin minus scan_end */
_Atomic int fake_scan_begins = 0;
_Atomic int fake_probes = 0;
_Atomic int fake_bad_probes = 0; /* probes of a frequency outside the bands */
_Atomic int fake_records_open = 0; /* started minus saved/discarded */
_Atomic int fake_records_started = 0;
_Atomic int fake_records_saved = 0;
_Atomic uint32_t fake_tuned_hz = 0;
uint32_t fake_band_lo = 300000000, fake_band_hi = 928000000; /* tunable span */

struct RadioGeddonSubGhz {
    int unused;
};

static bool rec_running = false;
static bool rec_pending = false;
static void* rec_buffer = NULL; /* stands for the recorder's ring */

void furi_delay_ms(uint32_t ms) {
    struct timespec ts = {ms / 1000, (long)(ms % 1000) * 1000000L};
    nanosleep(&ts, NULL);
}

static int fake_record;
void* furi_record_open(const char* name) {
    (void)name;
    return &fake_record;
}
void furi_record_close(const char* name) {
    (void)name;
}

static float fake_rssi_at(uint32_t hz) {
    static _Atomic unsigned seed = 1;
    unsigned s = atomic_fetch_add(&seed, 2654435761u);
    float noise = -100.0f + (float)((s >> 8) % 30) / 10.0f; /* -100 .. -97 dBm */
    return (hz && hz == atomic_load(&fake_active_hz)) ? -45.0f : noise;
}

void radiogeddon_subghz_scan_begin(RadioGeddonSubGhz* instance) {
    (void)instance;
    atomic_fetch_add(&fake_scan_sessions, 1);
    atomic_fetch_add(&fake_scan_begins, 1);
}

void radiogeddon_subghz_scan_end(RadioGeddonSubGhz* instance) {
    (void)instance;
    atomic_fetch_sub(&fake_scan_sessions, 1);
}

float radiogeddon_subghz_probe_rssi_dwell(
    RadioGeddonSubGhz* instance,
    uint32_t frequency,
    uint32_t dwell_ms) {
    (void)instance;
    (void)dwell_ms; /* the test does not wait for real dwell times */
    atomic_fetch_add(&fake_probes, 1);
    if(frequency < fake_band_lo || frequency > fake_band_hi) atomic_fetch_add(&fake_bad_probes, 1);
    return fake_rssi_at(frequency);
}

float radiogeddon_subghz_get_rssi(RadioGeddonSubGhz* instance) {
    (void)instance;
    return fake_rssi_at(atomic_load(&fake_tuned_hz));
}

void radiogeddon_subghz_rx_retune(RadioGeddonSubGhz* instance, uint32_t frequency) {
    (void)instance;
    atomic_store(&fake_tuned_hz, frequency);
}

RadioGeddonRecordError radiogeddon_subghz_record_start(RadioGeddonSubGhz* instance) {
    (void)instance;
    if(rec_running) return RadioGeddonRecordOk;
    if(rec_pending) radiogeddon_subghz_record_discard(instance);
    rec_buffer = malloc(1024);
    rec_running = true;
    atomic_fetch_add(&fake_records_open, 1);
    atomic_fetch_add(&fake_records_started, 1);
    return RadioGeddonRecordOk;
}

void radiogeddon_subghz_record_stop(RadioGeddonSubGhz* instance) {
    (void)instance;
    if(!rec_running) return;
    rec_running = false;
    rec_pending = true;
}

bool radiogeddon_subghz_record_status(RadioGeddonSubGhz* instance, RadioGeddonRecordStats* out) {
    (void)instance;
    memset(out, 0, sizeof(*out));
    out->samples = 500; /* long enough to be saved */
    return rec_running;
}

bool radiogeddon_subghz_record_pending(RadioGeddonSubGhz* instance) {
    (void)instance;
    return rec_pending;
}

bool radiogeddon_subghz_record_save(RadioGeddonSubGhz* instance, const char* path) {
    (void)instance;
    (void)path;
    if(!rec_pending) return false;
    rec_pending = false;
    free(rec_buffer);
    rec_buffer = NULL;
    atomic_fetch_sub(&fake_records_open, 1);
    atomic_fetch_add(&fake_records_saved, 1);
    return true;
}

void radiogeddon_subghz_record_discard(RadioGeddonSubGhz* instance) {
    (void)instance;
    if(!rec_pending) return;
    rec_pending = false;
    free(rec_buffer);
    rec_buffer = NULL;
    atomic_fetch_sub(&fake_records_open, 1);
}

const char* radiogeddon_recorder_error_text(RadioGeddonRecordError error) {
    (void)error;
    return "fake";
}

bool radiogeddon_storage_make_unique_path(Storage* storage, FuriString* out, const char* name) {
    (void)storage;
    furi_string_printf(out, "/ext/apps_data/radiogeddon/signals/%s.sub", name);
    return true;
}
