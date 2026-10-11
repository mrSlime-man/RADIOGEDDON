/**
 * @file radiogeddon_bits_view.h
 * @brief Full edition: Bitstream Explorer screen.
 *
 * Shows the frames the analyzer inferred from one RAW capture (an
 * RgAnalysis, owned by the caller and unchanged while shown) in five views,
 * cycled with OK:
 *
 *   FRAMES  one frame a line: index, start time, bits, pattern, how many
 *           frames share it, similarity to the reference; Up/Down select.
 *   BITS    the selected frame's bits, 24 a line with their position;
 *           Left/Right move the bit cursor, Up/Down a line; hold OK starts a
 *           field at the cursor.
 *   HEX     whole bytes from a bit offset, with the bits before the first
 *           byte and after the last whole byte shown apart as bits; Left/Right
 *           select a byte, hold OK moves the byte grid one bit.
 *   DIFF    the frame's bits over markers: '.' the same in every comparable
 *           frame, 'X' changes, '?' too few frames to tell.
 *   FIELD   a bit range: Up/Down move its start, Left/Right its end; binary,
 *           hex and decimal value of exactly those bits.
 *
 * Every bit is an inference from the encoding guess (HYPOTHESIS). Back
 * leaves the explorer.
 */
#pragma once

#include "../radiogeddon_edition.h"

#if RG_FEATURE_BITSTREAM

#include <gui/view.h>
#include "../helpers/rg_bits.h"

typedef struct RadioGeddonBitsView RadioGeddonBitsView;

typedef enum {
    RadioGeddonBitsModeFrames,
    RadioGeddonBitsModeBits,
    RadioGeddonBitsModeHex,
    RadioGeddonBitsModeDiff,
    RadioGeddonBitsModeField,
    RadioGeddonBitsModeCount,
} RadioGeddonBitsMode;

RadioGeddonBitsView* radiogeddon_bits_view_alloc(void);
void radiogeddon_bits_view_free(RadioGeddonBitsView* instance);
View* radiogeddon_bits_view_get_view(RadioGeddonBitsView* instance);

/** Show @p analysis (NULL: nothing); starts on the reference frame. */
void radiogeddon_bits_view_set_analysis(RadioGeddonBitsView* instance, const RgAnalysis* analysis);

/** Mode and selected frame, kept by the scene across a trip elsewhere. */
void radiogeddon_bits_view_set_mode(RadioGeddonBitsView* instance, RadioGeddonBitsMode mode);
RadioGeddonBitsMode radiogeddon_bits_view_get_mode(RadioGeddonBitsView* instance);

#endif
