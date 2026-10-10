/**
 * Replays fuzz inputs through a harness without libFuzzer, so the committed
 * corpus runs as a regression test under any compiler (`make -C test check`).
 *
 *   replay <file-or-folder>...
 *
 * Folders are read one level deep. Prints how many inputs ran; a harness
 * that finds a broken invariant aborts, which fails the run.
 */
#define _POSIX_C_SOURCE 200809L
#include <dirent.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size);

static int run_file(const char* path) {
    FILE* f = fopen(path, "rb");
    if(!f) {
        fprintf(stderr, "cannot open %s\n", path);
        return -1;
    }
    size_t cap = 4096, len = 0;
    uint8_t* buf = malloc(cap);
    size_t n;
    while(buf && (n = fread(buf + len, 1, cap - len, f)) > 0) {
        len += n;
        if(len == cap) {
            cap *= 2;
            uint8_t* grown = realloc(buf, cap);
            if(!grown) free(buf);
            buf = grown;
        }
    }
    fclose(f);
    if(!buf) return -1;
    /* An exact-size copy, so ASan sees reads past the end of the input. */
    uint8_t* exact = malloc(len ? len : 1);
    if(!exact) {
        free(buf);
        return -1;
    }
    memcpy(exact, buf, len);
    free(buf);
    LLVMFuzzerTestOneInput(exact, len);
    free(exact);
    return 1;
}

int main(int argc, char** argv) {
    int inputs = 0;
    for(int i = 1; i < argc; i++) {
        struct stat st;
        if(stat(argv[i], &st) != 0) {
            fprintf(stderr, "missing %s\n", argv[i]);
            return 1;
        }
        if(!S_ISDIR(st.st_mode)) {
            if(run_file(argv[i]) < 0) return 1;
            inputs++;
            continue;
        }
        DIR* dir = opendir(argv[i]);
        if(!dir) return 1;
        struct dirent* de;
        while((de = readdir(dir)) != NULL) {
            if(de->d_name[0] == '.') continue;
            char path[1024];
            snprintf(path, sizeof(path), "%s/%s", argv[i], de->d_name);
            if(stat(path, &st) != 0 || !S_ISREG(st.st_mode)) continue;
            if(run_file(path) < 0) {
                closedir(dir);
                return 1;
            }
            inputs++;
        }
        closedir(dir);
    }
    if(inputs == 0) {
        fprintf(stderr, "no inputs\n");
        return 1;
    }
    printf("%s: %d inputs replayed\n", argv[0], inputs);
    return 0;
}
