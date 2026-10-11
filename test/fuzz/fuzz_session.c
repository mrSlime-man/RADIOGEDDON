/**
 * Fuzz target: research session files (helpers/rg_session.c) and the module
 * size estimate (helpers/rg_elf.c), both read from the SD card.
 *
 * Input: the first byte picks the target. Session: the rest is a session
 * file's text. ELF: the rest is a module file.
 *
 * Invariants (abort on violation):
 *  - a parsed session has a NUL-terminated, valid name (when Ok), at most
 *    RG_SESSION_SIGNALS recordings, each a valid, distinct ".sub" name;
 *  - writing a parsed session and parsing the text again gives the same
 *    session (the format round-trips);
 *  - the ELF estimate never reads outside the file and never exceeds the
 *    sum of what the section headers in the file could describe.
 */
#include "../../helpers/rg_session.h"
#include "../../helpers/rg_elf.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define REQUIRE(c)        \
    do {                  \
        if(!(c)) abort(); \
    } while(0)

typedef struct {
    const uint8_t* data;
    size_t size;
} Blob;

static bool blob_read(void* context, uint32_t offset, void* buf, size_t len) {
    const Blob* b = context;
    if((size_t)offset > b->size || len > b->size - offset) return false;
    memcpy(buf, b->data + offset, len);
    return true;
}

static void check_session(const uint8_t* data, size_t size) {
    static RgSession s, again;
    static char text[RG_SESSION_TEXT_MAX];
    RgSessionParse r = rg_session_parse(&s, (const char*)data, size);
    REQUIRE(memchr(s.name, '\0', sizeof(s.name)) != NULL);
    REQUIRE(memchr(s.created, '\0', sizeof(s.created)) != NULL);
    REQUIRE(s.count <= RG_SESSION_SIGNALS);
    for(size_t i = 0; i < s.count; i++) {
        REQUIRE(memchr(s.signal[i], '\0', RG_SESSION_SIGNAL_MAX) != NULL);
        REQUIRE(rg_session_signal_valid(s.signal[i]));
        for(size_t j = 0; j < i; j++)
            REQUIRE(strcmp(s.signal[i], s.signal[j]) != 0);
    }
    if(r != RgSessionParseOk) return;
    REQUIRE(rg_session_name_valid(s.name));
    size_t n = rg_session_write(&s, text, sizeof(text));
    if(n == 0) return; // a created date too long for the text: not written
    REQUIRE(rg_session_parse(&again, text, n) == RgSessionParseOk);
    REQUIRE(strcmp(again.name, s.name) == 0 && again.count == s.count);
    for(size_t i = 0; i < s.count; i++)
        REQUIRE(strcmp(again.signal[i], s.signal[i]) == 0);
}

static void check_elf(const uint8_t* data, size_t size) {
    Blob b = {data, size};
    size_t total = rg_elf_alloc_bytes(blob_read, &b);
    // At most RG_ELF_MAX_SECTIONS sizes of 32 bits each.
    REQUIRE(total <= (size_t)RG_ELF_MAX_SECTIONS * 0xFFFFFFFFull);
}

int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    if(size == 0) return 0;
    if(data[0] & 1) {
        check_elf(data + 1, size - 1);
    } else {
        check_session(data + 1, size - 1);
    }
    return 0;
}
