/**
 * Fuzz target: a .sub file of any content through the streaming RAW reader
 * (helpers/rg_raw.c), then the samples it yields through the analyzer
 * (helpers/rg_analyzer.c), as Unknown Protocol Analysis does on the device.
 *
 * Input: byte 0 caps each source read (exercises the reader's buffer edges),
 * byte 1 caps each rg_raw_reader_read() call, the rest is the file.
 *
 * Invariants (abort on violation):
 *  - samples are never 0, and time_us is the sum of their magnitudes;
 *  - the same samples come out whatever the read sizes, and after a rewind;
 *  - a seek lands at or before the target time and resumes with the samples
 *    that are at that index;
 *  - analysis results stay inside their documented ranges.
 */
#include "../../helpers/rg_raw.h"
#include "../../helpers/rg_analyzer.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define KEEP 4096

#define REQUIRE(c)        \
    do {                  \
        if(!(c)) abort(); \
    } while(0)

typedef struct {
    const uint8_t* data;
    size_t len;
    size_t pos;
    size_t chunk;
} Src;

static size_t src_read(void* ctx, uint8_t* buf, size_t len) {
    Src* s = ctx;
    size_t n = s->len - s->pos;
    if(n > len) n = len;
    if(s->chunk && n > s->chunk) n = s->chunk;
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

typedef struct {
    size_t count; /* all samples */
    uint64_t time_us;
    uint64_t hash; /* over all samples, for those beyond KEEP */
    int32_t kept[KEEP];
} Pass;

static void read_pass(RgRawReader* r, size_t step, Pass* p) {
    memset(p, 0, sizeof(*p));
    p->hash = 1469598103934665603ull;
    int32_t buf[64];
    size_t n;
    while((n = rg_raw_reader_read(r, buf, step)) > 0) {
        REQUIRE(n <= step);
        for(size_t i = 0; i < n; i++) {
            REQUIRE(buf[i] != 0);
            if(p->count < KEEP) p->kept[p->count] = buf[i];
            p->count++;
            p->time_us += buf[i] < 0 ? (uint64_t)(-(int64_t)buf[i]) : (uint64_t)buf[i];
            p->hash = (p->hash ^ (uint32_t)buf[i]) * 1099511628211ull;
        }
    }
    REQUIRE(r->index == p->count);
    REQUIRE(r->time_us == p->time_us);
}

static bool same(const Pass* a, const Pass* b) {
    size_t kept = a->count < KEEP ? a->count : KEEP;
    return a->count == b->count && a->time_us == b->time_us && a->hash == b->hash &&
           memcmp(a->kept, b->kept, kept * sizeof(int32_t)) == 0;
}

static void check_analysis(const RgAnalysis* r, size_t samples) {
    REQUIRE(r->sample_count <= samples);
    REQUIRE(r->high_peak_count <= RG_ANALYZER_MAX_PEAKS);
    REQUIRE(r->low_peak_count <= RG_ANALYZER_MAX_PEAKS);
    REQUIRE(r->noise_pct <= 100);
    REQUIRE(r->encoding < RgEncodingCount && r->alternative < RgEncodingCount);
    REQUIRE(r->encoding_confidence >= 0 && r->encoding_confidence <= 95);
    REQUIRE(r->fit_pct >= 0 && r->fit_pct <= 100);
    REQUIRE(r->alternative_fit >= 0 && r->alternative_fit <= 100);
    REQUIRE(r->frames_kept <= RG_ANALYZER_MAX_FRAMES);
    REQUIRE(r->frames_kept <= r->frame_count);
    REQUIRE(r->signal_frames <= r->frame_count);
    REQUIRE(r->bit_count <= RG_ANALYZER_MAX_BITS);
    REQUIRE(strlen(r->bits) <= RG_ANALYZER_MAX_BITS);
    REQUIRE(r->group_count <= RG_ANALYZER_MAX_GROUPS);
    /* The dominant pattern is the first group's frame. */
    REQUIRE(r->group_count == 0 || r->groups[0].frame < r->frames_kept);
    REQUIRE(strlen(r->bits) == (r->group_count ? r->frames[r->groups[0].frame].bit_count : 0u));
    REQUIRE(strlen(r->field_map) <= RG_ANALYZER_MAX_BITS);
    REQUIRE(r->const_bits + r->changing_bits <= RG_ANALYZER_MAX_BITS);
    REQUIRE(r->id_start + r->id_len <= RG_ANALYZER_MAX_BITS);
    for(size_t i = 0; i < r->frames_kept; i++) {
        const RgFrame* f = &r->frames[i];
        REQUIRE(f->fit <= 100);
        REQUIRE(f->bit_count <= RG_ANALYZER_MAX_BITS);
        REQUIRE(f->group == RG_ANALYZER_NO_GROUP || f->group < r->group_count);
    }
    for(size_t g = 0; g < r->group_count; g++) {
        REQUIRE(r->groups[g].frame < r->frames_kept);
        REQUIRE(r->groups[g].bit_count <= RG_ANALYZER_MAX_BITS);
    }
}

int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    if(size < 2) return 0;
    size_t chunk = data[0];
    size_t step = 1 + data[1] % 64;
    Src src = {data + 2, size - 2, 0, chunk};
    RgRawSource source = {src_read, src_seek, &src};

    static RgRawReader r;
    static Pass first, again;
    rg_raw_reader_init(&r, source);
    read_pass(&r, step, &first);

    /* Whole-buffer reads and full read steps give the same samples. */
    static RgRawReader r2;
    Src src2 = {data + 2, size - 2, 0, 0};
    RgRawSource source2 = {src_read, src_seek, &src2};
    rg_raw_reader_init(&r2, source2);
    read_pass(&r2, 64, &again);
    REQUIRE(same(&first, &again));
    REQUIRE(r.corrupt == r2.corrupt && r.lost == r2.lost && r.any_data == r2.any_data);

    REQUIRE(rg_raw_reader_rewind(&r));
    REQUIRE(r.index == 0 && r.time_us == 0);
    read_pass(&r, step, &again);
    REQUIRE(same(&first, &again));

    /* Seeks to a few times spread over the recording. */
    for(int t = 0; t <= 4 && first.time_us > 0; t++) {
        uint64_t target = first.time_us * (uint64_t)t / 4u;
        REQUIRE(rg_raw_reader_seek_time(&r, target));
        REQUIRE(r.time_us <= target);
        REQUIRE(r.index <= first.count);
        uint32_t at = r.index;
        int32_t win[16];
        size_t got = rg_raw_reader_read(&r, win, 16);
        for(size_t i = 0; i < got; i++)
            if(at + i < KEEP) REQUIRE(win[i] == first.kept[at + i]);
    }

    /* Analysis over the same stream, pass by pass, as the device runs it. */
    static RgAnalyzer a;
    rg_analyzer_begin(&a);
    int passes = 0;
    do {
        REQUIRE(++passes <= 8);
        REQUIRE(rg_raw_reader_rewind(&r));
        int32_t buf[64];
        size_t n;
        while((n = rg_raw_reader_read(&r, buf, step)) > 0)
            rg_analyzer_feed(&a, buf, n);
    } while(rg_analyzer_next_pass(&a));
    check_analysis(&a.result, first.count);
    return 0;
}
