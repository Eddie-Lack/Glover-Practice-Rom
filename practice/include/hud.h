#ifndef PRACTICE_HUD_H
#define PRACTICE_HUD_H

#include "rdp.h"

void HudDraw(Gfx **dl, Gfx *end);
void HudDrawStatus(Gfx **dl);
/* Called by the game thread once a game frame; counts the lag. */
void HudTick(void);
void HudResetLag(void);
/* The name of a glove action (enums.h, HandAction), or "?". */
const char *HandActionName(int mode);
/* One step of the game's random number generator (maths.c, Random), without calling it. */
u32 RngNext(u32 r);
/* Called at the start of each level: the level before it becomes the last level. */
void HudLevelStart(void);
/* The random number seed has been set to its value at a level's start: the call count starts again at 0. */
void HudRngRestart(void);
/* The seed was stepped by the practice code: the call count goes on from the new seed without counting them. */
void HudRngMoved(void);
/* Shows a short message for two seconds. */
void HudMessage(const char *text);
/* 1 while a message shows. */
int HudMessageShown(void);

#endif
