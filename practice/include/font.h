#ifndef PRACTICE_FONT_H
#define PRACTICE_FONT_H

#include "types.h"

/* A 5 by 7 pixel font for the characters 0x20 to 0x7F, with one more row for the descenders of g, j, p, q and
 * y. Each glyph has 8 rows; bit 4 of a row is its left pixel. 0x7F is a filled block. */
#define FONT_FIRST 0x20
#define FONT_COUNT 96
#define FONT_WIDTH 5
#define FONT_HEIGHT 8

#ifdef PRACTICE_LOADER
#define fontGlyphs loaderFontGlyphs
#endif

extern const u8 fontGlyphs[FONT_COUNT][FONT_HEIGHT];

/* The glyph index for a character, or 0 (space) for a character the font does not have. */
static inline int FontGlyph(char ch) {
    u8 c = ch;

    if (c < FONT_FIRST || c >= FONT_FIRST + FONT_COUNT) {
        return 0;
    }
    return c - FONT_FIRST;
}

#endif
