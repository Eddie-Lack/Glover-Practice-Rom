#ifndef PRACTICE_CORRUPT_H
#define PRACTICE_CORRUPT_H

#include "types.h"

/* Memory corruption tools, for finding crashes and arbitrary behaviour (docs/build.md, "Memory corruption").
 *
 * Pokes: the memory view's Z key marks the address and the word at the cursor as a poke, and its second page lists
 * them (POKE_COUNT of them). Each poke has a mode:
 *   once    written when "Apply once pokes" (Inspect menu, Memory corruption) or A on the page is used;
 *   freeze  the value is written at the start of every game frame;
 *   random  every PERIOD game frames, the poke's low BITS bits are set to random bits and the others keep the
 *           value's;
 *   flip    every PERIOD game frames, one random bit of the low BITS bits of the memory there is flipped.
 * "Flip one random bit" flips one bit in the chosen region: the glove's or the ball's actor, a random actor of the
 * level, or a range of memory from the memory view's address.
 *
 * Writes go only to RDRAM (0x80000000 to 0x807FFFFF), never to the practice segment and its data, and a word is
 * written at an address that is a multiple of 4 (a half word, 2). The tools use their own random number
 * generator and never touch the game's seed. The HUD shows "MODIFIED pokes" while a poke is running, and after a
 * once poke or a flip, until a state is loaded or "Clear all pokes" is used (or the console is reset). A write to a
 * global or to the save outlives a level, so a level change does not clear it. A write into code clears the
 * instruction cache for those bytes. The game thread does the work, once a game frame
 * (CorruptGameFrame, from ActionsGameFrame); the menu thread edits the list. */
#define POKE_COUNT 8

enum PokeMode { POKE_OFF, POKE_ONCE, POKE_FREEZE, POKE_RANDOM, POKE_FLIP, POKE_MODES };
enum CorruptRegion { REGION_GLOVE, REGION_BALL, REGION_ACTORS, REGION_RANGE, REGION_COUNT };

typedef struct Poke {
    /* 0x00 */ u32 address;  /* 0: an empty place */
    /* 0x04 */ u32 value;    /* the low SIZE bytes count */
    /* 0x08 */ u32 count;    /* writes made */
    /* 0x0C */ u8 mode;
    /* 0x0D */ u8 size;      /* 1, 2 or 4 bytes */
    /* 0x0E */ u8 bits;      /* random and flip: the low bits that change, 1 to 8 * size */
    /* 0x0F */ u8 period;    /* an index in pokePeriods */
} Poke;

typedef struct Corrupt {
    /* 0x00 */ Poke pokes[POKE_COUNT];
    /* 0x80 */ u32 rng;
    /* 0x84 */ s32 lastFrame;  /* frameCount2 at the last round of pokes */
    /* 0x88 */ u32 flips;      /* bits flipped by "Flip one random bit" */
    /* 0x8C */ u32 lastAddress; /* the byte of the last flip */
    /* 0x90 */ u8 lastBit;
    /* 0x91 */ u8 region;      /* a CorruptRegion */
    /* 0x92 */ u8 rangeSize;   /* an index in corruptRangeSizes */
    /* 0x93 */ u8 requests;    /* CORRUPT_APPLY, CORRUPT_FLIP: for the next game frame */
    /* 0x94 */ u8 active;      /* pokes that run every game frame (freeze, random, flip) */
    /* 0x95 */ u8 dirty;       /* something was written since the last state load or "Clear all pokes" */
    /* 0x96 */ u8 seeded;
    /* 0x98 */ u32 frames;     /* game frames with a round of pokes */
} Corrupt;

extern Corrupt corrupt;
extern const u8 pokePeriods[6];
extern const u32 corruptRangeSizes[4];

#define CORRUPT_APPLY 1
#define CORRUPT_FLIP 2

/* Whether the HUD shows the marker. */
static inline int CorruptMarker(void) {
    return corrupt.active | corrupt.dirty;
}

/* The menu: asks for the next game frame to apply the once pokes, or to flip a bit. */
void CorruptRequest(int what);
/* The game thread, at the start of each game frame, when anything is on. */
void CorruptGameFrame(void);

/* Whether a write of SIZE bytes at ADDRESS is allowed. */
int PokeAllowed(u32 address, u32 size);
/* The memory view's Z: marks the word at ADDRESS with VALUE (a poke in the mode "once"). */
void PokeMark(u32 address, u32 value);
/* Writes (or, for random and flip, takes one step of) one poke now. */
void PokeApply(Poke *p);
/* Counts the running pokes again after the list changed. */
void PokeRecount(void);
void PokeClear(Poke *p);
void PokesClearAll(void);
/* Keeps the poke's size, alignment and bits valid. */
void PokeFix(Poke *p);

#endif
