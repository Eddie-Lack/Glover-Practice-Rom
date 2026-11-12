#ifndef PRACTICE_COLLVIEW_H
#define PRACTICE_COLLVIEW_H

#include "rdp.h"
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

/* Run.collWire: the outline of every face. */
enum CollWire {
    COLL_WIRE_OFF,
    COLL_WIRE_OVER, /* over the faces */
    COLL_WIRE_ONLY, /* in place of the faces */
    COLL_WIRES
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
    /* 0x11 */ u8 full;     /* bit 0: an actor did not fit in the cache; bit 1: open edges did not; bit 2: an actor did
                             * not fit in the frame's list */
    /* 0x12 */ u16 shapes;  /* meshes in the cache */
    /* 0x14 */ u16 records; /* actors in the cache */
    /* 0x16 */ u16 vertices; /* vertices in the cache */
    /* 0x18 */ u16 commands; /* display list commands in the cache */
    /* 0x1A */ u16 builds;  /* cache builds since boot */
    /* 0x1C */ u32 counts;  /* CPU counts (46.875 MHz) of the view's work on the draw thread this frame */
    /* 0x20 */ u32 buildCounts; /* CPU counts of the last cache build, over all its frames */
    /* 0x24 */ u32 infoCounts;  /* CPU counts of the face info this frame */
} CollStats;

extern CollStats collStats;

/* The kinds of faces, by the normal's y (collview.c): flat floor, steep floor, wall, ceiling. */
enum CollKind { COLL_KIND_FLAT, COLL_KIND_STEEP, COLL_KIND_WALL, COLL_KIND_CEILING, COLL_KINDS };

/* Run.collInfo: one face, as the panel shows it. */
typedef struct CollFaceInfo {
    /* 0x00 */ const struct CollFace *face; /* the game's face */
    /* 0x04 */ const struct Actor *actor;   /* the actor it belongs to, or NULL if none was found */
    /* 0x08 */ f32 normal[3];
    /* 0x14 */ f32 distance; /* from the eye (the centre's face) */
    /* 0x18 */ u16 number;   /* its number in the actor's faces */
    /* 0x1A */ u8 state;     /* 0: no face; 1: this frame's; 2: an earlier frame's (the glove's last face) */
    /* 0x1B */ u8 terrain;   /* its row of terrain[] */
    /* 0x1C */ u8 kind;      /* CollKind */
    /* 0x1D */ u8 platform;  /* 1: the actor is a platform */
    /* 0x1E */ u8 back;      /* 1: the ray met the face from behind */
} CollFaceInfo;

/* 0: the face under the glove: of the faces that it touched on the last game frame, the one whose normal points
 * most upwards; the last such face while it touches none. 1: the face at the screen's centre: the nearest that a
 * ray from the eye along the view meets, of the faces that the view's filter shows. */
extern CollFaceInfo collInfo[2];

/* The draw thread, before CollViewDraw, while face info is on: finds the two faces. */
void CollInfoUpdate(void);
/* Draws the face info panel and a cross at the screen's centre (in the text pass). */
void CollInfoDraw(Gfx **dl);

/* The draw thread, at the end of the frame's display list (PracticePrintTimers), while the view is on. Appends
 * one command to the game's list. */
void CollViewDraw(void);

/* Builds the cache again: a savestate load, a reset or a warp changes the level's memory. Safe from any thread. */
void CollViewReset(void);

#endif
