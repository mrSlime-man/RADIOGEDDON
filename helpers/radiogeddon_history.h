/**
 * @file radiogeddon_history.h
 * @brief Volatile per-session history of decoded known-protocol signals.
 *
 * Each entry keeps the complete serialized .sub payload so any decoded signal
 * can later be persisted to the database verbatim, plus a short display string.
 */
#pragma once

#include <furi.h>

#define RADIOGEDDON_HISTORY_MAX 32

typedef struct RadioGeddonHistory RadioGeddonHistory;

RadioGeddonHistory* radiogeddon_history_alloc(void);
void radiogeddon_history_free(RadioGeddonHistory* instance);

/** Remove all entries. */
void radiogeddon_history_reset(RadioGeddonHistory* instance);

size_t radiogeddon_history_count(RadioGeddonHistory* instance);

/**
 * Add a decoded signal. De-duplicates consecutive identical (name+hash) hits.
 * @param serialized complete .sub content (may be NULL if unavailable)
 * @return true if a new entry was added
 */
bool radiogeddon_history_add(
    RadioGeddonHistory* instance,
    const char* protocol_name,
    uint8_t hash,
    FuriString* text,
    FuriString* serialized);

const char* radiogeddon_history_get_name(RadioGeddonHistory* instance, size_t index);
FuriString* radiogeddon_history_get_text(RadioGeddonHistory* instance, size_t index);
FuriString* radiogeddon_history_get_serialized(RadioGeddonHistory* instance, size_t index);
bool radiogeddon_history_has_serialized(RadioGeddonHistory* instance, size_t index);
