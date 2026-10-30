#ifndef PRACTICE_INPUT_H
#define PRACTICE_INPUT_H

#include "types.h"

/* The practice code's view of the controllers, updated once a game frame by the controller thread before the
 * game reads them. Buttons use the CONT_ bits of game.h. */
typedef struct Input {
    u16 held;     /* controller 1, as the player holds it */
    u16 pressed;  /* controller 1, pressed on this frame */
    s8 stickX;
    s8 stickY;
    u16 held2;    /* controller 2 */
    u16 pressed2;
    u16 hidden;   /* buttons kept from the game until they are released */
} Input;

extern Input input;

/* The last presses of controller 1, newest first. */
#define PRESS_HISTORY 4
typedef struct Press {
    u16 bit;     /* the button's CONT_ bit; 0: none */
    u8 holding;  /* still held */
    u8 frames;   /* game frames held */
    s32 frame;   /* the level's game frame (frameCount) of the press */
} Press;

extern Press presses[PRESS_HISTORY];

/* The game's button order: the CONT_ bit of each pad[].button[] index. */
extern const u16 padBits[14];

/* Hides the buttons held now from the game until each is released. */
void InputHideHeld(void);

#endif
