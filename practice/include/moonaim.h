#ifndef PRACTICE_MOONAIM_H
#define PRACTICE_MOONAIM_H

#include "rdp.h"
#include "trainers.h"
#include "types.h"

/* The Moon Pound aim (trainers.h, MoonView): the part of the trainers that works on the collision faces. It reads the
 * game and writes nothing to it. */

/* What the game thread read at the hook (trainers.c fills it from the glove and the camera). */
typedef struct MoonIn {
    f32 pos[3], vel[3];  /* the glove's, at the start of the game frame (what the last frame left) */
    f32 radius;          /* the glove's first collision sphere */
    f32 gravity;
    f32 handFriction;    /* hand.phys->friction */
    f32 slamRadius;      /* the radius that the slam grows to: 16 * levelHandScale / 0.05 */
    f32 camYaw;
    s16 actionLock, actionTime;
    s16 mode;
    u8 waterState, onGround;
    u8 aHeld;
    u8 scaled;           /* Hercules or the last boss: the glove's radius does not follow the rule */
    u8 noExit;           /* MOWF_NO_EXIT applies */
} MoonIn;

/* Drops the work buffer and the candidates (a level start, a savestate load, turning the trainer on). */
void MoonAimReset(void);
/* One game frame. */
void MoonAimStep(const MoonIn *in, MoonView *out);
/* The draw thread. */
void MoonAimDraw(Gfx **dl, const MoonView *v);

/* The hole painter (the collision view's "Mark holes", Run.collHoles). The game thread finds the vertices with a dead
 * cone (the candidates of the aim) and publishes them; the collision view draws a post at each. */
#define HOLE_MAX 64
typedef struct HoleView {
    /* 0x00 */ u8 count;
    /* 0x01 */ u8 capped;   /* the list stopped at HOLE_MAX in the buffer */
    /* 0x02 */ u16 pad02;
    /* 0x04 */ f32 p[HOLE_MAX][3]; /* the hole's centre for a glove centre one unit above the vertex, at the vertex's height */
} HoleView;

extern HoleView holeViews[2];
extern volatile u32 holeSeq;   /* the view holeViews[holeSeq & 1] is complete */
extern volatile u32 holeBegin; /* the number of the publish that is being written (holeSeq + 1 while it is) */
extern u32 holeLast, holeWorst; /* CPU counts of the last painter step, and of the longest since the level began (the test reads them) */

/* The game thread, once a game frame (PracticeClearText), while the collision view and the painter are on. */
void HolesGameFrame(void);

/* The wall in front of a ball (the wall-clip indicator, trainers.c). */
typedef struct WallHit {
    /* 0x00 */ u8 found;
    /* 0x01 */ u8 pad01[3];
    /* 0x04 */ f32 dist;   /* from the ball's centre to the wall's plane */
    /* 0x08 */ f32 speedN; /* the ball's speed along the wall's normal (towards the wall is positive) */
    /* 0x0C */ f32 cosA;   /* 1: the ball's direction is the wall's normal; less: the wall stands at an angle */
    /* 0x10 */ f32 point[3]; /* where the ball's line meets the wall's plane */
    /* 0x1C */ u32 counts; /* CPU counts the scan took */
    /* 0x20 */ f32 normal[3]; /* the wall's normal, pointing at the ball */
} WallHit;

/* The nearest wall that the ball at POS (x, y, z) reaches along its horizontal velocity VEL (x, y, z). 1 if there is one. */
int MoonWallScan(const f32 *pos, const f32 *vel, WallHit *out);

#endif
