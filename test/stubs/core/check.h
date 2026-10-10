/* Host stand-in for the firmware's checks: a failed check aborts the test. */
#pragma once

#include <stdio.h>
#include <stdlib.h>

#define furi_check(cond, ...)                                                              \
    do {                                                                                   \
        if(!(cond)) {                                                                      \
            fprintf(stderr, "furi_check failed: %s (%s:%d)\n", #cond, __FILE__, __LINE__); \
            abort();                                                                       \
        }                                                                                  \
    } while(0)
#define furi_assert     furi_check
#define furi_crash(...) abort()
