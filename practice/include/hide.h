#ifndef PRACTICE_HIDE_H
#define PRACTICE_HIDE_H

#include "types.h"

/* The kinds of objects that the Hide objects menu can leave out of the picture: bits of Run.hide. The game's
 * drawing code still runs for a hidden kind, and the practice code then removes what it drew from the frame's
 * display list (hide.c). */
enum HideKind {
    HIDE_LANDSCAPE, /* landscape actors that collide (actor flags 0x10000 without 4) */
    HIDE_SCENERY,   /* landscape that does not collide (flag 4), background and cameo pieces */
    HIDE_WATER,     /* water surfaces */
    HIDE_PLATFORMS,
    HIDE_GLOVE,
    HIDE_BALL,      /* the ball, its three copies (ball.c, extraBall) and its plasters */
    HIDE_ENEMIES,   /* enemies and the shots of the bullet list */
    HIDE_COLLECT,   /* pick-ups, the power-up that orbits the glove or ball, garibs */
    HIDE_DEBRIS,    /* debris and particles */
    HIDE_SKY,       /* the backdrops */
    HIDE_HUD,       /* the game's counters (PrintStats) */
    HIDE_SHADOWS,
    HIDE_FOOTPRINTS, /* footprints and blast rings */
    HIDE_TRAILS,     /* blurs */
    HIDE_LIGHTNING,
    HIDE_KINDS
};

#define HIDE_BIT(kind) (1 << (kind))

/* What the last frame hid, for the tests. Counts are of display list commands turned into no-ops. */
typedef struct HideStats {
    /* 0x00 */ u32 frame;   /* dispFrameCount of the frame */
    /* 0x04 */ u16 actors;  /* actors hidden */
    /* 0x06 */ u16 sprites; /* sprites hidden */
    /* 0x08 */ u16 commands; /* commands turned into no-ops */
    /* 0x0A */ u16 objectSprites; /* sprites of hidden actors' models noted, since the boot */
    /* 0x0C */ u16 last[3];  /* the frame before: actors, sprites, commands */
    /* 0x12 */ u16 pad;
    /* 0x14 */ u32 counts;     /* CPU counts (46.875 MHz) of the practice code's work for the 3D objects and sprites */
    /* 0x18 */ u32 lastCounts; /* the frame before */
} HideStats;

extern HideStats hideStats;

/* PracticePrintStats calls this in place of PrintStats (the HUD). */
void HidePrintStats(void);
/* The menu's item: toggles kind ARG. */
void HideToggle(int arg);
/* Its value text. */
void HideText(char *buf, int size, int arg);

#endif
