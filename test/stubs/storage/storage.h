/**
 * Host stand-in for the Storage API used by the recorder, the Database loader
 * and (test_formats) the firmware's FlipperFormat and stream code: files and
 * folders are real ones on the host (EXT_PATH maps to build/ext), with knobs
 * to make opens fail, writes slow or failing, or renames refuse to replace.
 */
#pragma once

#include "../furi.h" /* as the SDK's storage.h does */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#define EXT_PATH(path)          "build/ext/" path
#define STORAGE_EXT_PATH_PREFIX "build/ext"

typedef struct Storage Storage;

typedef struct {
    FILE* fp;
    void* dir; /* DIR* while a folder is open */
    char dir_path[256];
    bool error;
} File;

/* Values as in the firmware's filesystem_api_defines.h. */
typedef enum {
    FSAM_READ = (1 << 0),
    FSAM_WRITE = (1 << 1),
    FSAM_READ_WRITE = FSAM_READ | FSAM_WRITE,
} FS_AccessMode;

typedef enum {
    FSOM_OPEN_EXISTING = 1,
    FSOM_OPEN_ALWAYS = 2,
    FSOM_OPEN_APPEND = 4,
    FSOM_CREATE_NEW = 8,
    FSOM_CREATE_ALWAYS = 16,
} FS_OpenMode;

typedef enum {
    FSE_OK,
    FSE_NOT_READY,
    FSE_EXIST,
    FSE_NOT_EXIST,
    FSE_INVALID_PARAMETER,
    FSE_DENIED,
    FSE_INVALID_NAME,
    FSE_INTERNAL,
    FSE_NOT_IMPLEMENTED,
    FSE_ALREADY_OPEN,
} FS_Error;

typedef enum {
    FSF_DIRECTORY = (1 << 0),
} FS_Flags;

typedef struct {
    uint8_t flags;
    uint64_t size;
} FileInfo;

File* storage_file_alloc(Storage* storage);
bool storage_file_open(File* file, const char* path, FS_AccessMode access, FS_OpenMode mode);
size_t storage_file_read(File* file, void* buf, size_t size);
size_t storage_file_write(File* file, const void* buf, size_t size);
FS_Error storage_file_get_error(File* file);
bool storage_file_close(File* file);
void storage_file_free(File* file);

bool storage_dir_open(File* file, const char* path);
bool storage_dir_read(File* file, FileInfo* info, char* name, uint16_t name_length);
bool storage_dir_close(File* file);
bool file_info_is_dir(const FileInfo* info);

FS_Error storage_common_timestamp(Storage* storage, const char* path, uint32_t* timestamp);

/* Used by the firmware's FlipperFormat and stream code (test_formats). */
bool storage_file_is_open(File* file);
bool storage_file_seek(File* file, uint32_t offset, bool from_start);
uint64_t storage_file_tell(File* file);
bool storage_file_truncate(File* file);
uint64_t storage_file_size(File* file);
bool storage_file_sync(File* file);
bool storage_file_eof(File* file);
FS_Error storage_common_remove(Storage* storage, const char* path);
FS_Error storage_common_rename(Storage* storage, const char* old_path, const char* new_path);
FS_Error storage_common_mkdir(Storage* storage, const char* path);
bool storage_common_exists(Storage* storage, const char* path);
typedef struct FuriString FuriString;
void storage_get_next_filename(
    Storage* storage,
    const char* dirname,
    const char* filename,
    const char* fileextension,
    FuriString* nextfilename,
    uint8_t max_len);

/* Used by the firmware's RAW protocol to save a capture, which test_fwdecode
 * never does: the stand-ins abort (subghz_stub.c). */
bool storage_simply_mkdir(Storage* storage, const char* path);
bool storage_simply_remove(Storage* storage, const char* path);

/* Test controls. */
extern bool stub_open_fails;
extern unsigned stub_write_delay_us; /* per write call */
extern long stub_write_fail_after; /* bytes; -1 = never */
extern unsigned stub_write_calls;
extern unsigned stub_files_open; /* files and folders open now */
extern bool stub_rename_keeps_target; /* rename onto an existing file fails (FSE_EXIST) */
