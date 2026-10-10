/**
 * @file radiogeddon_progress.h
 * @brief Progress reporting for long SD-card operations (indexing, analysis).
 */
#pragma once

#include <stdint.h>

/**
 * Called from the thread doing the work with @p done of @p total units.
 * Units are the operation's own (files, kilobytes); only the ratio matters.
 */
typedef void (*RadioGeddonProgressCallback)(void* context, uint32_t done, uint32_t total);
