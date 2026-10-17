/**
 * Fuzz target: recording recovery (helpers/rg_recover.c), which reads
 * whatever a reboot left of recording.tmp on the SD card.
 *
 * Input: the file's bytes, fed in chunks whose size comes from the first byte.
 *
 * Invariants (abort on violation):
 *  - the cut is inside the file, and the scan is the same whatever the chunks;
 *  - a Partial or Complete verdict has samples; the repaired file (the first
 *    `cut` bytes plus rg_recover_tail) scans as Complete with exactly the same
 *    samples and duration, and rg_raw reads exactly that many samples from it
 *    without finding a malformed value.
 */
#include "../../helpers/rg_recover.h"
#include "../../helpers/rg_raw.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define REQUIRE(c)        \
    do {                  \
        if(!(c)) abort(); \
    } while(0)

typedef struct {
    const uint8_t* data;
    size_t len, pos;
} Src;

static size_t src_read(void* ctx, uint8_t* buf, size_t len) {
    Src* s = ctx;
    size_t n = s->len - s->pos < len ? s->len - s->pos : len;
    memcpy(buf, s->data + s->pos, n);
    s->pos += n;
    return n;
}

static bool src_seek(void* ctx, uint32_t offset) {
    Src* s = ctx;
    if(offset > s->len) return false;
    s->pos = offset;
    return true;
}

int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    if(size < 1 || size > 16384) return 0;
    size_t chunk = 1 + data[0] % 61;
    data++;
    size--;
    RgRecoverScan a, b;
    rg_recover_init(&a);
    rg_recover_init(&b);
    for(size_t off = 0; off < size; off += chunk)
        rg_recover_feed(&a, data + off, size - off < chunk ? size - off : chunk);
    rg_recover_feed(&b, data, size);
    RgRecoverVerdict va = rg_recover_finish(&a), vb = rg_recover_finish(&b);
    REQUIRE(va == vb && a.cut == b.cut && a.samples == b.samples);
    REQUIRE(a.cut <= size && a.cut <= a.offset);
    if(va != RgRecoverPartial && va != RgRecoverComplete) return 0;
    REQUIRE(a.samples > 0);

    static uint8_t fixed[16384 + 256];
    memcpy(fixed, data, a.cut);
    size_t len = a.cut;
    if(va == RgRecoverPartial) {
        size_t t = rg_recover_tail(&a, (char*)fixed + len, sizeof(fixed) - len);
        REQUIRE(t > 0);
        len += t;
    }
    RgRecoverScan c;
    rg_recover_init(&c);
    rg_recover_feed(&c, fixed, len);
    REQUIRE(rg_recover_finish(&c) == RgRecoverComplete);
    REQUIRE(c.samples == a.samples && c.duration_us == a.duration_us);

    Src src = {fixed, len, 0};
    static RgRawReader r;
    rg_raw_reader_init(&r, (RgRawSource){src_read, src_seek, &src});
    int32_t buf[64];
    size_t n, total = 0;
    uint64_t dur = 0;
    while((n = rg_raw_reader_read(&r, buf, 64)) > 0) {
        for(size_t i = 0; i < n; i++)
            dur += (uint64_t)(buf[i] < 0 ? -(int64_t)buf[i] : buf[i]);
        total += n;
    }
    REQUIRE(total == a.samples && dur == a.duration_us);
    REQUIRE(!r.corrupt);
    return 0;
}
