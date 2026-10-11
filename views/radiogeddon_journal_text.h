/**
 * @file radiogeddon_journal_text.h
 * @brief Full edition (in the lab module): the Research Journal's pages.
 */
#pragma once

#include "../radiogeddon_edition.h"

#if RG_FEATURE_JOURNAL

#include <stddef.h>
#include <stdint.h>
#include "../helpers/rg_journal.h"

/** Level, counted events, identified protocols and the recent history. */
size_t radiogeddon_journal_text(const RgJournal* journal, bool xp_on, char* out, size_t size);

/** Badges earned (with when; @p fresh marked new), open ones with hints,
 * and the decorations by level. */
size_t radiogeddon_journal_achievements_text(
    const RgJournal* journal,
    uint32_t fresh,
    char* out,
    size_t size);

#endif
