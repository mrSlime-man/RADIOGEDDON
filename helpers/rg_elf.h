/**
 * @file rg_elf.h
 * @brief What loading an ELF32 module takes: the sum of its allocated sections.
 *
 * Before a module (radiogeddon_modules.h) is loaded the app checks it fits in
 * the free heap. The module file holds much more than the loader keeps
 * (symbols, relocations, debug links); what stays in RAM is its allocated
 * sections (SHF_ALLOC: code, read-only data, data, bss), read here from the
 * section header table.
 *
 * Pure C with no SDK includes; host-tested (test/test_elf.c).
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/** Read @p len bytes at @p offset of the file; true if all were read. */
typedef bool (*RgElfRead)(void* context, uint32_t offset, void* buf, size_t len);

/* Most section headers looked at (a module has a few dozen). */
#define RG_ELF_MAX_SECTIONS 512u

/**
 * Sum of the sizes of the SHF_ALLOC sections of a little-endian ELF32 file,
 * or 0 if it is not one or a read fails.
 */
size_t rg_elf_alloc_bytes(RgElfRead read, void* context);
