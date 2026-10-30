#ifndef PRACTICE_COLLVIEW_H
#define PRACTICE_COLLVIEW_H

#include "types.h"

/* The collision view's modes (Run.collView). */
enum CollViewMode {
    COLLVIEW_OFF,
    COLLVIEW_SEE_THROUGH, /* every face near Glover, drawn over everything */
    COLLVIEW_SURFACES,    /* only where nothing nearer hides it (the Z buffer) */
    COLLVIEW_MODES
};

/* What the last frame drew, for the menu and the tests. Written once, at the end of each frame's scan. */
typedef struct CollStats {
    u16 faces;
    u16 spheres;
    u16 range;  /* faces too far from Glover for the vertex format */
    u16 bad;    /* actors with data that could not be read */
    u8 slow;    /* 1: the scan stopped at its time limit */
    u8 full;    /* 1: more faces were in reach than the buffer holds */
    s16 radius; /* the radius used: the menu's, or less while the faces fill the buffer */
    u16 enemies;    /* collision spheres of enemies drawn (also counted in spheres) */
    u16 fresh;      /* platforms whose faces were worked out again from their mesh, position and rotation */
    u16 edges;      /* open edges drawn */
    u8 edgesFull;   /* 1: more open edges were in reach than the buffer holds */
    u8 edgesSkipped; /* meshes left out of the open edges: too many faces, or unreadable */
    u32 counts;     /* CPU counts (46.875 MHz) from the start of the scan to the end of the last frame's list */
    u16 holes;      /* posts drawn for the marked holes (moonaim.h) */
    u16 pad;
} CollStats;

extern CollStats collStats;

/* The draw thread, at the end of the frame's display list (PracticePrintTimers), while the view is on. Appends
 * one command to the game's list. */
void CollViewDraw(void);

/* Drops the kept open edges: a savestate load or a warp changes the level's memory. */
void CollViewEdgesReset(void);

#endif
