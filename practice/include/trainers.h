#ifndef PRACTICE_TRAINERS_H
#define PRACTICE_TRAINERS_H

#include "bosswin.h"
#include "rdp.h"
#include "types.h"

/* Trick trainers (Game menu, "Trainers"; Run.trainers, off at start, not saved). Each reads the game and draws
 * on the HUD. None writes to the game, calls Random or changes input. docs/plan.md ("Trick trainers") describes
 * the five; all five are built.
 *
 * Run.trainers holds one bit for each trainer. With the byte at 0 the game thread does one test a call.
 *
 * Timing. TrainersGameFrame runs at the start of every message that RunGame handles, before the game frame's
 * logic. The glove, the ball and the camera hold the state that the last game frame left. The controller thread has
 * not always read the pad when the hook runs (docs/testing.md, "Trainers"). The trainer logic runs once a game
 * frame, in TrainersProcessInput (called by PracticeProcessInput), with the state of the hook and the pad of
 * ProcessInput. "Frame n" below counts the logic runs from the start of an attempt.
 *
 * The game thread builds a TrainerView in the buffer that the next publish uses, and raises trainerViewSeq. Before it
 * writes it sets trainerViewBegin to the number of the publish. The draw thread copies the buffer that
 * trainerViewSeq names, and keeps the copy only if trainerViewBegin is at most one ahead of that number (a second
 * publish would have written the buffer that it copied). It tries four times and draws nothing if it fails. */

#define TRAINER_LMASH 0x01 /* L mash timing */
#define TRAINER_LINE 0x02  /* line keeper */
#define TRAINER_MBZL 0x04  /* MBZ-L timing */
#define TRAINER_MOON 0x08  /* Moon Pound aim */
#define TRAINER_BOSS 0x10  /* boss windows (bosswin.c) */
/* Bit 0x20 is not used. It was a removed trainer, and an older Controller Pak note can have it set. */
#define TRAINER_CLIP 0x40  /* the wall-clip indicator for MAZ-R (trainers.c) */
#define TRAINER_ALL 0x5F
#define TRAINER_TRICKS 0x4F /* the ones that need the glove under the player's control (Quiet) */

/* L mash (trainers.c): one line for the last press, the last 24 game frames, and the speed. */
enum LMashKind { LM_NONE, LM_STAND, LM_HOLD };
enum LMashVerdict { LMV_NONE, LMV_ON_TIME, LMV_LATE, LMV_SLOW, LMV_REST, LMV_EARLY };
#define LM_STRIP 24
/* Warnings, one bit each. */
#define LMW_WEAK 0x01
#define LMW_CAMERA 0x02
#define LMW_RELEASED 0x04
#define LMW_AIR 0x08
#define LMW_OTHER_BALL 0x10
#define LMW_NORMAL 0x20

typedef struct LMashView {
    /* 0x00 */ u8 kind;       /* LMashKind of the last press */
    /* 0x01 */ u8 verdict;    /* LMashVerdict */
    /* 0x02 */ u8 gap;        /* game frames since the press before it */
    /* 0x03 */ u8 over;       /* game frames beyond the best gap, for LATE and SLOW */
    /* 0x04 */ u8 warn;       /* LMW_ bits */
    /* 0x05 */ u8 cells;      /* valid cells of strip */
    /* 0x06 */ u8 mode;       /* the glove's mode in the newest cell: HAND_JOINED, HAND_BALLWALK or 0 */
    /* 0x07 */ u8 pad07;
    /* 0x08 */ u8 strip[LM_STRIP]; /* oldest first: 1 holding (J), 2 stood on (B), 0 other; bit 0x80 an L press */
    /* 0x20 */ f32 move;      /* the ball's move in the last game frame */
    /* 0x24 */ f32 peak;      /* the largest move since the attempt began */
    /* 0x28 */ f32 avg20;     /* the mean of the last 20 moves */
    /* 0x2C */ f32 target;    /* the steady move of a perfect mash with this pad (0: not known) */
} LMashView;

/* MBZ-L (trainers.c): one attempt. B or A pressed on frame 0; the aim or windup is expected on frame 6 (B) or
 * 4 (A), the cancel on the same frame, the L on the next. */
enum MbzKind { MBZ_NONE, MBZ_B, MBZ_A };
enum MbzVerdict {
    MBV_LIVE,        /* the attempt goes on */
    MBV_PERFECT,
    MBV_L_EARLY_LOST,    /* L before the cancel frame, ignored (n frames early) */
    MBV_L_EARLY_STOOD,   /* L from JOINED before the aim: Glover stood on the ball and the aim never began */
    MBV_L_LATE,          /* n frames late, loss in speed */
    MBV_NO_L,            /* no L in the window */
    MBV_Z_LATE,          /* the cancel came n frames late */
    MBV_Z_FRESH,         /* Z pressed on frame n dropped the ball */
    MBV_NO_Z,            /* the aim stays */
    MBV_Z_HELD,          /* Z still down after the L: the glove dismounts */
    MBV_RELEASED,        /* the button was let go in the aim */
};
#define MBZ_KEEP 4
#define MBZ_TIMELINE 12

typedef struct MbzAttempt {
    /* 0x00 */ u8 kind;      /* MbzKind; 0: no attempt in this slot */
    /* 0x01 */ u8 verdict;   /* MbzVerdict */
    /* 0x02 */ u8 zKind;     /* 0 Z not down yet, 1 armed (down before frame 0), 2 fresh press */
    /* 0x03 */ s8 n;         /* the verdict's number of frames */
    /* 0x04 */ s8 zFrame;    /* the frame of the fresh Z press, or -1 */
    /* 0x05 */ s8 aimFrame;  /* the frame on which the aim or windup showed, or -1 */
    /* 0x06 */ s8 cancelFrame; /* the frame on which Z was down in the aim, or -1 */
    /* 0x07 */ s8 lFrame;    /* the frame of the L that counts (the first from the L target on), or -1 */
    /* 0x08 */ s8 lTarget;   /* the best frame for the L: the cancel frame plus 1; -1 before the cancel */
    /* 0x09 */ u8 speedFlags; /* bit 0: speed known, bit 1: speed3 known, bit 2: speed6 known */
    /* 0x0A */ u8 startAir;  /* the ball was in the air at the aim */
    /* 0x0B */ u8 done;      /* the verdict is final */
    /* 0x0C */ u16 bMask;    /* bit n: the start button was down on frame n (0 to 9) */
    /* 0x0E */ u16 zMask;
    /* 0x10 */ u16 lMask;    /* bit n: L was pressed on frame n */
    /* 0x12 */ u16 pad12;
    /* 0x14 */ f32 v0;       /* the ball's speed at the aim: what the cancel gives back */
    /* 0x18 */ f32 speed;    /* the ball's speed after the L frame */
    /* 0x1C */ f32 speed3;   /* 3 frames later */
    /* 0x20 */ f32 speed6;   /* 6 frames later */
    /* 0x24 */ f32 best;     /* the speed of a perfect L */
    /* 0x28 */ u32 doneFrame; /* frameCount2 when the verdict was made */
} MbzAttempt;


/* Line keeper (trainers.c): when to tap C-left or C-right while walking on a diagonal D-pad, to hold a straight line.
 * E is the heading error in radians (the camera's yaw against the yaw at the latch, plus the turn that the camera's
 * speed still adds). */
enum LineAdvice { LK_NONE, LK_RIGHT, LK_LEFT };
/* Why the trainer is silent (LineView.why). */
enum LineWhy { LKW_OK, LKW_CAMERA, LKW_SLOW, LKW_NO_STICK, LKW_LATCHING, LKW_JUMPED };
#define LK_STRIP 40

typedef struct LineView {
    /* 0x00 */ u8 live;       /* 1: the reference is latched and E is valid */
    /* 0x01 */ u8 advice;     /* LineAdvice */
    /* 0x02 */ u8 why;        /* LineWhy */
    /* 0x03 */ u8 cells;      /* valid cells of strip */
    /* 0x04 */ u8 share;      /* frames with C down among the last 20 */
    /* 0x05 */ u8 expect;     /* the expected share, in percent */
    /* 0x06 */ u8 walking;    /* the mover is the glove (it walks), not the ball */
    /* 0x07 */ u8 pad07;
    /* 0x08 */ u8 strip[LK_STRIP]; /* oldest first: 0 none, 1 C-left, 2 C-right */
    /* 0x30 */ f32 err;       /* E */
    /* 0x34 */ f32 rate;      /* Edot: the drift of E in a frame, taps taken out */
    /* 0x38 */ f32 lead;      /* E + 2 Edot: the value that the advice tests */
    /* 0x3C */ f32 tap;       /* what one frame of C turns the view: 21.82 / D */
    /* 0x40 */ f32 cross;     /* units off the line since the latch: the sum of each move times the sine of the yaw's error; right is positive */
    /* 0x44 */ f32 speed;     /* the mover's speed */
    /* 0x48 */ f32 dist;      /* D: the camera's distance from its target */
} LineView;

/* Moon Pound aim (moonaim.c): while the glove jumps, what a slam would do if Z came 1 to 8 game frames from now, and
 * where the nearest hole in the floor is. */
enum MoonCell { MC_GREY, MC_LAND, MC_CLIP, MC_NO_Z, MC_VOID };
enum MoonWhy { MW_OK, MW_NOT_JUMP, MW_WATER, MW_SCALE, MW_NO_FACES, MW_BUILDING };
#define MOON_CELLS 8
/* MoonView.flags */
#define MOWF_SLOW 0x01     /* the frame's work ran out of its time (about 4 ms): some cells are left grey, or the search stopped */
#define MOWF_FULL 0x02     /* more faces than the buffer holds: the nearest are kept */
#define MOWF_NO_EXIT 0x04  /* the hub's Space exit would not open (docs/game.md) */
#define MOWF_A_HELD 0x08   /* A is down and the jump still lifts: every cell is grey */
#define MOWF_CANDS 0x10    /* the list of holes may be missing some: it stopped at 64, or the column is far from the build centre */
#define MOWF_BUILD 0x20    /* a new buffer of faces is being built (over several game frames); the slams use the old one */

typedef struct MoonView {
    /* 0x00 */ u8 live;       /* 1: the glove jumps and the strip is valid */
    /* 0x01 */ u8 why;        /* MoonWhy: why it is not live */
    /* 0x02 */ u8 aimCell;    /* the cell the aim follows: the first one that is not grey; 255: none */
    /* 0x03 */ u8 hasTarget;  /* a hole was found near the column */
    /* 0x04 */ u8 cell[MOON_CELLS]; /* MoonCell of Z on game frame +1 to +8 */
    /* 0x0C */ u8 zoneKnown;  /* dist is the distance to the nearest clipping column, found by search */
    /* 0x0D */ u8 flags;      /* MOWF_ */
    /* 0x0E */ u16 faces;     /* collision faces in the work buffer */
    /* 0x10 */ u16 cands;     /* hole candidates in range */
    /* 0x12 */ u16 rebuilds;  /* times the buffer was filled since the level began */
    /* 0x14 */ u32 counts;    /* CPU counter ticks (46.875 MHz) from the start to the end of the frame's work; other threads' time is in it */
    /* 0x18 */ f32 col[2];    /* the fall column (x, z) of the aim cell */
    /* 0x20 */ f32 vtx[3];    /* the vertex that the hole belongs to */
    /* 0x2C */ f32 target[2]; /* the hole's centre (x, z) */
    /* 0x34 */ f32 dx, dz;    /* from the column to the target, in world units */
    /* 0x3C */ f32 dist;      /* the distance to show */
    /* 0x40 */ f32 phase;     /* d: the centre's height above the vertex in the pass before the cross */
    /* 0x44 */ f32 fwd, right; /* dx, dz turned by the camera: up and right on the screen */
    /* 0x4C */ f32 stickX, stickY; /* the stick that moves the column onto the target if tapped for one read */
    /* 0x54 */ f32 hover;     /* the height of the slam's hover for the aim cell */
    /* 0x58 */ u32 stripCounts; /* the part of counts that the 8 cells took (the counter also runs while other threads do) */
    /* 0x5C */ u16 sims;      /* slams played this frame */
    /* 0x5E */ u16 calls;     /* contact tests (one sphere against one face) */
    /* 0x60 */ u16 iters;     /* steps of the 20-step interpolation in those tests */
    /* 0x62 */ u16 pad62;
} MoonView;

/* Wall clip (trainers.c): MAZ-R with the ball bearing. While the glove holds the ball (JOINED, or PREWHACK after A), the
 * view tells what a cancel (Z) on this game frame would do: the ball's distance to the wall in front of it against the
 * window in which the clip works (docs/game.md, "Walls and the ball bearing"). The cancel frame's numbers are kept, with
 * the frame of the R and whether the glove went through. */
enum ClipWhy { CW_OK, CW_NOT_HOLDING, CW_NOT_BEARING, CW_NO_WALL };
enum ClipVerdict { CV_IN, CV_FAR, CV_NEAR, CV_SLOW };
enum ClipOutcome { CO_PENDING, CO_CLIPPED, CO_NO_CLIP };

typedef struct ClipView {
    /* 0x00 */ u8 why;        /* ClipWhy: why the live part is empty (CW_OK: it shows) */
    /* 0x01 */ u8 verdict;    /* ClipVerdict of a cancel on this frame */
    /* 0x02 */ u8 ground;     /* the glove counts as on the ground (the window is a little narrower) */
    /* 0x03 */ u8 angled;     /* the wall is turned more than 30 degrees from the ball's direction: the window is only a guess */
    /* 0x04 */ u8 hasResult;  /* a cancel was seen: the fields below are valid */
    /* 0x05 */ u8 rVerdict;   /* ClipVerdict at the cancel frame */
    /* 0x06 */ u8 outcome;    /* ClipOutcome */
    /* 0x07 */ s8 rAt;        /* the frames after the cancel frame on which R was pressed (1 is on time), or -1 */
    /* 0x08 */ u16 age;       /* game frames since the cancel frame */
    /* 0x0A */ u16 pad0A;
    /* 0x0C */ f32 dist;      /* the ball's centre to the wall's plane */
    /* 0x10 */ f32 speed;     /* the ball's speed along the wall's normal */
    /* 0x14 */ f32 lo, hi;    /* the window */
    /* 0x1C */ f32 minSpeed;  /* the least speed that clips */
    /* 0x20 */ f32 rDist, rSpeed, rHi; /* the same at the cancel frame */
    /* 0x2C */ f32 behind;    /* how far the glove's centre went behind the wall's plane since the cancel (0: not at all) */
    /* 0x30 */ u32 counts;    /* CPU counts of the wall scan of this frame */
    /* 0x34 */ u32 pad34[3];
} ClipView;

typedef struct TrainerView {
    /* 0x00 */ u32 frame2;   /* frameCount2 of the game frame that made this view */
    /* 0x04 */ u8 on;        /* Run.trainers when it was made */
    /* 0x05 */ u8 valid;     /* 0: the four trick trainers are quiet and draw nothing */
    /* 0x06 */ u8 mbzCount;  /* attempts in mbz[], newest first */
    /* 0x07 */ u8 pad07;
    /* 0x08 */ LMashView lmash;
    /* 0x38 */ MbzAttempt mbz[MBZ_KEEP];
    /* 0xE8 */ LineView line;
    /* 0x134 */ MoonView moon;
    /* 0x198 */ BossView boss; /* has its own live flag: the boss trainer does not need the glove's control */
    /* 0x20C */ ClipView clip;
} TrainerView;

_Static_assert(sizeof(LMashView) == 0x30 && sizeof(MbzAttempt) == 0x2C && sizeof(LineView) == 0x4C &&
                   sizeof(MoonView) == 0x64 && __builtin_offsetof(TrainerView, line) == 0xE8 &&
                   __builtin_offsetof(TrainerView, moon) == 0x134 && __builtin_offsetof(TrainerView, boss) == 0x198 &&
                   sizeof(ClipView) == 0x40 && __builtin_offsetof(TrainerView, clip) == 0x20C && sizeof(TrainerView) == 0x24C,
               "tests/trainers.py has the offsets");

/* The trainer log: the result of each trainer's last attempts, newest first, drawn at the side of the screen (the
 * Settings menu's "Trainer log length" says how many lines show). An attempt is one L mash run (LM), one MBZ-L (MB), one
 * diagonal walk of the line keeper (LK), one slam of the Moon Pound aim (MP), one boss event (BW)
 * or one wall-clip cancel (WC). The game thread writes a line when the attempt ends. The draw thread copies the log
 * and keeps the copy only if no line was written meanwhile (begin and seq, as for the views).
 *
 * The result hold: when an attempt ends, the trainer's panel keeps the view of that moment for the Settings menu's
 * "Trainer result hold" (1 to 3 s, in game frames) and the trainer goes on running unseen. Another attempt that ends
 * during the hold puts its view in place and starts the time again. Nothing is queued. */
#define TLOG_MAX 10
#define TLOG_TEXT 32 /* characters of a line with its end (27 are drawn: from x 158 to the screen's edge) */

typedef struct TrainerLogLine {
    /* 0x00 */ u32 color;
    /* 0x04 */ u8 trainer;        /* the TRAINER_ bit that wrote it; 0: empty */
    /* 0x05 */ char text[TLOG_TEXT - 1];
} TrainerLogLine;

typedef struct TrainerLog {
    /* 0x00 */ u32 begin; /* the number of the write that is going on (seq + 1 while it is) */
    /* 0x04 */ u32 seq;   /* the number of the last complete write */
    /* 0x08 */ u32 total; /* lines written since the level began */
    /* 0x0C */ u32 pad0C;
    /* 0x10 */ TrainerLogLine line[TLOG_MAX]; /* newest first */
} TrainerLog;

_Static_assert(sizeof(TrainerLogLine) == 0x24 && sizeof(TrainerLog) == 0x178, "tests/trainerlog.py has the offsets");

extern TrainerLog trainerLog;
/* The game thread: BIT's trainer ends an attempt. Adds a line to the log and starts the result hold. */
void TrainerResult(int bit, u32 color, const char *fmt, ...);

/* Counters for the test of the pad at the hook (docs/testing.md, "Trainers"). They count only while a trainer is on. */
typedef struct TrainerDiag {
    /* 0x00 */ u32 hookCalls;    /* hook runs */
    /* 0x04 */ u32 frames;       /* game frames seen by both the hook and the pad read in ProcessInput */
    /* 0x08 */ u32 firstDiff;    /* frames on which the first hook run of the frame saw other buttons than ProcessInput */
    /* 0x0C */ u32 lastDiff;     /* the same for the last hook run before ProcessInput */
    /* 0x10 */ u32 firstPressedDiff; /* the same for the presses */
    /* 0x14 */ u32 lastPressedDiff;
    /* 0x18 */ u32 steps;        /* logic runs */
    /* 0x1C */ u32 skipped;      /* ProcessInput runs without a state from the hook for this frame */
} TrainerDiag;

extern TrainerDiag trainerDiag;
extern volatile u32 trainerViewSeq;
extern volatile u32 trainerViewBegin;
extern TrainerView trainerViews[2];

/* The game thread, at the start of each message of RunGame (PracticeClearText), after ActionsGameFrame. */
void TrainersGameFrame(void);
/* The game thread, once a game frame inside PracticeProcessInput, only when Run.trainers is not 0. */
void TrainersProcessInput(void);
/* The menu: turns a trainer on or off and clears its state. */
void TrainersToggle(int bit);
/* The draw thread, only when Run.trainers is not 0. */
void TrainersDraw(Gfx **dl, Gfx *end);
/* The state of the trainer in the Trainers menu: "on" or "off". */
void TrainersText(char *buf, int size, int bit);

#endif
