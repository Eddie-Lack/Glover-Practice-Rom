#ifndef PRACTICE_BESTTIMES_H
#define PRACTICE_BESTTIMES_H

#include "types.h"

/* The practice best times: the best in-game time of each level, in game frames, kept apart from the game's save.
 * A level's time counts when the level is finished: at CheckForLevelCompleted (the end of level screen), or on the
 * Hard path at PrepareSong (a record) or InitOverlayList (no record), where the game has already set its own
 * records. A run with the game modified, with frame advance or slow motion, or with a
 * savestate loaded during the level is not kept. The Hi Scores screen shows these times (PracticeprintHiscores). */

/* One time for each bit of levelInfo[].bit that belongs to a timed level: five levels in each of six worlds, in
 * the order of the game's own table (bestTimes) and of the Hi Scores screen. */
#define BEST_LEVELS 30

/* The data that a later commit can save: 0 means no time yet. A time is at most 0xFFFF game frames. */
typedef struct PracticeBests {
    u16 frames[BEST_LEVELS];
} PracticeBests;

extern PracticeBests practiceBests;

/* The reasons that a run is not kept. They are bits of BestTimesState.taint. */
enum {
    BT_NOCLIP = 1 << 0,    /* noclip or moon jump */
    BT_CHAOS = 1 << 1,
    BT_POKES = 1 << 2,     /* memory pokes or flips */
    BT_ACTORS = 1 << 3,    /* an actor made or moved, Glover moved (teleport, Go to checkpoint), a power-up given */
    BT_SET = 1 << 4,       /* a set glove action or ball state */
    BT_TIME = 1 << 5,      /* frame advance or slow motion */
    BT_STATE = 1 << 6,     /* a savestate was loaded */
    BT_OTHER = 1 << 7,     /* a cheat, controller 2's debug controls, a frozen or changed seed, Lives or Health set, a camera speed, hidden objects */
};

enum {
    BT_RESULT_NONE,
    BT_RESULT_NEW,      /* a new best */
    BT_RESULT_SLOWER,   /* kept nothing: not faster than the best */
    BT_RESULT_MODIFIED, /* kept nothing: the run was modified */
};

/* The tests read these at their offsets (tests/besttimes.py). */
typedef struct BestTimesState {
    /* 0x00 */ u8 showGame;   /* 1: the Hi Scores screen shows the game's records */
    /* 0x01 */ u8 taint;      /* reasons that the level being played is not kept; cleared at a level's start */
    /* 0x02 */ u8 lastResult; /* BT_RESULT_* of the last finished level */
    /* 0x03 */ s8 lastBit;    /* its place in practiceBests, or -1 */
    /* 0x04 */ u16 lastFrames; /* its time in game frames */
    /* 0x06 */ u16 finished;  /* levels finished since the boot */
} BestTimesState;

extern BestTimesState bestTimesState;

/* The game thread, on every message of a game frame (idempotent): notes a modified run. */
void BestTimesTick(void);
/* Notes that the level being played is not kept, for the reasons BITS. */
void BestTimesTaint(int bits);
/* StartNewLevel: the new level has no reasons yet. */
void BestTimesLevelStart(void);
/* A level has been finished (one of the three call sites above). */
void BestTimesLevelDone(void);
/* Forgets every practice best time. */
void BestTimesClear(void);
/* Shows the time in the form "m:ss.hh". */
void BestTimesFormat(char *buf, int size, u32 frames);
/* The number of levels with a time. */
int BestTimesCount(void);

#endif
