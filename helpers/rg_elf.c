#include "rg_elf.h"
#include "../radiogeddon_edition.h"

#if RG_EDITION_FULL

#include <string.h>

#define RG_ELF_HEADER_SIZE  52u
#define RG_ELF_SECTION_SIZE 40u
#define RG_ELF_SHF_ALLOC    0x2u

static uint32_t rg_elf_u32(const uint8_t* p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}

static uint16_t rg_elf_u16(const uint8_t* p) {
    return (uint16_t)(p[0] | (p[1] << 8));
}

size_t rg_elf_alloc_bytes(RgElfRead read, void* context) {
    uint8_t h[RG_ELF_HEADER_SIZE];
    if(!read(context, 0, h, sizeof(h))) return 0;
    // Magic, 32-bit class, little-endian.
    if(memcmp(
           h,
           "\x7f"
           "ELF",
           4) != 0 ||
       h[4] != 1 || h[5] != 1)
        return 0;
    uint32_t shoff = rg_elf_u32(h + 32);
    uint16_t shentsize = rg_elf_u16(h + 46);
    uint16_t shnum = rg_elf_u16(h + 48);
    if(shoff == 0 || shnum == 0 || shnum > RG_ELF_MAX_SECTIONS || shentsize < RG_ELF_SECTION_SIZE)
        return 0;
    size_t total = 0;
    uint8_t sh[RG_ELF_SECTION_SIZE];
    for(uint32_t i = 0; i < shnum; i++) {
        if(!read(context, shoff + i * shentsize, sh, sizeof(sh))) return 0;
        uint32_t flags = rg_elf_u32(sh + 8);
        uint32_t size = rg_elf_u32(sh + 20);
        if(flags & RG_ELF_SHF_ALLOC) total += size;
    }
    return total;
}

#endif
