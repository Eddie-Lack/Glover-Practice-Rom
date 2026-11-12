#ifndef PRACTICE_SWEEP_H
#define PRACTICE_SWEEP_H

#include "types.h"

/* The Fear boss sweep: plays the Fear boss's opening lob once for each start that Fear 3's thunder and a carried
 * ballPain allow, and keeps what happened to the burst ball.
 *
 * Each run warps to Fear 3, moves the glove and the ball onto the level's exit, sets the thunder flashes that are left
 * (rain.lightning) when the exit starts, and holds A: the exit spin then ends at once. A is pressed on every second
 * controller read through the end of level screen and the first game frames of the boss, which makes the glove lob
 * the ball in the boss's opening cutscene. At the boss's first game frame the run sets ballPain. It then watches both
 * bursts of the ball and the flight after the second, until the ball presses the ball switch or the life is lost.
 *
 * Run K sets SWEEP_RAIN(K) flashes and ballPain SWEEP_PAIN(K). The results stay in RAM until the console is turned
 * off, at SWEEP_RESULTS: the last 4 KB of the last ghost slot, which only a ghost of more than 7900 game frames
 * reaches. */
#define SWEEP_RUNS 88
#define SWEEP_RAIN(k) ((k) / 8)
#define SWEEP_PAIN(k) ((k) % 8)
#define SWEEP_RESULTS 0x8077F000

enum SweepStatus {
    SWEEP_NOT_RUN,
    SWEEP_MISS,      /* the life was lost after the second burst */
    SWEEP_HIT,       /* the ball pressed the ball switch */
    SWEEP_NO_BURST,  /* a stage took too long: no lob, no exit or no second burst */
};

typedef struct SweepResult {
    /* 0x00 */ u8 rain;     /* flashes set at the exit */
    /* 0x01 */ u8 pain;     /* ballPain set at the boss's first game frame */
    /* 0x02 */ u8 carried;  /* flashes left at the boss's first game frame */
    /* 0x03 */ u8 status;   /* SweepStatus */
    /* 0x04 */ s16 burst1;  /* game frames of the two bursts */
    /* 0x06 */ s16 burst2;
    /* 0x08 */ s16 aura;    /* the game frame on which Frankie's aura animation (0) started after game frame 400 */
    /* 0x0A */ s16 end;     /* the game frame of the switch press, or of the lost life */
    /* 0x0C */ u32 seed1;   /* rseed after each burst's game frame */
    /* 0x10 */ u32 seed2;
    /* 0x14 */ u16 nearest; /* the ball's nearest pass to the switch after the second burst, in tenths */
    /* 0x16 */ u16 pad;
} SweepResult; /* size 0x18 */

typedef struct Sweep {
    u8 stage;   /* 0: off */
    u8 run;     /* the run in progress, 0 to SWEEP_RUNS - 1 */
    u8 done;    /* runs finished */
    u8 hits;
    u16 frames; /* game frames in the stage */
} Sweep;

extern Sweep sweep;

/* The menu: starts the sweep from run 0, or stops it. SweepText: the count of runs and hits, and the view's page. */
void SweepStart(int arg);
void SweepStop(int arg);
void SweepText(char *buf, int size, int arg);
/* The game thread, at the start of each game frame (ActionsGameFrame), while a sweep runs. */
void SweepGameFrame(void);
/* The controller thread, after the practice input layer, before the game reads the controller. */
void SweepRead(u8 cntlr);

#endif
