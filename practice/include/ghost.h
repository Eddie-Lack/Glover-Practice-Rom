#ifndef PRACTICE_GHOST_H
#define PRACTICE_GHOST_H

#include "types.h"

/* The ghost of a recorded run: a see-through glove and ball, drawn where the glove and the ball were on the same
 * game frame of a recorded run.
 *
 * While an input recording is made (recording.h), the game thread also keeps the glove's and the ball's place each
 * game frame. The place of game frame N is kept at index N - start, where start is the game frame counter at the
 * recording's savestate. So the ghost stays in step with a live run that began at the same state, or at the same
 * level start. One ghost is kept for each of up to GHOST_SLOTS levels. A new recording of a level replaces that
 * level's ghost when it ends with at least one game frame kept; a recording that is stopped at once keeps the
 * old one. A ghost is not saved to the EEPROM.
 *
 * The frames are in the Expansion Pak's memory after the savestate pool, in front of the input recording. */
#define GHOST_BUFFER 0x8075C000
#define GHOST_SLOTS 4
#define GHOST_SLOT_FRAMES 8192 /* game frames a ghost keeps: about 6.8 minutes */
#define GHOST_SLOT_BYTES 0x20000

/* One game frame: places in whole game units, the glove's heading as a turn (65536 is a full circle). */
typedef struct GhostFrame {
    /* 0x00 */ s16 hand[3];
    /* 0x06 */ s16 ball[3];
    /* 0x0C */ u16 yaw;
    /* 0x0E */ u8 flags; /* GHOST_HAND, GHOST_BALL, GHOST_YAW: what was readable on that frame */
    /* 0x0F */ u8 pad;
} GhostFrame;

#define GHOST_HAND 1
#define GHOST_BALL 2
#define GHOST_YAW 4

typedef struct GhostSlot {
    /* 0x00 */ u8 used;   /* 1: a finished ghost of LEVEL */
    /* 0x01 */ u8 level;
    /* 0x02 */ u16 count; /* game frames, counting the gaps */
    /* 0x04 */ s32 start; /* the game frame counter at the savestate */
    /* 0x08 */ u32 stamp; /* newer ghosts have larger numbers */
} GhostSlot;

typedef struct Ghost {
    /* 0x00 */ GhostSlot slots[GHOST_SLOTS];
    /* 0x30 */ s8 writing;     /* the slot being filled, or -1 */
    /* 0x31 */ u8 pad[3];
    /* 0x34 */ s32 lastFrame2; /* frameCount2 at the last sample */
    /* 0x38 */ u32 clock;
    /* 0x3C */ u32 stored;     /* game frames kept since the start */
    /* What the draw thread drew last. */
    /* 0x40 */ u32 draws;      /* frames that got a ghost */
    /* 0x44 */ s32 drawIndex;  /* the game frame of the ghost's places */
    /* 0x48 */ s16 drawnHand[3];
    /* 0x4E */ s16 drawnBall[3];
    /* 0x54 */ u16 drawnYaw;
    /* 0x56 */ u8 drawnFlags;
    /* 0x57 */ u8 drawnSlot;
    /* 0x58 */ const void *list; /* the ghost's display list in the last frame */
    /* 0x5C */ u32 commands;     /* its length */
    /* 0x60 */ s32 drawFrame;    /* frameCount when the draw thread looked */
    /* 0x64 */ f32 liveHand[3];  /* the glove's and the ball's places as the draw thread saw them (for the tests) */
    /* 0x70 */ f32 liveBall[3];
    /* While a recording is made. */
    /* 0x7C */ s32 lastFrame;    /* frameCount at the last sample */
    /* 0x80 */ u32 loads;        /* stateLoads when the recording started */
} Ghost;

extern Ghost ghost;

/* The game thread, after a recording's savestate was saved: starts keeping places. The recording ends (with the
 * places kept so far) when the game frame counter goes back, the level changes or a state is loaded. */
void GhostRecordStart(void);
/* The game thread, once for each message of the main queue (PracticeClearText). Keeps one place for each game
 * frame while a recording is made. With no recording it returns at once. */
void GhostGameFrame(void);
/* The draw thread, at the end of the frame's display list, while the switch is on. Appends one command to the
 * game's list when the level has a ghost and it has a place for this game frame. */
void GhostDraw(void);
/* The Controller Pak, after it loaded a recording: forgets every ghost, which belong to other recordings. */
void GhostClear(void);
/* The menu: the switch, and its text. */
void GhostToggle(int arg);
void GhostText(char *buf, int size, int arg);

#endif
