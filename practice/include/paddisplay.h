#ifndef PRACTICE_PADDISPLAY_H
#define PRACTICE_PADDISPLAY_H

#include "types.h"

/* The game's own controller display, from its demos, shown during play (HUD menu, "Controller display").
 * run.padDisplay: 0 off, 1 controller 1, 2 controller 2. run.padX and run.padY place the pad's left half (in units of
 * 2 pixels), and run.padSize scales the whole display around that point. */
#define PAD_X_DEFAULT 23 /* the place in the demos, 40 pixels higher: clear of the hearts */
#define PAD_Y_DEFAULT 71
#define PAD_SIZES 6
#define PAD_SIZE_DEFAULT 2 /* 100% */
extern const char *const padSizeNames[PAD_SIZES];

/* StartNewLevel, after LoadLevel: loads the display's textures when it is on. */
void PadDisplayLevelStart(void);
/* The controller thread, once a game frame: controller 2's buttons and stick. */
void PadDisplayRead(u16 held, s8 stickX, s8 stickY);

#endif
