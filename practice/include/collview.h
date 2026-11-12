#ifndef PRACTICE_COLLVIEW_H
#define PRACTICE_COLLVIEW_H

#include "types.h"

/* The collision view's modes (Run.collView). */
enum CollViewMode {
    COLLVIEW_OFF,
    COLLVIEW_SEE_THROUGH, /* every face, drawn over everything */
    COLLVIEW_SURFACES,    /* only where nothing nearer hides it (the Z buffer) */
    COLLVIEW_MODES
};

/* Run.collRadius: the faces of the actors whose bounding sphere comes this near Glover. */
enum CollRadius {
    COLL_RADIUS_LEVEL, /* the whole level */
    COLL_RADIUS_1000,
    COLL_RADIUS_500,
    COLL_RADIUS_250,
    COLL_RADIUSES
};

/* Run.collFaces: the kinds of faces left out, one bit each. A face is a floor when its normal's y is above 0.3
 * (the game lets such a face carry Glover, collision.c), a ceiling below -0.3, and a wall in between. The terrain
 * bits use the actor's terrain row: water is row 12, slippery has slip above 1, low gravity has a gravity scale
 * below 1. */
enum CollFaceBit {
    COLL_FLOORS = 1 << 0,
    COLL_WALLS = 1 << 1,
    COLL_CEILINGS = 1 << 2,
    COLL_WATER = 1 << 3,
    COLL_SLIPPERY = 1 << 4,
    COLL_LOW_GRAVITY = 1 << 5,
    COLL_LANDSCAPE = 1 << 6,
    COLL_PLATFORMS = 1 << 7,
};

/* Run.collColour. */
enum CollColour {
    COLL_COLOUR_TERRAIN, /* special terrain first, then the slope */
    COLL_COLOUR_KIND,    /* floor, wall, ceiling */
    COLL_COLOUR_OBJECT,  /* one colour for each actor */
    COLL_COLOURS
};

/* Run.collBack: the faces seen from behind (from the side that is not solid). */
enum CollBack {
    COLL_BACK_FAINT,
    COLL_BACK_HIDE,
    COLL_BACK_SAME,
    COLL_BACKS
};

/* The state of the cache of the level's collision meshes (CollStats.state). */
enum CollCacheState {
    COLL_CACHE_WAIT,  /* waiting for the frames that still draw the old cache */
    COLL_CACHE_BUILD, /* being built: nothing of it is drawn */
    COLL_CACHE_READY,
};

/* What the last frame drew, for the menu and the tests. Written once, at the end of each frame's view. */
typedef struct CollStats {
    /* 0x00 */ u16 faces;   /* faces drawn */
    /* 0x02 */ u16 actors;  /* actors drawn */
    /* 0x04 */ u16 spheres; /* collision spheres drawn: the glove's, the ball's and the enemies' */
    /* 0x06 */ u16 enemies; /* of them, the enemies' */
    /* 0x08 */ u16 edges;   /* open edges drawn */
    /* 0x0A */ u16 holes;   /* posts drawn for the marked holes (moonaim.h) */
    /* 0x0C */ u16 contacts; /* faces that the glove or the ball touched, drawn white */
    /* 0x0E */ u16 bad;     /* actors with data that could not be read, or out of range */
    /* 0x10 */ u8 state;    /* CollCacheState */
    /* 0x11 */ u8 full;     /* bit 0: an actor did not fit in the cache or the frame's list; bit 1: open edges did not */
    /* 0x12 */ u16 shapes;  /* meshes in the cache */
    /* 0x14 */ u16 records; /* actors in the cache */
    /* 0x16 */ u16 vertices; /* vertices in the cache */
    /* 0x18 */ u16 commands; /* display list commands in the cache */
    /* 0x1A */ u16 builds;  /* cache builds since boot */
    /* 0x1C */ u32 counts;  /* CPU counts (46.875 MHz) of the view's work on the draw thread this frame */
    /* 0x20 */ u32 buildCounts; /* CPU counts of the last cache build, over all its frames */
} CollStats;

extern CollStats collStats;

/* The draw thread, at the end of the frame's display list (PracticePrintTimers), while the view is on. Appends
 * one command to the game's list. */
void CollViewDraw(void);

/* Builds the cache again: a savestate load, a reset or a warp changes the level's memory. Safe from any thread. */
void CollViewReset(void);

#endif
