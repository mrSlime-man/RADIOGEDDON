/**
 * Host tests for the RF Activity Logger's core (helpers/rg_actlog.c):
 * per-visit statistics, intervals with and without measurements, the CSV
 * rows and header, part rotation and the size limit, ranking, coverage and
 * the summary. Visits are simulated; no radio is involved.
 * Run: make -C test
 */
#include "../helpers/rg_actlog.h"
#include <stdio.h>
#include <string.h>

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

static const uint32_t g_freqs[] = {315000000, 433920000, 868350000};

static void test_rows(void) {
    printf("test_rows\n");
    RgActlog log;
    rg_actlog_init(&log, g_freqs, 3, 4096, 1000);
    // Interval 1: channel 1 active once, channel 0 quiet, channel 2 never visited.
    rg_actlog_observe(&log, 0, false, false, -100, -101, 20, 1100);
    rg_actlog_observe(&log, 1, true, true, -45, -99, 20, 1130);
    rg_actlog_observe(&log, 1, true, false, -48, -99, 20, 1160);
    CHECK(!rg_actlog_interval_due(&log, 10000, 5000), "interval not yet due");
    CHECK(rg_actlog_interval_due(&log, 10000, 11000), "interval due");
    char line[RG_ACTLOG_LINE_MAX];
    rg_actlog_row(&log, 0, 11000, line, sizeof(line));
    CHECK(strcmp(line, "10,315000000,1,20,0,0,-100,-101\n") == 0, "quiet but measured");
    rg_actlog_row(&log, 1, 11000, line, sizeof(line));
    CHECK(strcmp(line, "10,433920000,2,40,2,1,-45,-99\n") == 0, "active");
    rg_actlog_row(&log, 2, 11000, line, sizeof(line));
    CHECK(strcmp(line, "10,868350000,0,0,,,,\n") == 0, "no measurement is not quiet");
    CHECK(rg_actlog_row(&log, 9, 11000, line, sizeof(line)) == 0, "no such channel");
    CHECK(rg_actlog_row(&log, 1, 11000, line, 10) == 0, "a row that does not fit is not cut");
    rg_actlog_end_interval(&log, 11000);
    rg_actlog_row(&log, 1, 21000, line, sizeof(line));
    CHECK(strcmp(line, "20,433920000,0,0,,,,\n") == 0, "interval counters cleared");
    CHECK(log.ch[1].events == 1 && log.ch[1].visits == 2, "totals kept");
    CHECK(log.intervals == 1, "intervals counted");
    char head[600];
    size_t n = rg_actlog_header(&log, "AM650, 20 ms", "2026-10-11 12:00", head, sizeof(head));
    CHECK(n > 0 && strstr(head, "one frequency is measured at a time") == NULL, "header written");
    CHECK(strstr(head, "only during its visits") != NULL, "header states the coverage limit");
    CHECK(strstr(head, "t_s,freq_hz,visits") != NULL, "columns");
    CHECK(rg_actlog_header(&log, "x", "y", head, 40) == 0, "header that does not fit");
}

static void test_rotation(void) {
    printf("test_rotation\n");
    RgActlog log;
    rg_actlog_init(&log, g_freqs, 3, 100, 0);
    CHECK(log.max_bytes == 1024, "minimum part size");
    char name[48];
    rg_actlog_part_name("activity_20261011", 0, name, sizeof(name));
    CHECK(strcmp(name, "activity_20261011.csv") == 0, "first part");
    rg_actlog_part_name("activity_20261011", 2, name, sizeof(name));
    CHECK(strcmp(name, "activity_20261011_3.csv") == 0, "third part");
    int parts = 1;
    bool stopped = false;
    for(int i = 0; i < 1000 && !stopped; i++) {
        if(rg_actlog_account(&log, 100)) parts++;
        stopped = log.full;
    }
    CHECK(parts == RG_ACTLOG_MAX_PARTS, "rotated into every part");
    CHECK(stopped, "stops when the last part is full");
    char foot[80];
    rg_actlog_footer(&log, "size limit", foot, sizeof(foot));
    CHECK(strstr(foot, "# End:") == foot && strstr(foot, "size limit"), "footer");
}

static void test_summary(void) {
    printf("test_summary\n");
    RgActlog log;
    rg_actlog_init(&log, g_freqs, 3, 4096, 0);
    uint32_t t = 0;
    for(int i = 0; i < 300; i++) {
        for(size_t c = 0; c < 3; c++) {
            t += 23;
            bool active = (c == 2 && i % 10 < 2) || (c == 0 && i == 50);
            bool started = (c == 2 && i % 10 == 0) || (c == 0 && i == 50);
            rg_actlog_observe(&log, c, active, started, active ? -60 : -98, -99, 20, t);
        }
    }
    rg_actlog_tick(&log, t + 1000);
    uint8_t order[3];
    CHECK(rg_actlog_ranking(&log, order, 3) == 3, "ranked");
    CHECK(order[0] == 2 && order[1] == 0 && order[2] == 1, "most active first");
    uint8_t cov = rg_actlog_coverage(&log, 1);
    CHECK(cov > 20 && cov < 33, "each heard about a third of the time, minus retunes");
    char text[700];
    rg_actlog_summary(&log, text, sizeof(text));
    CHECK(strstr(text, "868.35: 30 ev") != NULL, "top frequency");
    CHECK(strstr(text, "[OBSERVED] 31 activity starts") != NULL, "events");
    CHECK(strstr(text, "only during its visits") != NULL, "coverage limit stated");
    char small[60];
    rg_actlog_summary(&log, small, sizeof(small));
    CHECK(strlen(small) < sizeof(small), "summary cut to the buffer");
    // Nothing seen.
    RgActlog quiet;
    rg_actlog_init(&quiet, g_freqs, 3, 4096, 0);
    rg_actlog_observe(&quiet, 0, false, false, -100, -100, 20, 50);
    rg_actlog_summary(&quiet, text, sizeof(text));
    CHECK(strstr(text, "No activity above the threshold") != NULL, "says nothing was seen");
    CHECK(strstr(text, "Most active") == NULL, "no ranking of silence");
}

int main(void) {
    test_rows();
    test_rotation();
    test_summary();
    printf("\n%d checks, %d failures\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
