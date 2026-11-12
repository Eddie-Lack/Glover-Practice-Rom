/* Text drawn by the RDP from a texture made of the 5 by 7 font. */
#include "game.h"
#include "font.h"
#include "text.h"

/* The font texture: 16 by 4 cells of 8 by 8 texels, 4-bit intensity. */
#define CELL 8
#define ATLAS_COLUMNS 16
#define ATLAS_WIDTH (ATLAS_COLUMNS * CELL)
#define ATLAS_HEIGHT (FONT_COUNT / ATLAS_COLUMNS * CELL)

_Static_assert(TEXT_ATLAS_SIZE == ATLAS_WIDTH * ATLAS_HEIGHT / 2, "TEXT_ATLAS_SIZE");

static u8 *fontAtlas;
u8 textShadow = 1;
/* The end of the list being written: nothing goes at or beyond it. */
static const Gfx *textEnd;

/* 1 if N more commands fit before textEnd. */
static int Room(const Gfx *g, int n) {
    return textEnd == NULL || g + n <= textEnd;
}

void TextInit(u8 *atlas) {
    fontAtlas = atlas;
    for (int g = 0; g < FONT_COUNT; g++) {
        int x0 = g % ATLAS_COLUMNS * CELL;
        int y0 = g / ATLAS_COLUMNS * CELL;
        for (int row = 0; row < FONT_HEIGHT; row++) {
            for (int col = 0; col < FONT_WIDTH; col++) {
                if (fontGlyphs[g][row] & (0x10 >> col)) {
                    int i = (y0 + row) * ATLAS_WIDTH + x0 + col;
                    /* Two texels a byte; the even texel is the high nibble. */
                    fontAtlas[i / 2] |= (i & 1) ? 0x0F : 0xF0;
                }
            }
        }
    }
    osWritebackDCache(fontAtlas, TEXT_ATLAS_SIZE);
}

/* The colour combiner: colour = primitive colour, alpha = texel alpha times primitive alpha (in both cycles).
 * Mux codes: 15 and 31 give 0 for colour inputs A, B and C; 7 gives 0 for alpha inputs; 3 is the primitive
 * colour; 1 is texel 0's alpha. */
#define COMBINE_W0 (CMD_SETCOMBINE << 24 | 15 << 20 | 31 << 15 | 1 << 12 | 3 << 9 | 15 << 5 | 31)
#define COMBINE_W1 (15u << 28 | 15 << 24 | 1 << 21 | 3 << 18 | 3 << 15 | 7 << 12 | 7 << 9 | 3 << 6 | 7 << 3 | 7)

/* Other mode, high word, bits 4 to 23: no alpha or colour dither, texture filter conversion, point sampling, no
 * palette, one tile, no perspective correction, one-cycle mode. */
#define OTHERMODE_H (3 << 4 | 3 << 6 | 6 << 9)
/* Other mode, low word: a translucent surface without Z. The blender mixes the incoming colour with memory by
 * the incoming alpha in both cycles; FORCE_BL, ZMODE_XLU, CVG_DST_FULL and IM_RD are set. */
#define OTHERMODE_L (1 << 22 | 1 << 20 | 1 << 14 | 2 << 10 | 2 << 8 | 1 << 6)

void TextBegin(Gfx **dl, const Gfx *end) {
    Gfx *g = *dl;

    textEnd = end;
    /* A 4-bit texture loads as 16-bit texels: four texels each. DXT is 2048 divided by the 64-bit words in a
     * row, rounded up. */
    int words = ATLAS_WIDTH * 4 / 64;

    *g++ = GfxSync(CMD_PIPESYNC);
    *g++ = GfxSetOtherMode(CMD_SETOTHERMODE_H, 4, 20, OTHERMODE_H);
    *g++ = GfxSetOtherMode(CMD_SETOTHERMODE_L, 3, 29, OTHERMODE_L);
    *g++ = GfxCmd(COMBINE_W0, COMBINE_W1);
    *g++ = GfxSetTextureImage(TX_FMT_I, TX_SIZ_16B, 1, fontAtlas);
    *g++ = GfxSetTile(TX_FMT_I, TX_SIZ_16B, 0, 0, TX_LOADTILE, 0, 0);
    *g++ = GfxSync(CMD_LOADSYNC);
    *g++ = GfxLoadBlock(TX_LOADTILE, ATLAS_WIDTH * ATLAS_HEIGHT / 4, (2048 + words - 1) / words);
    *g++ = GfxSync(CMD_PIPESYNC);
    *g++ = GfxSetTile(TX_FMT_I, TX_SIZ_4B, words, 0, TX_RENDERTILE, TX_CLAMP, TX_CLAMP);
    *g++ = GfxSetTileSize(TX_RENDERTILE, ATLAS_WIDTH, ATLAS_HEIGHT);
    *dl = g;
}

static Gfx *TextChars(Gfx *g, int x, int y, int scale, const char *str) {
    for (; *str != '\0'; str++, x += TEXT_ADVANCE * scale) {
        int glyph = FontGlyph(*str);
        if (glyph == 0) {
            continue;
        }
        if (!Room(g, 3)) {
            break;
        }
        /* Screen coordinates are 10.2 fixed point, texture coordinates 10.5, the steps 5.10. */
        *g++ = GfxCmd(CMD_TEXRECT << 24 | Bits((x + CELL * scale) << 2, 12, 12) | Bits((y + CELL * scale) << 2, 0, 12),
                      Bits(TX_RENDERTILE, 24, 3) | Bits(x << 2, 12, 12) | Bits(y << 2, 0, 12));
        *g++ = GfxCmd(CMD_RDPHALF_1 << 24, (glyph % ATLAS_COLUMNS * CELL) << 5 << 16 | (glyph / ATLAS_COLUMNS * CELL) << 5);
        *g++ = GfxCmd(CMD_RDPHALF_2 << 24, (1 << 10) / scale << 16 | (1 << 10) / scale);
    }
    return g;
}

void TextPrint(Gfx **dl, int x, int y, int scale, u32 rgba, const char *str) {
    Gfx *g = *dl;

    if (!Room(g, 4)) {
        return;
    }
    if (textShadow) {
        *g++ = GfxSync(CMD_PIPESYNC);
        *g++ = GfxSetPrimColor(rgba & 0xFF);
        g = TextChars(g, x + scale, y + scale, scale, str);
    }
    if (!Room(g, 2)) {
        *dl = g;
        return;
    }
    *g++ = GfxSync(CMD_PIPESYNC);
    *g++ = GfxSetPrimColor(rgba);
    g = TextChars(g, x, y, scale, str);
    *dl = g;
}

/* The combiner for plain rectangles: colour and alpha are the primitive colour's (0 for A, B and C again). */
#define RECT_COMBINE_W0 (CMD_SETCOMBINE << 24 | 15 << 20 | 31 << 15 | 7 << 12 | 7 << 9 | 15 << 5 | 31)
#define RECT_COMBINE_W1 (15u << 28 | 15 << 24 | 7 << 21 | 7 << 18 | 3 << 15 | 7 << 12 | 3 << 9 | 3 << 6 | 7 << 3 | 3)

void TextRect(Gfx **dl, int x0, int y0, int x1, int y1, u32 rgba) {
    Gfx *g = *dl;

    if (!Room(g, 6)) {
        return;
    }
    *g++ = GfxSync(CMD_PIPESYNC);
    *g++ = GfxCmd(RECT_COMBINE_W0, RECT_COMBINE_W1);
    *g++ = GfxSetPrimColor(rgba);
    /* In one-cycle mode the lower right edge is not drawn. */
    *g++ = GfxCmd(CMD_FILLRECT << 24 | Bits(x1 << 2, 12, 12) | Bits(y1 << 2, 0, 12), Bits(x0 << 2, 12, 12) | Bits(y0 << 2, 0, 12));
    *g++ = GfxSync(CMD_PIPESYNC);
    *g++ = GfxCmd(COMBINE_W0, COMBINE_W1);
    *dl = g;
}

int TextWidth(const char *str, int scale) {
    int n = 0;

    while (str[n] != '\0') {
        n++;
    }
    return (n * TEXT_ADVANCE - 1) * scale;
}
