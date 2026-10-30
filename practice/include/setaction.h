#ifndef PRACTICE_SETACTION_H
#define PRACTICE_SETACTION_H

#include "types.h"

/* Set action: puts the glove in an action from a list, or the ball in a state, with the game's own functions
 * (ApplyAction, BurstBall, ReleaseBall, DropBall). The Game menu's "Set action" sub menu picks one and asks for
 * it (Run.setRequest, Run.setBallRequest). The game thread starts it at the start of its next game frame.
 *
 * The list leaves out the actions that can crash the game from a state without their set up:
 * - hopping (HAND_HOPPING) follows the enemy in the glove's link, and the link is empty;
 * - frog tongue (HAND_FROG_TONGUE) reads the tongue bullet that FireBulletFromActor may not give;
 * - cast spell (HAND_CASTSPELL) reads the spell in player.castSpell, which is empty when no spell is held;
 * - nothing (HAND_NOTHING) is the state after a cast with no spell and only waits.
 * The other actions are in the list. Actions that need the thing they work on (cling and the climbs need a
 * ledge, push a block, the frog actions the frog power-up) are set up by the game as if the glove were there:
 * the game goes on from the state and usually leaves it within a few game frames. The HUD shows "MODIFIED"
 * for a while after each use. */

/* The number of actions in the list, their names for the menu, and the glove actions (enums.h) they set. */
#define SETACTION_GLOVE_COUNT 41
extern const char *const setActionGloveNames[SETACTION_GLOVE_COUNT];
extern const u8 setActionGloveModes[SETACTION_GLOVE_COUNT];

#define SETACTION_BALL_COUNT 3
extern const char *const setActionBallNames[SETACTION_BALL_COUNT];

/* Asks for the chosen action: arg 0 the glove's, arg 1 the ball's. Safe from any thread. */
void SetActionRequest(int arg);
/* The game thread, at the start of each game frame (ActionsGameFrame). */
void SetActionGameFrame(void);

#endif
