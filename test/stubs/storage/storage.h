/**
 * Host stand-in for the Storage API used by the recorder and the Database
 * loader: files and folders are real ones on the host (EXT_PATH maps to
 * build/ext), with knobs to make opens fail or writes slow or failing.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#define EXT_PATH(path) "build/ext/" path

typedef struct Storage Storage;

typedef struct {
    FILE* fp;
    void* dir; /* DIR* while a folder is open */
    char dir_path[256];
    bool error;
} File;

typedef enum {
    FSAM_READ = 1,
    FSAM_WRITE = 2,
} FS_AccessMode;

typedef enum {
    FSOM_OPEN_EXISTING = 1,
    FSOM_CREATE_ALWAYS = 8,
} FS_OpenMode;

typedef enum {
    FSE_OK,
    FSE_NOT_EXIST = 3,
    FSE_INTERNAL = 7,
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

/* Test controls. */
extern bool stub_open_fails;
extern unsigned stub_write_delay_us; /* per write call */
extern long stub_write_fail_after; /* bytes; -1 = never */
extern unsigned stub_write_calls;
extern unsigned stub_files_open; /* files and folders open now */
