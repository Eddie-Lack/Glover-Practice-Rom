#ifndef PRACTICE_HUD_H
#define PRACTICE_HUD_H

#include "rdp.h"
#include "settings.h"

/* The place of an item (HUD_TIMER to HUD_WATCH) or a note on the screen, from x0, y0 to x1, y1 (not included), with
 * its shadow, and Practice.drawFrames (low 16 bits) of the last frame that drew it. The tests read these
 * (tests/hudlayout.py). */
typedef struct HudBox {
    /* 0x00 */ s16 x0;
    /* 0x02 */ s16 y0;
    /* 0x04 */ s16 x1;
    /* 0x06 */ s16 y1;
    /* 0x08 */ u16 frame;
} HudBox;

enum {
    HUD_BOX_MODIFIED = HUD_COUNT,
    HUD_BOX_FREECAM,
    HUD_BOX_SPEED,     /* frame advance or slow motion */
    HUD_BOX_RECORDING,
    HUD_BOX_MESSAGE,
    HUD_BOX_COUNT
};

extern HudBox hudBoxes[HUD_BOX_COUNT];

void HudDraw(Gfx **dl, Gfx *end);
/* The notes that show whatever the HUD items are: MODIFIED, frame advance, the free camera, a recording. */
void HudDrawStatus(Gfx **dl);
/* The message line; drawn after the menu. */
void HudDrawMessage(Gfx **dl);
/* 1 while HudDrawStatus has something to draw that no switch of the Run struct shows: a run that does not count
 * for a practice best time, or a recording. */
int HudStatusShown(void);
/* Moves a block of text inside the part of the screen that a TV shows. X, Y is the start of its main line, WIDTH its
 * width in pixels; ABOVE and BELOW are the pixels of the lines above and below that line. */
void HudFit(int *x, int *y, int width, int above, int below);
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
