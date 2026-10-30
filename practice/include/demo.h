#ifndef PRACTICE_DEMO_H
#define PRACTICE_DEMO_H

#include "types.h"

/* The game's demo recordings (docs/game.md, "Demo recordings"). The Warp menu starts one the way the title does
 * (DemoKeyRun in title.c), or all of them one after another. A demo is a level that ReadControllerFromData plays
 * from a file in the ROM (autoPlayback 2). When it ends, or A, B or Start leaves it, the game goes to the title. */
#define DEMO_COUNT 8
#define DEMO_ONE 1
#define DEMO_ALL 2

typedef struct Demo {
    u8 choice;  /* the Warp menu's choice: an index in demoLevels */
    u8 request; /* DEMO_ONE or DEMO_ALL, for the next game frame */
    u8 all;     /* 1: Play all runs */
    u8 index;   /* the demo that plays, or that Play all plays next */
    u8 start;   /* 1: the level is being left for a demo; the setup comes when mode 8 starts the level */
    u8 advance; /* 1: a demo ended; the game is on its way to the title */
} Demo;

extern Demo demo;
/* The level of each demo: the five of the title in its order, then the two it never shows, then the cutscene. */
extern const u8 demoLevels[DEMO_COUNT];

/* The menu: asks for the next game frame to start a demo. */
void DemoRequest(int what);
/* The game thread, at the start of each game frame, when DemoBusy. */
void DemoGameFrame(void);
/* What the title does before it starts a demo level. */
void DemoSetup(void);
/* Ends a demo that plays, frees its recording, and stops Play all. */
void DemoAbort(void);
/* The controller thread: reads the demo's input in place of the controller, and notes its end. */
void DemoRead(u8 cntlr);

static inline int DemoBusy(void) {
    return demo.request | demo.start | demo.advance;
}

#endif
