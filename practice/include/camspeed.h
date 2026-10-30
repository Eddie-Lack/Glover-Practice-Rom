#ifndef PRACTICE_CAMSPEED_H
#define PRACTICE_CAMSPEED_H

#include "types.h"

/* Camera speed (Game menu, Movement and camera; Run.cameraSpeed: 0 normal, 1 fast, 2 faster; not saved).
 *
 * The game's camera follows Glover in two steps (docs/game.md, "The camera"). The point it looks at
 * (cameraInfo.actor.pos) moves toward the focus by at most followSpeed (20) units a game frame. The camera's body then
 * closes the distance to that point with an acceleration of at most 10 and a speed of at most 20 (MoveCam). A ball that
 * goes faster than the body can follow leaves the camera behind.
 *
 * PracticeMoveCam replaces the call to MoveCam in MainGameLoop. After the game's own MoveCam it raises the two limits:
 * the point's followSpeed, and the camera body's pull and speed limit (camera.phys->maxSpeed). The game's code and
 * data are not changed. With the setting at 0 the call goes straight to MoveCam.
 *
 * The camera's direction turns the stick and the D-pad into Glover's heading, so a faster camera can change a route.
 * A run with it on does not count for a practice best time (BT_OTHER). The HUD shows a camera icon, not MODIFIED. */

#define CAMERA_SPEEDS 3

/* How many times the game's limits the setting allows: 1, 2 and 3. */
extern const u8 cameraGain[CAMERA_SPEEDS];

#endif
