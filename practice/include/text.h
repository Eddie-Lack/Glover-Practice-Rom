#ifndef PRACTICE_TEXT_H
#define PRACTICE_TEXT_H

#include "rdp.h"

#ifdef PRACTICE_LOADER
/* The loader has its own copy of the text code (see loader.c). */
#define TextInit LoaderTextInit
#define TextBegin LoaderTextBegin
#define TextPrint LoaderTextPrint
#define TextWidth LoaderTextWidth
#define TextRect LoaderTextRect
#define textShadow loaderTextShadow
#endif

#define TEXT_ADVANCE 6
#define TEXT_LINE 9

/* The font texture's size in bytes. Its buffer must be 8-byte aligned. */
#define TEXT_ATLAS_SIZE 3072

/* 0: TextPrint draws no shadow. */
extern u8 textShadow;

/* Makes the font texture in ATLAS, which must be zero. */
void TextInit(u8 *atlas);
/* Adds the commands that set up text drawing to the list at *dl. TextPrint and TextRect add nothing beyond END:
 * text that does not fit is left out. NULL: no end. */
void TextBegin(Gfx **dl, const Gfx *end);
/* Draws STR with its top left corner at X, Y, each font pixel SCALE pixels wide and high, with a shadow. RGBA is
 * the colour. */
void TextPrint(Gfx **dl, int x, int y, int scale, u32 rgba, const char *str);
/* Fills the rectangle from X0, Y0 to X1, Y1 (not included) with RGBA, blended by its alpha. Call after
 * TextBegin. */
void TextRect(Gfx **dl, int x0, int y0, int x1, int y1, u32 rgba);
/* The width of STR in pixels. */
int TextWidth(const char *str, int scale);

#endif
