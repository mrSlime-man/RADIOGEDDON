/* Host stand-ins for what the firmware's Sub-GHz decoders link against but
 * test_fwdecode never uses: the keystore, the RAW file player and saving.
 *
 * The keystore is a stand-in because the firmware's (lib/subghz/
 * subghz_keystore.c) holds ARM assembly. It behaves as the firmware's does
 * when no keys were loaded, which is how the tests run: an empty list of
 * manufacturer keys. It cannot load, save or read any key or table; those
 * calls abort. */
#include "furi.h"
#include "storage/storage.h"

#include <lib/subghz/types.h>
#include <lib/subghz/subghz_keystore.h>
#include <lib/subghz/subghz_file_encoder_worker.h>

#define STUB_UNREACHED(what)                                                       \
    do {                                                                           \
        fprintf(stderr, "test_fwdecode: %s is not available on the host\n", what); \
        abort();                                                                   \
    } while(0)

/* As in the firmware's keystore. */
struct SubGhzKeystore {
    SubGhzKeyArray_t data;
};

SubGhzKeystore* subghz_keystore_alloc(void) {
    SubGhzKeystore* instance = malloc(sizeof(SubGhzKeystore));
    SubGhzKeyArray_init(instance->data);
    return instance;
}

void subghz_keystore_free(SubGhzKeystore* instance) {
    furi_check(instance);
    /* Nothing is ever added, so there are no names to free. */
    furi_check(SubGhzKeyArray_size(instance->data) == 0);
    SubGhzKeyArray_clear(instance->data);
    free(instance);
}

SubGhzKeyArray_t* subghz_keystore_get_data(SubGhzKeystore* instance) {
    furi_check(instance);
    return &instance->data;
}

bool subghz_keystore_load(SubGhzKeystore* instance, const char* filename) {
    (void)instance;
    (void)filename;
    STUB_UNREACHED("subghz_keystore_load");
}

bool subghz_keystore_save(SubGhzKeystore* instance, const char* filename, uint8_t* iv) {
    (void)instance;
    (void)filename;
    (void)iv;
    STUB_UNREACHED("subghz_keystore_save");
}

bool subghz_keystore_raw_encrypted_save(
    const char* input_file_name,
    const char* output_file_name,
    uint8_t* iv) {
    (void)input_file_name;
    (void)output_file_name;
    (void)iv;
    STUB_UNREACHED("subghz_keystore_raw_encrypted_save");
}

bool subghz_keystore_raw_get_data(const char* file_name, size_t offset, uint8_t* data, size_t len) {
    (void)file_name;
    (void)offset;
    (void)data;
    (void)len;
    STUB_UNREACHED("subghz_keystore_raw_get_data");
}

/* The RAW protocol's encoder plays a file through this worker. */
void subghz_file_encoder_worker_callback_end(
    SubGhzFileEncoderWorker* instance,
    SubGhzFileEncoderWorkerCallbackEnd callback_end,
    void* context_end) {
    (void)instance;
    (void)callback_end;
    (void)context_end;
    STUB_UNREACHED("subghz_file_encoder_worker_callback_end");
}

SubGhzFileEncoderWorker* subghz_file_encoder_worker_alloc(void) {
    STUB_UNREACHED("subghz_file_encoder_worker_alloc");
}

void subghz_file_encoder_worker_free(SubGhzFileEncoderWorker* instance) {
    (void)instance;
    STUB_UNREACHED("subghz_file_encoder_worker_free");
}

LevelDuration subghz_file_encoder_worker_get_level_duration(void* context) {
    (void)context;
    STUB_UNREACHED("subghz_file_encoder_worker_get_level_duration");
}

bool subghz_file_encoder_worker_start(
    SubGhzFileEncoderWorker* instance,
    const char* file_path,
    const char* radio_device_name) {
    (void)instance;
    (void)file_path;
    (void)radio_device_name;
    STUB_UNREACHED("subghz_file_encoder_worker_start");
}

void subghz_file_encoder_worker_stop(SubGhzFileEncoderWorker* instance) {
    (void)instance;
    STUB_UNREACHED("subghz_file_encoder_worker_stop");
}

bool subghz_file_encoder_worker_is_running(SubGhzFileEncoderWorker* instance) {
    (void)instance;
    STUB_UNREACHED("subghz_file_encoder_worker_is_running");
}

/* Saving a RAW capture (the RAW protocol's file writer). */
void* furi_record_open(const char* name) {
    (void)name;
    STUB_UNREACHED("furi_record_open");
}

void furi_record_close(const char* name) {
    (void)name;
    STUB_UNREACHED("furi_record_close");
}

bool storage_simply_mkdir(Storage* storage, const char* path) {
    (void)storage;
    (void)path;
    STUB_UNREACHED("storage_simply_mkdir");
}

bool storage_simply_remove(Storage* storage, const char* path) {
    (void)storage;
    (void)path;
    STUB_UNREACHED("storage_simply_remove");
}

void furi_delay_ms(uint32_t milliseconds) {
    (void)milliseconds;
    STUB_UNREACHED("furi_delay_ms");
}

size_t memmgr_get_free_heap(void) {
    STUB_UNREACHED("memmgr_get_free_heap");
}
