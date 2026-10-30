#ifndef PRACTICE_FREECAM_H
#define PRACTICE_FREECAM_H

#include "game.h"
#include "types.h"

/* The free camera: a second camera that controller 2 flies. While it is on (Game menu, "Free camera": off or a
 * speed), the draw thread builds the view from it. The game keeps its own camera and its own state: nothing the
 * game's logic reads is written. The hook PracticeguLookAtHilite (practice_hooks.h) replaces the eye, the target and the
 * up vector of the view matrix, and nothing else.
 *
 * The controller thread moves the camera on every controller read (once a game frame, at the game's pace), from
 * controller 2. The thread runs on while the game waits. The camera also moves in frame advance and with the menu
 * open:
 *   stick            forward and back along the view, and sideways
 *   D-pad up, down   up and down
 *   C left, right    turn left and right
 *   C up, down       look up and down
 *   D-pad left, right  turn left and right (the same as C left and right)
 *   R, Z             four times faster, four times slower (also for turning)
 * The camera starts where the game's camera is. It is placed again when the switch goes on, at the start of each
 * level, once the level's first game frames have set the game's camera, and after a savestate is loaded. The state
 * is not part of a savestate or a recording.
 *
 * The draw thread reads the camera while the controller thread writes it. Both do so with interrupts off, in one
 * piece. A value that is not a normal number makes the draw thread use the game's own camera for that frame.
 *
 * What the game draws is chosen by its own camera: an actor (the landscape's objects are actors too) is drawn when
 * its distance to the game's camera is below its radius plus the far plane (IsActorVisible, object.c). The free
 * camera sees nothing that the game's camera does not reach. */
typedef struct FreeCam {
    /* 0x00 */ Vec3 pos;      /* the eye */
    /* 0x0C */ Vec3 dir;      /* the view direction, length 1 */
    /* 0x18 */ Vec3 gameEye;  /* the eye that the game gave the last view (not used while the camera is on) */
    /* 0x24 */ Vec3 viewEye;  /* the eye of the last view that the draw thread built */
    /* 0x30 */ u32 moves;     /* controller reads that moved the camera */
    /* 0x34 */ u32 views;     /* views built from the free camera */
    /* 0x38 */ u32 start;     /* practice.levelStarts when the camera was placed */
    /* 0x3C */ u8 active;     /* 1: placed, and the views come from it */
    /* 0x3D */ u8 settled;    /* 1: placed after the level's first game frames, or flown since */
    /* 0x40 */ u32 loads;     /* stateLoads when the camera was placed */
} FreeCam;

extern FreeCam freeCam;

/* The controller thread, after it has read the controllers. */
void FreeCamRead(void);
/* The controller 2 buttons that the camera uses while it is on (the stick too). Bindings and the game's debug pad
 * do not see them; the other buttons work as usual. */
#define FREECAM_BUTTONS                                                                                          \
    (CONT_UP | CONT_DOWN | CONT_LEFT | CONT_RIGHT | CONT_CUP | CONT_CDOWN | CONT_CLEFT | CONT_CRIGHT | CONT_R | \
     CONT_Z)

/* 1 while the free camera is on and controller 2 flies it. */
int FreeCamOn(void);
/* Collision view: replaces the eye of a frame (three floats) with the free camera's, when it is on. */
void FreeCamEye(f32 *eye);

#endif
