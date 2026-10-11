/**
 * Host-side unit tests for the module size estimate (helpers/rg_elf.c) on
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

/* An ELF32 image with sections of these flags and sizes at offset 0x200. */
static void build(const uint32_t* flags, const uint32_t* sizes, uint16_t n, uint16_t entsize) {
    memset(g_img, 0, sizeof(g_img));
    memcpy(
        g_img,
        "\x7f"
        "ELF",
        4);
    g_img[4] = 1; // ELFCLASS32
    g_img[5] = 1; // little-endian
    put32(g_img + 32, 0x200);
    put16(g_img + 46, entsize);
    put16(g_img + 48, n);
    for(uint16_t i = 0; i < n; i++) {
        uint8_t* sh = g_img + 0x200 + i * entsize;
        put32(sh + 8, flags[i]);
        put32(sh + 20, sizes[i]);
    }
    g_len = 0x200 + (size_t)n * entsize;
}

int main(void) {
    printf("test_elf\n");
    // null, .text (AX), .rodata (A), .data (WA), .bss (WA), .symtab, .rel.text, .debug
    uint32_t flags[] = {0, 0x6, 0x2, 0x3, 0x3, 0, 0x40, 0};
    uint32_t sizes[] = {0, 5604, 870, 8, 396, 9000, 7000, 20000};
    build(flags, sizes, 8, 40);
    CHECK(rg_elf_alloc_bytes(mem_read, NULL) == 5604 + 870 + 8 + 396, "allocated sections only");
    // Larger header entries are stepped over by their stated size.
    build(flags, sizes, 8, 48);
    CHECK(rg_elf_alloc_bytes(mem_read, NULL) == 6878, "entsize honoured");
    // Not an ELF32 little-endian file, or truncated: 0 (the caller falls back).
    build(flags, sizes, 8, 40);
    g_img[4] = 2;
    CHECK(rg_elf_alloc_bytes(mem_read, NULL) == 0, "ELF64 refused");
    build(flags, sizes, 8, 40);
    g_img[5] = 2;
    CHECK(rg_elf_alloc_bytes(mem_read, NULL) == 0, "big-endian refused");
    build(flags, sizes, 8, 40);
    g_img[0] = 0;
    CHECK(rg_elf_alloc_bytes(mem_read, NULL) == 0, "no magic");
    build(flags, sizes, 8, 40);
    g_len -= 10;
    CHECK(rg_elf_alloc_bytes(mem_read, NULL) == 0, "truncated section table");
    g_len = 30;
    CHECK(rg_elf_alloc_bytes(mem_read, NULL) == 0, "truncated header");
    build(flags, sizes, 0, 40);
    CHECK(rg_elf_alloc_bytes(mem_read, NULL) == 0, "no sections");
    build(flags, sizes, 8, 20);
    CHECK(rg_elf_alloc_bytes(mem_read, NULL) == 0, "entries too small");
    // A corrupt section count does not make it read thousands of headers.
    build(flags, sizes, 8, 40);
    put16(g_img + 48, 60000);
    g_reads = 0;
    CHECK(rg_elf_alloc_bytes(mem_read, NULL) == 0 && g_reads <= 1, "absurd count refused at once");
    printf("%d checks, %d failures\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
