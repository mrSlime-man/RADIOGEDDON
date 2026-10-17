/* Host model of the firmware's heap_4 allocator (see heap4.h). */
#include "heap4.h"

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(__SANITIZE_ADDRESS__)
#define HEAP4_ASAN 1
#elif defined(__has_feature)
#if __has_feature(address_sanitizer)
#define HEAP4_ASAN 1
#endif
#endif

#ifdef HEAP4_ASAN
#include <sanitizer/asan_interface.h>
#define HEAP4_POISON(p, n)   ASAN_POISON_MEMORY_REGION((p), (n))
#define HEAP4_UNPOISON(p, n) ASAN_UNPOISON_MEMORY_REGION((p), (n))
/* The allocator itself reads and writes headers inside poisoned memory. */
#define HEAP4_RAW __attribute__((no_sanitize("address")))
#else
#define HEAP4_POISON(p, n)   ((void)(p), (void)(n))
#define HEAP4_UNPOISON(p, n) ((void)(p), (void)(n))
#define HEAP4_RAW
#endif

#define ALLOCATED   0x80000000u
#define END         0xFFFFFFFFu
/* heapMINIMUM_BLOCK_SIZE: a block is split only if more than this remains. */
#define MIN_SPLIT   (2u * HEAP4_HEADER)
#define MAX_PINS    8192u

typedef struct {
    uint32_t size; /* block size with header; ALLOCATED bit while in use */
    uint32_t next; /* free: offset of the next free block (END); used: bytes asked */
} Hdr;

static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static uint8_t* arena;
static uint32_t arena_size; /* usable bytes, the end marker excluded */
static uint32_t first_free = END; /* xStart.pxNextFreeBlock */
static Heap4Stats st;
static uint32_t fail_countdown;
static void* pins[MAX_PINS];
static size_t pin_count;

static HEAP4_RAW Hdr* hdr(uint32_t off) {
    return (Hdr*)(arena + off);
}

static HEAP4_RAW size_t largest_free(size_t* blocks) {
    size_t best = 0, n = 0;
    for(uint32_t o = first_free; o != END; o = hdr(o)->next) {
        if(hdr(o)->size > best) best = hdr(o)->size;
        n++;
    }
    if(blocks) *blocks = n;
    return best;
}

static void update_marks(void) {
    st.max_free_block = largest_free(&st.free_blocks);
    st.used_bytes = st.arena - st.free_bytes;
    if(st.free_bytes < st.min_free_bytes) st.min_free_bytes = st.free_bytes;
    if(st.max_free_block < st.min_max_free_block) st.min_max_free_block = st.max_free_block;
    if(st.used_bytes > st.peak_used_bytes) st.peak_used_bytes = st.used_bytes;
    if(st.live_bytes > st.peak_live_bytes) st.peak_live_bytes = st.live_bytes;
}

void heap4_init(size_t bytes) {
    pthread_mutex_lock(&lock);
    if(arena) {
        HEAP4_UNPOISON(arena, arena_size + HEAP4_HEADER);
        (free)(arena);
    }
    bytes &= ~(size_t)(HEAP4_ALIGNMENT - 1u);
    arena = (aligned_alloc)(HEAP4_ALIGNMENT, bytes);
    if(!arena) abort();
    /* prvHeapInit: the end marker takes one header at the top. */
    arena_size = (uint32_t)(bytes - HEAP4_HEADER);
    first_free = 0;
    hdr(0)->size = arena_size;
    hdr(0)->next = END;
    memset(&st, 0, sizeof(st));
    st.arena = arena_size;
    st.free_bytes = arena_size;
    st.min_free_bytes = arena_size;
    st.min_max_free_block = arena_size;
    fail_countdown = 0;
    pin_count = 0;
    update_marks();
    HEAP4_POISON(arena, bytes);
    pthread_mutex_unlock(&lock);
}

bool heap4_active(void) {
    return arena != NULL;
}

bool heap4_owns(const void* p) {
    const uint8_t* b = p;
    return arena && b >= arena && b < arena + arena_size;
}

HEAP4_RAW size_t heap4_requested(const void* p) {
    const uint8_t* u = p;
    return hdr((uint32_t)(u - arena) - HEAP4_HEADER)->next;
}

/* prvInsertBlockIntoFreeList: address order, merged with both neighbours. */
static HEAP4_RAW void insert_free(uint32_t off) {
    uint32_t prev = END, it = first_free;
    while(it != END && it < off) {
        prev = it;
        it = hdr(it)->next;
    }
    Hdr* b = hdr(off);
    b->next = it;
    if(it != END && off + b->size == it) {
        b->size += hdr(it)->size;
        b->next = hdr(it)->next;
    }
    if(prev != END && prev + hdr(prev)->size == off) {
        hdr(prev)->size += b->size;
        hdr(prev)->next = b->next;
    } else if(prev != END) {
        hdr(prev)->next = off;
    } else {
        first_free = off;
    }
}

static HEAP4_RAW void* malloc_locked(size_t size) {
    if(!arena || size == 0 || size > 0x7FFFFFF0u) return NULL;
    st.allocs++;
    if(fail_countdown && --fail_countdown == 0) {
        st.failures++;
        return NULL;
    }
    uint32_t want = (uint32_t)size + HEAP4_HEADER;
    want = (want + HEAP4_ALIGNMENT - 1u) & ~(HEAP4_ALIGNMENT - 1u);
    uint32_t prev = END, it = first_free;
    while(it != END && hdr(it)->size < want) {
        prev = it;
        it = hdr(it)->next;
    }
    if(it == END) {
        st.failures++;
        return NULL;
    }
    Hdr* b = hdr(it);
    uint32_t next = b->next;
    if(b->size - want > MIN_SPLIT) {
        uint32_t rest = it + want;
        hdr(rest)->size = b->size - want;
        hdr(rest)->next = next;
        next = rest;
        b->size = want;
    }
    if(prev == END)
        first_free = next;
    else
        hdr(prev)->next = next;
    st.free_bytes -= b->size;
    b->next = (uint32_t)size;
    b->size |= ALLOCATED;
    st.live_blocks++;
    st.live_bytes += size;
    uint8_t* p = arena + it + HEAP4_HEADER;
    HEAP4_UNPOISON(p, size);
    memset(p, 0, size); /* pvPortMalloc wipes the block */
    return p;
}

void* heap4_malloc(size_t size) {
    pthread_mutex_lock(&lock);
    void* p = malloc_locked(size);
    update_marks();
    pthread_mutex_unlock(&lock);
    return p;
}

static HEAP4_RAW void free_locked(void* p) {
    uint8_t* u = p;
    if(!heap4_owns(u) || (size_t)(u - arena) < HEAP4_HEADER) {
        fprintf(stderr, "heap4: free of %p outside the arena\n", p);
        abort();
    }
    uint32_t off = (uint32_t)(u - arena) - HEAP4_HEADER;
    Hdr* b = hdr(off);
    if(!(b->size & ALLOCATED)) {
        fprintf(stderr, "heap4: double free of %p\n", p);
        abort();
    }
    b->size &= ~ALLOCATED;
    st.live_blocks--;
    st.live_bytes -= b->next;
    st.free_bytes += b->size;
    st.frees++;
    HEAP4_POISON(arena + off, b->size);
    insert_free(off);
}

void heap4_free(void* p) {
    if(!p) return;
    pthread_mutex_lock(&lock);
    free_locked(p);
    update_marks();
    pthread_mutex_unlock(&lock);
}

size_t heap4_max_free_block(void) {
    pthread_mutex_lock(&lock);
    size_t n = arena ? largest_free(NULL) : 0;
    pthread_mutex_unlock(&lock);
    return n;
}

size_t heap4_free_bytes(void) {
    return st.free_bytes;
}

void heap4_stats(Heap4Stats* out) {
    pthread_mutex_lock(&lock);
    update_marks();
    *out = st;
    pthread_mutex_unlock(&lock);
}

void heap4_reset_marks(void) {
    pthread_mutex_lock(&lock);
    update_marks();
    st.min_free_bytes = st.free_bytes;
    st.min_max_free_block = st.max_free_block;
    st.peak_used_bytes = st.used_bytes;
    st.peak_live_bytes = st.live_bytes;
    st.allocs = st.frees = st.failures = 0;
    pthread_mutex_unlock(&lock);
}

void heap4_fail_at(uint32_t n) {
    pthread_mutex_lock(&lock);
    fail_countdown = n;
    pthread_mutex_unlock(&lock);
}

size_t heap4_fragment(size_t hole, size_t pin, size_t keep_free) {
    void* holes[MAX_PINS];
    size_t n = 0;
    pthread_mutex_lock(&lock);
    while(pin_count < MAX_PINS && n < MAX_PINS && st.free_bytes > keep_free + hole + pin + 32u) {
        void* h = malloc_locked(hole);
        void* p = malloc_locked(pin);
        if(!h || !p) {
            if(h) free_locked(h);
            if(p) free_locked(p);
            break;
        }
        holes[n++] = h;
        pins[pin_count++] = p;
    }
    for(size_t i = 0; i < n; i++)
        free_locked(holes[i]);
    st.allocs = st.frees = st.failures = 0;
    update_marks();
    pthread_mutex_unlock(&lock);
    return pin_count;
}

void heap4_unpin(void) {
    pthread_mutex_lock(&lock);
    for(size_t i = 0; i < pin_count; i++)
        free_locked(pins[i]);
    pin_count = 0;
    update_marks();
    pthread_mutex_unlock(&lock);
}

HEAP4_RAW bool heap4_check(void) {
    pthread_mutex_lock(&lock);
    bool ok = true;
    size_t total = 0;
    uint32_t prev = END;
    for(uint32_t o = first_free; o != END; o = hdr(o)->next) {
        Hdr* b = hdr(o);
        if((b->size & ALLOCATED) || o % HEAP4_ALIGNMENT || o + b->size > arena_size) ok = false;
        if(prev != END && (o <= prev || prev + hdr(prev)->size >= o)) ok = false; /* order, merged */
        total += b->size;
        prev = o;
        if(!ok) break;
    }
    if(total != st.free_bytes) ok = false;
    pthread_mutex_unlock(&lock);
    return ok;
}
