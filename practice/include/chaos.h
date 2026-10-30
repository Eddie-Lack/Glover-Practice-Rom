#ifndef PRACTICE_CHAOS_H
#define PRACTICE_CHAOS_H

#include "types.h"

/* Chaos inputs (Game menu, third last item; Run.chaos, off at start, not saved): practice for recovery. Now and then the
 * tool changes one thing, chosen at random:
 *   - it adds a one-frame press of a random button (A, B, Z, L, R or a C button);
 *   - it takes away the next press that the player makes (the button is hidden from the game until it is let go);
 *   - it nudges the glove: its place by up to 6 units sideways or 3 units up, or its speed by up to 2.5.
 * The menu sets how often: rare (about every 10 seconds), sometimes (3 seconds) or often (1 second), counted in
 * game frames of a level. The tool has its own random number generator, seeded from the CPU counter when it is
 * turned on, and never touches the game's seed. The HUD shows "MODIFIED chaos" while it is on. It changes
 * nothing while the practice menu is open, the game is paused or stepped, a recording is made or plays back, or noclip is on. A nudge of the speed keeps each
 * component within 60.
 *
 * The game thread decides (ChaosGameFrame, from ActionsGameFrame); the controller thread makes the input changes
 * (ChaosInput, in the input layer, after the bindings have seen the real input, so the bindings and the input
 * display are not affected). */
typedef struct Chaos {
    /* 0x00 */ u32 rng;
    /* 0x04 */ u32 frames;   /* game frames rolled while on */
    /* 0x08 */ u32 events;   /* changes decided */
    /* 0x0C */ u32 added;    /* presses added */
    /* 0x10 */ u32 dropped;  /* presses taken away */
    /* 0x14 */ u32 nudgedPos; /* nudges of the glove's place */
    /* 0x18 */ u32 nudgedVel; /* nudges of its speed */
    /* 0x1C */ u16 pendingAdd; /* the button to add at the next controller read (a CONT_ bit), or 0 */
    /* 0x1E */ u8 dropArmed;   /* 1: the next press is taken away */
    /* 0x1F */ u8 seeded;
    /* 0x20 */ s32 lastFrame; /* frameCount2 at the last roll: the game calls ChaosGameFrame for other messages too */
} Chaos;

extern Chaos chaos;

/* The game thread, at the start of each game frame. */
void ChaosGameFrame(void);
/* The controller thread, before the game reads controller CNTLR, after the other changes to its buttons. */
void ChaosInput(u8 cntlr);

#endif
