/* The heap model (stubs/heap4.h) against the rules of the firmware's heap_4. */
#include <stdio.h>
#include <string.h>

#include "stubs/heap4.h"

static int checks, failures;
#define CHECK(c, msg)                                \
    do {                                             \
        checks++;                                    \
        if(!(c)) {                                   \
            failures++;                              \
            printf("FAIL %s:%d %s\n", __FILE__, __LINE__, msg); \
        }                                            \
    } while(0)

static size_t block(size_t n) {
    return (n + HEAP4_HEADER + HEAP4_ALIGNMENT - 1u) & ~(size_t)(HEAP4_ALIGNMENT - 1u);
}

static void test_first_fit_and_split(void) {
    printf("test_first_fit_and_split\n");
    heap4_init(4096);
    size_t arena = heap4_max_free_block();
    CHECK(arena == 4096 - HEAP4_HEADER, "the end marker takes one header");
    unsigned char* a = heap4_malloc(10);
    unsigned char* b = heap4_malloc(100);
    CHECK(a && b && (size_t)(b - a) == block(10), "blocks follow each other");
    CHECK(((size_t)a % HEAP4_ALIGNMENT) == 0 && ((size_t)b % HEAP4_ALIGNMENT) == 0, "aligned");
    CHECK(heap4_max_free_block() == arena - block(10) - block(100), "the rest stays one block");
    for(int i = 0; i < 100; i++)
        CHECK(b[i] == 0, "zeroed");
    heap4_free(a);
    // A request that fits the first hole goes there (lowest address first).
    unsigned char* c = heap4_malloc(8);
    CHECK(c == a, "first fit reuses the lowest hole");
    heap4_free(c);
    // Remainder of 16 bytes or less is not split off.
    unsigned char* d = heap4_malloc(2);
    CHECK(d == a, "small block in the hole");
    Heap4Stats st;
    heap4_stats(&st);
    CHECK(st.free_blocks == 1, "an unsplit hole leaves no fragment");
    heap4_free(d);
    heap4_free(b);
    CHECK(heap4_max_free_block() == arena, "all merged back");
    CHECK(heap4_check(), "free list consistent");
}

static void test_merge_both_sides(void) {
    printf("test_merge_both_sides\n");
    heap4_init(8192);
    size_t arena = heap4_max_free_block();
    void* p[6];
    for(int i = 0; i < 6; i++)
        p[i] = heap4_malloc(200);
    heap4_free(p[1]);
    heap4_free(p[3]);
    Heap4Stats st;
    heap4_stats(&st);
    CHECK(st.free_blocks == 3, "two holes and the tail");
    heap4_free(p[2]); // joins 1, 2 and 3
    heap4_stats(&st);
    CHECK(st.free_blocks == 2, "merged with both neighbours");
    CHECK(st.max_free_block == arena - 6 * block(200), "tail is the largest");
    heap4_free(p[5]); // joins the tail
    heap4_free(p[0]);
    heap4_free(p[4]);
    heap4_stats(&st);
    CHECK(st.free_blocks == 1 && st.max_free_block == arena, "one block again");
    CHECK(st.live_blocks == 0 && st.live_bytes == 0, "nothing live");
    CHECK(heap4_check(), "free list consistent");
}

static void test_failure_and_injection(void) {
    printf("test_failure_and_injection\n");
    heap4_init(2048);
    CHECK(heap4_malloc(4096) == NULL, "too large fails");
    void* a = heap4_malloc(64);
    heap4_fail_at(2);
    void* b = heap4_malloc(64);
    void* c = heap4_malloc(64);
    void* d = heap4_malloc(64);
    CHECK(b && !c && d, "the second allocation from now fails, then it works again");
    Heap4Stats st;
    heap4_stats(&st);
    CHECK(st.failures == 2, "failures counted");
    heap4_free(a);
    heap4_free(b);
    heap4_free(d);
    CHECK(heap4_check(), "free list consistent");
}

static void test_fragment(void) {
    printf("test_fragment\n");
    heap4_init(64 * 1024);
    size_t before = heap4_max_free_block();
    size_t pins = heap4_fragment(1000, 24, 8192);
    Heap4Stats st;
    heap4_stats(&st);
    CHECK(pins > 10, "pins placed");
    CHECK(st.max_free_block < before / 2, "largest block shrinks");
    CHECK(st.free_blocks > pins / 2, "many fragments");
    CHECK(st.free_bytes > 8192 + pins * 1000 / 2, "free space is still there");
    heap4_unpin();
    CHECK(heap4_max_free_block() == before, "unpinned, one block");
    CHECK(heap4_check(), "free list consistent");
}

static void test_peaks(void) {
    printf("test_peaks\n");
    heap4_init(16384);
    heap4_reset_marks();
    void* a = heap4_malloc(1000);
    void* b = heap4_malloc(3000);
    heap4_free(a);
    void* c = heap4_malloc(500);
    Heap4Stats st;
    heap4_stats(&st);
    CHECK(st.peak_live_bytes == 4000, "peak of requested bytes");
    CHECK(st.peak_used_bytes == block(1000) + block(3000), "peak of block bytes");
    CHECK(st.live_bytes == 3500 && st.allocs == 3 && st.frees == 1, "counts");
    heap4_free(b);
    heap4_free(c);
}

int main(void) {
    test_first_fit_and_split();
    test_merge_both_sides();
    test_failure_and_injection();
    test_fragment();
    test_peaks();
    printf("\n%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
