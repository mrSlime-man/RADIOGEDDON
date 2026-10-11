/**
 * Host tests for the RF Activity Logger's files (helpers/radiogeddon_actlog.c)
 * on a real folder through the stub Storage (EXT_PATH maps to test/build/ext):
 * header, interval rows, part rotation at the size limit, stopping when the
 * last part is full, a card that fails mid-log, the footer and summary, and
 * that every byte and file is released. Visits are simulated.
 * Run: make -C test actlogfiles
 */
#include "furi.h"
#include "storage/storage.h"
#include "../helpers/radiogeddon_actlog.h"

#include <sys/stat.h>
#include <unistd.h>

static int g_failures = 0;
static int g_checks = 0;

#define CHECK(cond, msg)                                             \
    do {                                                             \
        g_checks++;                                                  \
        if(!(cond)) {                                                \
            g_failures++;                                            \
            printf("  FAIL: %s (%s:%d)\n", msg, __FILE__, __LINE__); \
        }                                                            \
    } while(0)

static int g_card;
static Storage* const CARD = (Storage*)&g_card;
static const uint32_t g_freqs[] = {315000000, 433920000, 868350000};
static char g_rows[RG_ACTLOG_MAX_CHANNELS * RG_ACTLOG_LINE_MAX];
static char g_summary[1200];

static void reset_card(void) {
    if(system("rm -rf build/ext && mkdir -p build/ext/apps_data/radiogeddon") != 0) exit(2);
}

static size_t count_files(const char* suffix) {
    char cmd[200];
    snprintf(cmd, sizeof(cmd), "ls %s 2>/dev/null | grep -c '%s$'", RADIOGEDDON_ACTLOG_FOLDER, suffix);
    FILE* p = popen(cmd, "r");
    unsigned n = 0;
    if(p) {
        if(fscanf(p, "%u", &n) != 1) n = 0;
        pclose(p);
    }
    return n;
}

static char* read_file(const char* name) {
    char path[256];
    snprintf(path, sizeof(path), "%s/%s", RADIOGEDDON_ACTLOG_FOLDER, name);
    FILE* f = fopen(path, "rb");
    if(!f) return NULL;
    static char buf[200000];
    size_t n = fread(buf, 1, sizeof(buf) - 1, f);
    buf[n] = '\0';
    fclose(f);
    return buf;
}

/* Visit every channel once per 100 ms of simulated time until @p until. */
static uint32_t run(RadioGeddonActlog* l, uint32_t t, uint32_t until, bool* stopped) {
    for(; t < until; t += 100) {
        for(size_t c = 0; c < 3; c++) {
            bool active = c == 1 && (t / 1000) % 7 == 0;
            radiogeddon_actlog_observe(l, c, active, active && t % 1000 == 0, active ? -50 : -99, -100, 20, t);
        }
        size_t n = radiogeddon_actlog_collect(l, t, g_rows, sizeof(g_rows));
        if(n && !radiogeddon_actlog_write(l, g_rows, n)) {
            *stopped = true;
            return t;
        }
    }
    return t;
}

static void test_log_and_summary(void) {
    printf("test_log_and_summary\n");
    reset_card();
    size_t bytes = stub_live_bytes, blocks = stub_live_blocks;
    RadioGeddonActlogConfig cfg = {10, 1, 64 * 1024, "AM650, dwell 20 ms, +10 dB, Standard"};
    RadioGeddonActlog* l = radiogeddon_actlog_start(CARD, g_freqs, 3, &cfg, 0);
    CHECK(l != NULL, "started");
    bool stopped = false;
    uint32_t t = run(l, 0, 61000, &stopped);
    RadioGeddonActlogStatus st;
    radiogeddon_actlog_status(l, t, &st);
    CHECK(!stopped && st.state == RadioGeddonActlogDone, "time up after one minute");
    CHECK(st.rows == 6 * 3, "six intervals of three rows");
    CHECK(st.top[0] == 1 && st.top_events[0] > 0, "the active channel leads");
    size_t n = radiogeddon_actlog_finish(l, "duration reached", t, g_summary, sizeof(g_summary));
    CHECK(n > 0 && strstr(g_summary, "433.92") && strstr(g_summary, "duration reached"), "summary");
    CHECK(count_files(".csv") == 1 && count_files("_summary.txt") == 1, "log and summary files");
    CHECK(stub_files_open == 0, "files closed");
    CHECK(stub_live_bytes == bytes && stub_live_blocks == blocks, "everything freed");
    // The log reads as described: header, rows, footer.
    char cmd[256];
    snprintf(cmd, sizeof(cmd), "ls %s | grep -v summary", RADIOGEDDON_ACTLOG_FOLDER);
    FILE* p = popen(cmd, "r");
    char name[80] = {0};
    if(p) {
        if(!fgets(name, sizeof(name), p)) name[0] = '\0';
        pclose(p);
    }
    name[strcspn(name, "\n")] = '\0';
    char* text = read_file(name);
    CHECK(text && strstr(text, "# RadioGeddon RF Activity Log, part 1"), "header");
    CHECK(text && strstr(text, "t_s,freq_hz,visits,observed_ms"), "columns");
    CHECK(text && strstr(text, "10,433920000,"), "a row");
    CHECK(text && strstr(text, "# End: 6"), "footer");
}

static void test_rotation_and_limit(void) {
    printf("test_rotation_and_limit\n");
    reset_card();
    // Tiny parts: rows every 10 s of a long observation fill all of them.
    RadioGeddonActlogConfig cfg = {10, 0, 1024, "rotation"};
    RadioGeddonActlog* l = radiogeddon_actlog_start(CARD, g_freqs, 3, &cfg, 0);
    bool stopped = false;
    uint32_t t = run(l, 0, 3600000, &stopped);
    RadioGeddonActlogStatus st;
    radiogeddon_actlog_status(l, t, &st);
    CHECK(stopped && st.state == RadioGeddonActlogFull, "stops when the last part is full");
    CHECK(count_files(".csv") == RG_ACTLOG_MAX_PARTS, "every part written, none overwritten");
    radiogeddon_actlog_finish(l, "size limit", t, g_summary, sizeof(g_summary));
    CHECK(strstr(g_summary, "size limit reached") != NULL, "the summary says why");
    CHECK(stub_files_open == 0, "files closed");
}

static void test_card_failure(void) {
    printf("test_card_failure\n");
    reset_card();
    RadioGeddonActlogConfig cfg = {10, 0, 64 * 1024, "failure"};
    RadioGeddonActlog* l = radiogeddon_actlog_start(CARD, g_freqs, 3, &cfg, 0);
    stub_write_fail_after = 900; // the card fails after the header and a few rows
    bool stopped = false;
    uint32_t t = run(l, 0, 600000, &stopped);
    stub_write_fail_after = -1;
    RadioGeddonActlogStatus st;
    radiogeddon_actlog_status(l, t, &st);
    CHECK(stopped && st.state == RadioGeddonActlogFailed, "a write error stops the log");
    radiogeddon_actlog_finish(l, "SD write failed", t, g_summary, sizeof(g_summary));
    CHECK(strstr(g_summary, "SD write failed") != NULL, "summary still produced");
    CHECK(stub_files_open == 0, "files closed after the failure");
    // The card is unusable from the start.
    stub_open_fails = true;
    CHECK(radiogeddon_actlog_start(CARD, g_freqs, 3, &cfg, 0) == NULL, "refused without a card");
    stub_open_fails = false;
}

int main(void) {
    stub_write_fail_after = -1;
    test_log_and_summary();
    test_rotation_and_limit();
    test_card_failure();
    printf("\n%d checks, %d failures\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
