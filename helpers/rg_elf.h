/**
 * @file rg_elf.h
 * @brief What loading an ELF32 module takes from the heap.
 *
 * Before a module (radiogeddon_modules.h) is loaded the app checks it fits in
 * the free heap. The module file holds much more than the loader keeps
 * (symbols, relocations, debug links). This reads the section header table
 * and models what the firmware's loader (lib/flipper_application/elf,
 * firmware 1.4.3) allocates for it:
 *
 *   - every allocated section (SHF_ALLOC: code, read-only data, data, bss)
 *     except the .ARM.* unwind tables, which it skips, in a block of its own
 *     from aligned_malloc (size + alignment - 1 + a pointer, plus the heap's
 *     8-byte header, rounded to 8), kept while the module is loaded, with a
 *     small bookkeeping entry each (its name and dictionary node);
 *   - the .gnu_debuglink section, kept too;
 *   - each .fast.rel.* table (the relocations, precomputed at build time),
 *     loaded with the sections and freed once its section is relocated:
 *     temporary, gone before the module's code first runs.
 *
 * The loader's own fixed structures (its file handle, manifest, plugin
 * manager) are the caller's to add (radiogeddon_module.c).
 *
 * Pure C with no SDK includes; host-tested (test/test_elf.c) and checked
 * against the built modules (scripts/module_cost.py).
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/** Read @p len bytes at @p offset of the file; true if all were read. */
typedef bool (*RgElfRead)(void* context, uint32_t offset, void* buf, size_t len);

/* Most section headers looked at (a module has a few dozen). */
#define RG_ELF_MAX_SECTIONS 512u

/* Heap bookkeeping of one section entry in the loader's dictionary: its
 * name (strdup) and node, generously. */
#define RG_ELF_SECTION_BOOK 64u

typedef struct {
    uint32_t sections; /* bytes of the sections the loader keeps */
    uint32_t resident; /* heap they take while loaded (blocks, debug link, bookkeeping) */
    uint32_t temporary; /* heap of the relocation tables, freed before the code runs */
    uint32_t largest; /* the largest single block the loader asks for */
} RgElfCost;

/** Heap block the firmware's allocator takes for @p size bytes. */
uint32_t rg_elf_heap_block(uint32_t size);

/**
 * The load cost of a little-endian ELF32 file; false if it is not one or a
 * read fails.
 */
bool rg_elf_load_cost(RgElfRead read, void* context, RgElfCost* out);
