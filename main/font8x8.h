#pragma once

#include <stdint.h>

/*
 * Compact ASCII font: 5x7 glyphs stored in 8x8 cells.
 *
 * Each glyph is 8 rows; within a row the leftmost 5 bits (MSB first) carry the
 * pixels and the low 3 bits are inter-character spacing. Row 7 is blank except
 * for descenders. Written as binary literals so the shapes stay readable.
 *
 * Coverage is 0x20..0x7E. Lowercase letters reuse the uppercase glyphs, which
 * keeps the table small; that is fine for status/diagnostic text.
 */

#define FONT8X8_WIDTH   8
#define FONT8X8_HEIGHT  8

#define FONT8X8_FIRST   0x20
#define FONT8X8_LAST    0x5F

extern const uint8_t font8x8_table[FONT8X8_LAST - FONT8X8_FIRST + 1][FONT8X8_HEIGHT];

/*
 * Look up a glyph, folding lowercase to uppercase and substituting a blank for
 * anything outside the table so callers never need to range-check.
 */
static inline const uint8_t *font8x8_glyph(unsigned char c)
{
    if (c >= 'a' && c <= 'z') {
        c -= 32;
    }
    if (c < FONT8X8_FIRST || c > FONT8X8_LAST) {
        c = ' ';
    }
    return font8x8_table[c - FONT8X8_FIRST];
}
