/**
 * Host-side unit tests for the module load-cost model (helpers/rg_elf.c) on
 * ELF32 images built in memory.
 * Run: make -C test
 */
#include "../helpers/rg_elf.h"
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

static uint8_t g_img[4096];
static size_t g_len;
static int g_reads;

static bool mem_read(void* context, uint32_t offset, void* buf, size_t len) {
    (void)context;
    g_reads++;
    if((size_t)offset + len > g_len) return false;
    memcpy(buf, g_img + offset, len);
    return true;
}

static void put32(uint8_t* p, uint32_t v) {
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

static void put16(uint8_t* p, uint16_t v) {
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
}

typedef struct {
    const char* name;
    uint32_t flags;
    uint32_t size;
    uint32_t align;
} Sec;

/* An ELF32 image: section names at 0x100, the header table at 0x400. */
static void build(const Sec* secs, uint16_t n, uint16_t entsize) {
    memset(g_img, 0, sizeof(g_img));
    memcpy(
        g_img,
        "\x7f"
        "ELF",
        4);
    g_img[4] = 1; // ELFCLASS32
    g_img[5] = 1; // little-endian
    put32(g_img + 32, 0x400);
    put16(g_img + 46, entsize);
    put16(g_img + 48, n);
    put16(g_img + 50, (uint16_t)(n - 1)); // the last section holds the names
    uint32_t at = 1;
    for(uint16_t i = 0; i < n; i++) {
        uint8_t* sh = g_img + 0x400 + i * entsize;
        size_t len = strlen(secs[i].name);
        memcpy(g_img + 0x100 + at, secs[i].name, len + 1);
        put32(sh, i ? at : 0);
        at += (uint32_t)len + 1;
        put32(sh + 8, secs[i].flags);
        put32(sh + 20, secs[i].size);
        put32(sh + 32, secs[i].align);
    }
    uint8_t* names = g_img + 0x400 + (n - 1) * entsize;
    put32(names + 16, 0x100);
    put32(names + 20, at);
    g_len = 0x400 + (size_t)n * entsize;
}

static uint32_t block(uint32_t n) {
    return (n + 8u + 7u) & ~7u;
}

/* What the firmware loader takes for a module laid out as ufbt builds them. */
static const Sec k_module[] = {
    {"", 0, 0, 0},
    {".text", 0x6, 5604, 4},
    {".rel.text", 0x40, 7000, 4},
    {".rodata", 0x2, 870, 4},
    {".ARM.exidx", 0x82, 8, 4},
    {".rel.ARM.exidx", 0x40, 8, 4},
    {".data", 0x3, 8, 4},
    {".bss", 0x3, 396, 4},
    {".fapmeta", 0, 86, 1},
    {".gnu_debuglink", 0, 28, 4},
    {".fast.rel.text", 0, 900, 1},
    {".fast.rel.rodata", 0, 88, 1},
    {".fast.rel.ARM.exidx", 0, 21, 1},
    {".symtab", 0, 9000, 4},
    {".strtab", 0, 2000, 1},
    {".shstrtab", 0, 200, 1},
};

int main(void) {
    printf("test_elf\n");
    uint16_t n = (uint16_t)(sizeof(k_module) / sizeof(k_module[0]));
    RgElfCost c;
    build(k_module, n, 40);
    CHECK(rg_elf_load_cost(mem_read, NULL, &c), "a module is read");
    CHECK(c.sections == 5604 + 870 + 8 + 396, "allocated sections, the ARM tables skipped");
    uint32_t resident = block(5604 + 3 + 4) + block(870 + 3 + 4) + block(8 + 3 + 4) +
                        block(396 + 3 + 4) + 4 * RG_ELF_SECTION_BOOK + block(28);
    CHECK(c.resident == resident, "each section in its own aligned block, plus bookkeeping");
    CHECK(c.temporary == 2 * block(24) + block(900 + 4) + block(88 + 4), "relocation tables");
    CHECK(rg_elf_heap_block(1) == 16 && rg_elf_heap_block(8) == 16 && rg_elf_heap_block(9) == 24,
          "heap blocks: 8-byte header, 8-byte rounding");
    // Larger header entries are stepped over by their stated size.
    build(k_module, n, 48);
    RgElfCost c48;
    CHECK(rg_elf_load_cost(mem_read, NULL, &c48) && memcmp(&c, &c48, sizeof(c)) == 0,
          "entsize honoured");
    // Not an ELF32 little-endian file, or truncated: refused (the caller
    // falls back to the file's size).
    build(k_module, n, 40);
    g_img[4] = 2;
    CHECK(!rg_elf_load_cost(mem_read, NULL, &c), "ELF64 refused");
    build(k_module, n, 40);
    g_img[5] = 2;
    CHECK(!rg_elf_load_cost(mem_read, NULL, &c), "big-endian refused");
    build(k_module, n, 40);
    g_img[0] = 0;
    CHECK(!rg_elf_load_cost(mem_read, NULL, &c), "no magic");
    build(k_module, n, 40);
    g_len -= 10;
    CHECK(!rg_elf_load_cost(mem_read, NULL, &c), "truncated section table");
    g_len = 30;
    CHECK(!rg_elf_load_cost(mem_read, NULL, &c), "truncated header");
    build(k_module, 1, 40);
    CHECK(!rg_elf_load_cost(mem_read, NULL, &c), "no sections to load");
    build(k_module, n, 20);
    CHECK(!rg_elf_load_cost(mem_read, NULL, &c), "entries too small");
    build(k_module, n, 40);
    put16(g_img + 50, 900);
    CHECK(!rg_elf_load_cost(mem_read, NULL, &c), "names index out of range");
    build(k_module, n, 40);
    put32(g_img + 0x400 + 1 * 40, 5000); // .text's name beyond the names
    CHECK(!rg_elf_load_cost(mem_read, NULL, &c), "name offset out of range");
    build(k_module, n, 40);
    put32(g_img + 0x400 + 1 * 40 + 20, 0x7FFFFFFFu);
    CHECK(!rg_elf_load_cost(mem_read, NULL, &c), "absurd section size refused");
    // A corrupt section count does not make it read thousands of headers.
    build(k_module, n, 40);
    put16(g_img + 48, 60000);
    g_reads = 0;
    CHECK(!rg_elf_load_cost(mem_read, NULL, &c) && g_reads <= 1, "absurd count refused at once");
    printf("%d checks, %d failures\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
