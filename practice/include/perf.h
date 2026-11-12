#ifndef PRACTICE_PERF_H
#define PRACTICE_PERF_H

#include "game.h"
#include "types.h"

/* The time that the practice code takes, in ticks of the CPU counter (46.875 MHz; one video frame is 781,250 ticks).
 * The counters only record: nothing reads them but tests/perf.py. Each stage has one writer thread. A thread that
 * runs above the writer (the draw thread above the game thread, the controller thread above both) adds its time to
 * the writer's, so a maximum can include another thread's work. */
enum PerfStageId {
    PERF_CLEAR,    /* game thread: the practice work at the start of a message (PracticeClearText), not its waits */
    PERF_INPUT,    /* game thread: the practice work around ProcessInput, once a game frame in a level */
    PERF_DRAW,     /* draw thread: the practice work at the end of a frame's list (PracticePrintTimers) */
    PERF_DRAW_ALL, /* draw thread: the whole list, from SendLastFrame to the end of PrintTimers' practice work */
    PERF_CTRL,     /* controller thread: PracticeReadController without the game's own read */
    PERF_STAGES
};

typedef struct PerfStage {
    /* 0x00 */ u32 n;    /* times measured */
    /* 0x04 */ u32 sum;  /* ticks, added up (wraps; a test takes differences) */
    /* 0x08 */ u32 max;  /* the longest one; a test writes 0 to start a new window */
    /* 0x0C */ u32 last;
} PerfStage;

#define PERF_INTERVALS 8

typedef struct Perf {
    /* 0x00 */ PerfStage stage[PERF_STAGES];
    /* 0x50 */ u32 interval[PERF_INTERVALS]; /* game frames by video interrupts since the game frame before (the last
                                              * bucket holds that many or more) */
    /* 0x70 */ u32 lastVi;                   /* __osViIntrCount at the last game frame */
    /* 0x74 */ u32 drawStart;                /* osGetCount at the start of the draw thread's list */
    /* 0x78 */ u32 lastCount;                /* osGetCount at the last game frame */
    /* 0x7C */ u32 gapTicks;                 /* ticks between game frames, added up over the gaps of under 8 interrupts */
    /* 0x80 */ u32 gapFrames;                /* the gaps in gapTicks */
} Perf;

extern Perf perf;

void PerfAdd(int stage, u32 ticks);

/* The game thread, once a game frame in a level. */
void PerfGameFrame(void);

#endif
