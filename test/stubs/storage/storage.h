/**
 * Host stand-in for the Storage file API used by the recorder: files are real
 * files on the host, with knobs to make writes slow or fail.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>

typedef struct Storage Storage;

typedef struct {
    FILE* fp;
} File;

typedef enum {
    FSAM_WRITE = 2,
} FS_AccessMode;

typedef enum {
    FSOM_CREATE_ALWAYS = 8,
} FS_OpenMode;

File* storage_file_alloc(Storage* storage);
bool storage_file_open(File* file, const char* path, FS_AccessMode access, FS_OpenMode mode);
size_t storage_file_write(File* file, const void* buf, size_t size);
bool storage_file_close(File* file);
void storage_file_free(File* file);

/* Test controls. */
extern bool stub_open_fails;
extern unsigned stub_write_delay_us; /* per write call */
extern long stub_write_fail_after; /* bytes; -1 = never */
extern unsigned stub_write_calls;
