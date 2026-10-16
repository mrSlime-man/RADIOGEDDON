/* Host benchmark of the fingerprint index (helpers/radiogeddon_fp.c): Find
 * Similar Signals over a library of N recordings on the stub card, counting
 * every call into the card (folder listings, entries read, files opened,
 * stat/timestamp calls), the peak heap and the host time.
 *
 *   make -C test benchfp
 *
 * Per N: a cold search (no index: every recording fingerprinted), a warm
 * one (nothing changed), and one after 5 new recordings. The recordings are
 * synthetic: decoded key files and short RAW captures (one in five). Host
 * time is only relative; the card counts are what the Flipper does too. */
#include "furi.h"
#include "storage/storage.h"
#include "../helpers/radiogeddon_fp.h"
#include "../helpers/radiogeddon_storage.h"

#include <time.h>

extern size_t stub_heap_free;
extern size_t stub_peak_bytes;
extern size_t stub_live_bytes;

static int g_card;
static Storage* const CARD = (Storage*)&g_card;

static uint64_t now_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000u + (uint64_t)ts.tv_nsec;
}

static void write_rec(int i) {
    char path[256];
    snprintf(path, sizeof(path), "%s/rec_%05d.sub", RADIOGEDDON_SIGNALS_FOLDER, i);
    FILE* fp = fopen(path, "wb");
    if(i % 5 == 0) {
        fprintf(
            fp,
            "Filetype: Flipper SubGhz RAW File\nVersion: 1\nFrequency: 433920000\n"
            "Preset: FuriHalSubGhzPresetOok650Async\nProtocol: RAW\nRAW_Data: -20000");
        for(int f = 0; f < 4; f++) {
            for(int b = 0; b < 24; b++) {
                int one = ((i >> (b % 10)) ^ b) & 1, te = 300 + (i % 7) * 40;
                fprintf(fp, " %d %d", one ? 3 * te : te, one ? -te : -3 * te);
            }
            fprintf(fp, " 300 -9300");
        }
        fprintf(fp, "\n");
    } else {
        fprintf(
            fp,
            "Filetype: Flipper SubGhz Key File\nVersion: 1\nFrequency: %u\n"
            "Preset: FuriHalSubGhzPresetOok650Async\nProtocol: Princeton\nBit: 24\n"
            "Key: 00 00 00 00 00 %02X %02X %02X\nTE: 400\n",
            433920000u + (unsigned)(i % 3) * 1000000u,
            (unsigned)(i * 37) & 0xFF,
            (unsigned)(i * 11) & 0xFF,
            (unsigned)i & 0xFF);
    }
    fclose(fp);
}

static void run(const char* what, size_t n) {
    static RadioGeddonFpResult res;
    char target[256];
    snprintf(target, sizeof(target), "%s/rec_%05d.sub", RADIOGEDDON_SIGNALS_FOLDER, 1);
    memset(&stub_io, 0, sizeof(stub_io));
    size_t base = stub_live_bytes;
    stub_peak_bytes = base;
    uint64_t t0 = now_ns();
    bool ok = radiogeddon_fp_find_similar(CARD, target, 0, &res, NULL, NULL);
    uint64_t t1 = now_ns();
    printf(
        "%6zu %-6s %3s %6u %8u %7u %7u %7u %8u %10.1f %7zu\n",
        n,
        what,
        ok ? "ok" : "NO",
        stub_io.dir_opens,
        stub_io.dir_reads,
        stub_io.file_opens,
        stub_io.stats,
        (unsigned)res.indexed,
        (unsigned)res.computed,
        (t1 - t0) / 1e6,
        stub_peak_bytes - base);
}

int main(void) {
    static const size_t sizes[] = {10, 100, 500, 1000};
    stub_write_fail_after = -1;
    stub_heap_free = 64 * 1024; // a Full edition with a tool module loaded
    printf(
        "%6s %-6s %3s %6s %8s %7s %7s %7s %8s %10s %7s\n",
        "N",
        "search",
        "",
        "lists",
        "entries",
        "opens",
        "stats",
        "indexed",
        "computed",
        "ms (host)",
        "peak B");
    for(size_t si = 0; si < sizeof(sizes) / sizeof(sizes[0]); si++) {
        size_t n = sizes[si];
        if(system("rm -rf build/ext && mkdir -p " RADIOGEDDON_SIGNALS_FOLDER) != 0) return 2;
        for(size_t i = 0; i < n; i++)
            write_rec((int)i);
        run("cold", n);
        run("warm", n);
        for(size_t i = n; i < n + 5; i++)
            write_rec((int)i);
        run("+5 new", n + 5);
    }
    if(system("rm -rf build/ext") != 0) return 2;
    return 0;
}
