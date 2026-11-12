#ifndef PRACTICE_RDP_H
#define PRACTICE_RDP_H

/* Display list commands for the game's microcode, F3DEX 1.21. Each function returns one 64-bit command. The
 * layouts follow the public RSP and RDP command documentation (n64brew wiki). */

#include "types.h"

typedef struct Gfx {
    u32 w0;
    u32 w1;
} Gfx;

/* Texture formats and texel sizes. */
#define TX_FMT_I 4
#define TX_SIZ_4B 0
#define TX_SIZ_16B 2

#define TX_LOADTILE 7
#define TX_RENDERTILE 0
#define TX_CLAMP 2

/* F3DEX command numbers. */
#define CMD_MTX 0x01
#define CMD_MOVEMEM 0x03
#define CMD_VTX 0x04
#define CMD_DL 0x06
#define CMD_TRI2 0xB1
#define CMD_CLEARGEOMETRYMODE 0xB6
#define CMD_SETGEOMETRYMODE 0xB7
#define CMD_TEXTURE 0xBB
#define CMD_MOVEWORD 0xBC
#define CMD_TRI1 0xBF
#define CMD_SETSCISSOR 0xED
#define CMD_SETZIMG 0xFE
#define CMD_ENDDL 0xB8
#define CMD_SETOTHERMODE_L 0xB9
#define CMD_SETOTHERMODE_H 0xBA
#define CMD_RDPHALF_2 0xB3
#define CMD_RDPHALF_1 0xB4
#define CMD_TEXRECT 0xE4
#define CMD_LOADSYNC 0xE6
#define CMD_PIPESYNC 0xE7
#define CMD_TILESYNC 0xE8
#define CMD_FULLSYNC 0xE9
#define CMD_LOADBLOCK 0xF3
#define CMD_FILLRECT 0xF6
#define CMD_SETTILESIZE 0xF2
#define CMD_SETTILE 0xF5
#define CMD_SETPRIMCOLOR 0xFA
#define CMD_SETENVCOLOR 0xFB
#define CMD_SETCOMBINE 0xFC
#define CMD_SETTIMG 0xFD

/* G_MTX parameters (F3DEX 1.x): the projection stack, load instead of multiply. */
#define MTX_PROJECTION 0x01
#define MTX_LOAD 0x02
/* G_MOVEMEM index of the viewport, G_MOVEWORD index of the perspective normal. */
#define MOVEMEM_VIEWPORT 0x80
#define MOVEWORD_PERSPNORM 0x0E
/* Geometry mode bits. */
#define GEOM_ZBUFFER 0x1
#define GEOM_SHADE 0x4
#define GEOM_SHADING_SMOOTH 0x200
#define GEOM_CULL_FRONT 0x1000
#define GEOM_CULL_BACK 0x2000
/* Every geometry mode bit that the game uses: Z, shade, smooth shading, culling, fog, lighting, texture
 * coordinate generation and LOD. */
#define GEOM_ALL 0x1F3205

/* A vertex: position, texture coordinates, colour and alpha (lighting off). */
typedef struct Vtx {
    s16 x, y, z;
    u16 flag;
    s16 s, t;
    u8 r, g, b, a;
} Vtx;

/* A viewport: half the screen in quarter pixels, and the Z range. */
typedef struct Vp {
    s16 scale[4];
    s16 trans[4];
} Vp;

static inline Gfx GfxCmd(u32 w0, u32 w1) {
    Gfx g = { w0, w1 };
    return g;
}

static inline u32 Bits(u32 value, int shift, int width) {
    return (value & ((1u << width) - 1)) << shift;
}

/* Converts a cached (KSEG0) address to the physical address that the RSP and RDP read. */
static inline u32 PhysAddr(const void *p) {
    return (u32)p & 0x1FFFFFFF;
}

static inline Gfx GfxMatrix(const void *mtx, u32 params) {
    return GfxCmd(Bits(CMD_MTX, 24, 8) | Bits(params, 16, 8) | 64, PhysAddr(mtx));
}

static inline Gfx GfxViewport(const Vp *vp) {
    return GfxCmd(Bits(CMD_MOVEMEM, 24, 8) | Bits(MOVEMEM_VIEWPORT, 16, 8) | sizeof(Vp), PhysAddr(vp));
}

static inline Gfx GfxPerspNormalize(u16 norm) {
    return GfxCmd(Bits(CMD_MOVEWORD, 24, 8) | MOVEWORD_PERSPNORM, norm);
}

/* Loads N vertices (at most 32) into the vertex cache from index 0. */
static inline Gfx GfxVertices(const Vtx *v, int n) {
    return GfxCmd(Bits(CMD_VTX, 24, 8) | Bits(n, 10, 6) | Bits(sizeof(Vtx) * n - 1, 0, 10), PhysAddr(v));
}

/* F3DEX 1.x takes vertex indices times 2. */
static inline Gfx GfxTriangle(int a, int b, int c) {
    return GfxCmd(CMD_TRI1 << 24, Bits(a * 2, 16, 8) | Bits(b * 2, 8, 8) | Bits(c * 2, 0, 8));
}

static inline Gfx GfxTriangles(int a, int b, int c, int d, int e, int f) {
    return GfxCmd(Bits(CMD_TRI2, 24, 8) | Bits(a * 2, 16, 8) | Bits(b * 2, 8, 8) | Bits(c * 2, 0, 8),
                  Bits(d * 2, 16, 8) | Bits(e * 2, 8, 8) | Bits(f * 2, 0, 8));
}

static inline Gfx GfxDisplayList(const Gfx *dl) {
    return GfxCmd(CMD_DL << 24, PhysAddr(dl));
}

static inline Gfx GfxEndDisplayList(void) {
    return GfxCmd(CMD_ENDDL << 24, 0);
}

static inline Gfx GfxSync(u32 cmd) {
    return GfxCmd(cmd << 24, 0);
}

/* Sets LEN bits of the other mode word, starting at bit SHIFT. */
static inline Gfx GfxSetOtherMode(u32 cmd, int shift, int len, u32 data) {
    return GfxCmd(Bits(cmd, 24, 8) | Bits(shift, 8, 8) | Bits(len, 0, 8), data);
}

static inline Gfx GfxSetPrimColor(u32 rgba) {
    return GfxCmd(CMD_SETPRIMCOLOR << 24, rgba);
}

static inline Gfx GfxSetEnvColor(u32 rgba) {
    return GfxCmd(CMD_SETENVCOLOR << 24, rgba);
}

static inline Gfx GfxSetTextureImage(int fmt, int siz, int width, const void *addr) {
    return GfxCmd(Bits(CMD_SETTIMG, 24, 8) | Bits(fmt, 21, 3) | Bits(siz, 19, 2) | Bits(width - 1, 0, 12),
                  PhysAddr(addr));
}

static inline Gfx GfxSetTile(int fmt, int siz, int line, int tmem, int tile, int cmt, int cms) {
    return GfxCmd(Bits(CMD_SETTILE, 24, 8) | Bits(fmt, 21, 3) | Bits(siz, 19, 2) | Bits(line, 9, 9) | Bits(tmem, 0, 9),
                  Bits(tile, 24, 3) | Bits(cmt, 18, 2) | Bits(cms, 8, 2));
}

static inline Gfx GfxLoadBlock(int tile, int texels, int dxt) {
    return GfxCmd(CMD_LOADBLOCK << 24, Bits(tile, 24, 3) | Bits(texels - 1, 12, 12) | Bits(dxt, 0, 12));
}

/* Sets a tile's size in texels. */
static inline Gfx GfxSetTileSize(int tile, int width, int height) {
    return GfxCmd(CMD_SETTILESIZE << 24, Bits(tile, 24, 3) | Bits((width - 1) << 2, 12, 12) | Bits((height - 1) << 2, 0, 12));
}

#endif
