/* Host tool: rg_elf_load_cost on built module files, one line each
 * ("name sections resident temporary largest"), for scripts/module_cost.py
 * --check to compare with its own reading of the same files. */
#include "../helpers/rg_elf.h"
#include <stdio.h>
#include <string.h>

static bool file_read(void* context, uint32_t offset, void* buf, size_t len) {
    FILE* f = context;
    return fseek(f, (long)offset, SEEK_SET) == 0 && fread(buf, 1, len, f) == len;
}

int main(int argc, char** argv) {
    int rc = 0;
    for(int i = 1; i < argc; i++) {
        FILE* f = fopen(argv[i], "rb");
        RgElfCost c;
        if(!f || !rg_elf_load_cost(file_read, f, &c)) {
            printf("%s unreadable\n", argv[i]);
            rc = 1;
        } else {
            const char* base = strrchr(argv[i], '/');
            printf(
                "%s %u %u %u %u\n",
                base ? base + 1 : argv[i],
                (unsigned)c.sections,
                (unsigned)c.resident,
                (unsigned)c.temporary,
                (unsigned)c.largest);
        }
        if(f) fclose(f);
    }
    return rc;
}
