#ifndef PRACTICE_PADDISPLAY_H
#define PRACTICE_PADDISPLAY_H

#include "types.h"

/* The game's own controller display, from its demos, shown during play (HUD menu, "Controller display").
 * run.padDisplay: 0 off, 1 controller 1, 2 controller 2. */

/* StartNewLevel, after LoadLevel: loads the display's textures when it is on. */
void PadDisplayLevelStart(void);
/* The controller thread, once a game frame: controller 2's buttons and stick. */
void PadDisplayRead(u16 held, s8 stickX, s8 stickY);

#endif
