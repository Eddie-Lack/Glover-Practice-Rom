#ifndef PRACTICE_NOCLIP_H
#define PRACTICE_NOCLIP_H

#include "types.h"

/* Moon jump and noclip: two ways to move the glove that the retail game does not have. Both are switches in the
 * Game menu (Run.moonJump, Run.noclip) and are not saved. The HUD shows "MODIFIED" while either is on.
 *
 * Moon jump: while L is held on controller 2, or L with the stick held fully up on controller 1, the glove's
 * vertical speed is set to a steady rise at the start of each game frame.
 *
 * Noclip: the glove flies where the stick points (relative to the camera, like walking). D-pad up and down move
 * it up and down, R makes it four times faster. The game sees no buttons and no stick, except Start and the C
 * buttons. The game's collision with the landscape and its gravity are switched off for the glove through its
 * flags (actor.h, flag 0x10 and flag 0x01). A savestate keeps the glove's flags as the game has them. Switching noclip off puts the flags back and the glove falls from
 * where it is. */

/* 1 while noclip is flying the glove. The quick-action bindings are off then: the stick and the D-pad fly. */
int NoclipFlying(void);
/* The savestate code, around a save and after a load. */
void NoclipBeforeSave(void);
void NoclipAfterSave(void);
void NoclipAfterLoad(void);

/* The game thread, at the start of each game frame (ActionsGameFrame). */
void NoclipGameFrame(void);
/* The controller thread, before the game reads controller CNTLR: takes the buttons away from the game. */
void NoclipHide(u8 cntlr);

#endif
