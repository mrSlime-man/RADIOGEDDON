#include "rg_elf.h"
#include "../radiogeddon_edition.h"

#if RG_EDITION_FULL

#include <string.h>

#define RG_ELF_HEADER_SIZE  52u
#define RG_ELF_SECTION_SIZE 40u
#define RG_ELF_SHF_ALLOC    0x2u
/* Longest section name prefix compared (".fast.rel.ARM." and a NUL). */
#define RG_ELF_NAME_PEEK    15u

static uint32_t rg_elf_u32(const uint8_t* p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}

static uint16_t rg_elf_u16(const uint8_t* p) {
    return (uint16_t)(p[0] | (p[1] << 8));
}

static bool rg_elf_prefix(const char* name, const char* prefix) {
    return strncmp(name, prefix, strlen(prefix)) == 0;
}

uint32_t rg_elf_heap_block(uint32_t size) {
    // pvPortMalloc: an 8-byte header, rounded up to 8 bytes.
    return (size + 8u + 7u) & ~7u;
}

/* aligned_malloc(size, align): malloc(size + align - 1 + sizeof(void*)). */
static uint32_t rg_elf_aligned_block(uint32_t size, uint32_t align) {
    if(align == 0) align = 1;
    return rg_elf_heap_block(size + align - 1u + 4u);
}

bool rg_elf_load_cost(RgElfRead read, void* context, RgElfCost* out) {
    memset(out, 0, sizeof(*out));
    uint8_t h[RG_ELF_HEADER_SIZE];
    if(!read(context, 0, h, sizeof(h))) return false;
    // Magic, 32-bit class, little-endian.
    if(memcmp(
           h,
           "\x7f"
           "ELF",
           4) != 0 ||
       h[4] != 1 || h[5] != 1)
        return false;
    uint32_t shoff = rg_elf_u32(h + 32);
    uint16_t shentsize = rg_elf_u16(h + 46);
    uint16_t shnum = rg_elf_u16(h + 48);
    uint16_t shstrndx = rg_elf_u16(h + 50);
    if(shoff == 0 || shnum == 0 || shnum > RG_ELF_MAX_SECTIONS ||
       shentsize < RG_ELF_SECTION_SIZE || shstrndx >= shnum)
        return false;
    uint8_t sh[RG_ELF_SECTION_SIZE];
    if(!read(context, shoff + (uint32_t)shstrndx * shentsize, sh, sizeof(sh))) return false;
    uint32_t names = rg_elf_u32(sh + 16);
    uint32_t names_size = rg_elf_u32(sh + 20);

    for(uint32_t i = 1; i < shnum; i++) {
        if(!read(context, shoff + i * shentsize, sh, sizeof(sh))) return false;
        uint32_t name_off = rg_elf_u32(sh);
        uint32_t flags = rg_elf_u32(sh + 8);
        uint32_t size = rg_elf_u32(sh + 20);
        uint32_t align = rg_elf_u32(sh + 32);
        if(size > 0x00FFFFFFu || align > 0x10000u) return false; // nothing a Flipper loads
        char name[RG_ELF_NAME_PEEK + 1];
        uint32_t peek = RG_ELF_NAME_PEEK;
        if(name_off >= names_size) return false;
        if(names_size - name_off < peek) peek = names_size - name_off;
        if(!read(context, names + name_off, name, peek)) return false;
        name[peek] = '\0';

        // The loader skips the ARM unwind tables and their relocations.
        if(rg_elf_prefix(name, ".ARM.") || rg_elf_prefix(name, ".rel.ARM.") ||
           rg_elf_prefix(name, ".fast.rel.ARM."))
            continue;
        uint32_t block = 0;
        if(flags & RG_ELF_SHF_ALLOC) {
            block = size ? rg_elf_aligned_block(size, align) : 0;
            out->sections += size;
            out->resident += block + RG_ELF_SECTION_BOOK;
        } else if(rg_elf_prefix(name, ".fast.rel")) {
            // Its ELFSection record (24 bytes) and the table itself.
            block = size ? rg_elf_aligned_block(size, align) : 0;
            out->temporary += rg_elf_heap_block(24u) + block;
        } else if(strcmp(name, ".gnu_debuglink") == 0) {
            block = rg_elf_heap_block(size);
            out->resident += block;
        }
        if(block > out->largest) out->largest = block;
        // Far beyond any Flipper's RAM: not a module (and no sum overflows).
        if(out->resident > 0x01000000u || out->temporary > 0x01000000u) return false;
    }
    return out->sections > 0;
}

#endif
