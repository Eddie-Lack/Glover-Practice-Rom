#ifndef PRACTICE_EARLYTITLE_H
#define PRACTICE_EARLYTITLE_H

#include "types.h"

/* The team's early title menu (docs/game.md, "The early title menu"). TitleScreen (game.c) is an earlier version
 * of the title: continue, new game and level select over a rotating view of level 7. Nothing in the retail game
 * calls it. RunGame calls DoMenu while gameInfo.mode is 1; the hook PracticeDoMenu calls TitleScreen in its place
 * while EarlyTitle.on is 1. The Warp menu opens it, and brings back the normal title. */
typedef struct EarlyTitle {
    /* 0x00 */ u8 on;       /* 1: PracticeDoMenu calls TitleScreen */
    /* 0x01 */ u8 pending;  /* 1: the level is being left (QUIT_EXIT); the early title shows when mode 1 starts */
    /* 0x02 */ u8 request;  /* EARLYTITLE_OPEN or EARLYTITLE_BACK, for the next game frame */
    /* 0x04 */ u32 frames;  /* calls of TitleScreen */
} EarlyTitle;

#define EARLYTITLE_OPEN 1
#define EARLYTITLE_BACK 2

extern EarlyTitle earlyTitle;

/* The menu: asks for the next game frame to open the early title, or to go back to the normal one. */
void EarlyTitleRequest(int what);
/* The game thread, at the start of each game frame, when anything is on. */
void EarlyTitleGameFrame(void);
/* Frees the title or the level that is loaded (nothing, in mode 1 before the title has set up). Used before a level
 * starts from a title and before the early title is set up. */
void LeaveTitle(void);
/* Whether a request is waiting, the early title shows or an exit for it is running. */
static inline int EarlyTitleBusy(void) {
    return earlyTitle.on | earlyTitle.request | earlyTitle.pending;
}

#endif
