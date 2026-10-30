#ifndef PRACTICE_GAMETIMER_H
#define PRACTICE_GAMETIMER_H

#include "rdp.h"

/* The full-game timer, in real time (docs/plan.md, "Timers"). It starts on the game frame after a new game's
 * difficulty is chosen, and stops on the game frame after Glover enters the cave with the last crystal. Time
 * with the practice menu open does not count. */
#define SPLITS_MAX 48

typedef struct GameTimer {
    u8 running;
    u8 done;
    u8 splitCount;
    u32 elapsed;               /* video frames counted so far */
    u32 splits[SPLITS_MAX];    /* elapsed at each split */
    s8 splitLevel[SPLITS_MAX]; /* the level that each split ends */
} GameTimer;

extern GameTimer gameTimer;

/* The game thread, at the start of each game frame, while the timer is shown. */
void GameTimerTick(void);
/* Video frames that the practice menu held the game. */
void GameTimerExclude(u32 frames);
void GameTimerReset(void);
void GameTimerStart(void);
void GameTimerDraw(Gfx **dl, int x, int y);
/* Hundredths of a second in a count of video frames (NTSC: 60000/1001 a second). */
u32 GameTimerHundredths(u32 frames);

#endif
