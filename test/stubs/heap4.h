/**
 * A host model of the Flipper Zero firmware's heap (furi/core/memmgr_heap.c,
 * FreeRTOS heap_4): one arena, an 8-byte block header, 8-byte alignment,
 * first fit over a free list kept in address order, a block split only when
 * more than 16 bytes would remain, and neighbouring free blocks merged on
 * free. memmgr_heap_get_max_free_block() on the device reports the largest
 * free block's size including its header; heap4_max_free_block() does too.
 *
 * Tests route the code under test's malloc/free here (STUB_HEAP4 with
 * STUB_TRACK_ALLOC, stubs/furi.h) to measure what the device's allocator
 * would see: peak use, the largest block left, how many free fragments, and
 * allocations that would fail (where the firmware stops with "out of
 * memory"). Sizes are the host's: pointers and size_t are 8 bytes here and
 * 4 on the device, so structures holding them are larger than on a Flipper
 * (scripts/struct_sizes.py gives the device's sizes).
 *
 * Under AddressSanitizer the headers, free blocks and alignment padding are
 * poisoned, so overflows and use after free inside the arena are still
 * reported.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define HEAP4_HEADER    8u
#define HEAP4_ALIGNMENT 8u

typedef struct {
    size_t arena; /* bytes the arena holds */
    size_t free_bytes; /* in free blocks, headers included (xFreeBytesRemaining) */
    size_t min_free_bytes; /* lowest free_bytes since the last reset */
    size_t max_free_block; /* largest free block, header included */
    size_t min_max_free_block; /* lowest max_free_block after any allocation */
    size_t free_blocks; /* fragments in the free list */
    size_t live_blocks;
    size_t live_bytes; /* requested bytes of the live blocks */
    size_t peak_live_bytes;
    size_t used_bytes; /* arena - free_bytes: block sizes with headers */
    size_t peak_used_bytes;
    uint32_t allocs;
    uint32_t frees;
    uint32_t failures; /* allocations that found no block (device: a crash) */
} Heap4Stats;

/** (Re)create the arena with @p bytes; every earlier block is forgotten. */
void heap4_init(size_t bytes);

/** True after heap4_init. */
bool heap4_active(void);

/** NULL when no free block fits (counted in failures). Memory is zeroed, as
 * the firmware's pvPortMalloc does. */
void* heap4_malloc(size_t size);
void heap4_free(void* p);

/** True when @p p points into the arena. */
bool heap4_owns(const void* p);

/** Bytes asked for when block @p p was allocated. */
size_t heap4_requested(const void* p);

size_t heap4_max_free_block(void);
size_t heap4_free_bytes(void);

/** Statistics; the min and peak values run from the last heap4_reset_marks. */
void heap4_stats(Heap4Stats* out);
void heap4_reset_marks(void);

/**
 * Fail allocation number @p n from now (1 = the next one), then work again;
 * 0 turns it off. Lets a test check what happens where the device would run
 * out of memory.
 */
void heap4_fail_at(uint32_t n);

/**
 * Fragment the arena: allocate blocks of @p hole and @p pin bytes in turn
 * until the free space is used, then free the @p hole ones, leaving pinned
 * blocks between free holes (like a long-running app's leftovers). Returns
 * how many pins were placed; heap4_unpin releases them.
 */
size_t heap4_fragment(size_t hole, size_t pin, size_t keep_free);
void heap4_unpin(void);

/** Consistency check of the free list (sorted, merged, sizes add up). */
bool heap4_check(void);
