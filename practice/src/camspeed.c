/* Camera speed (camspeed.h). */
#include "actions.h"
#include "camspeed.h"
#include "game.h"
#include "safe.h"

_Static_assert(__builtin_offsetof(Run, cameraSpeed) == 0x4D, "tests/camspeed.py has the offset");

const u8 cameraGain[CAMERA_SPEEDS] = { 1, 2, 3 };

void MoveCam(void);

/* The game's limits (camera.c, MoveCam; actor.c, UpdateActorPositionDependencies). */
#define FOLLOW_MAX 20.0f     /* followSpeed's top */
#define BODY_PULL 10.0f      /* the most that MoveCam adds to the camera's speed toward the target in a game frame */
#define BODY_SPEED 20.0f     /* the camera's top speed (phys->maxSpeed is twice the pull) */

void PracticeMoveCam(void) {
    f32 g;
    f32 dx, dz, len, want, pull, extra, speed;

    MoveCam();
    if (run.cameraSpeed == 0 || run.cameraSpeed >= CAMERA_SPEEDS || cameraInfo.mode != 1 || cameraInfo.overrideTimer != 0 ||
        cameraInfo.destActive != 0 || !SafePointer(camera.phys) || !SafeFloats(&camera.pos.x, 3) ||
        !SafeFloats(&cameraInfo.actor.pos.x, 3)) {
        return;
    }
    g = (f32)cameraGain[run.cameraSpeed];
    /* The point that the camera looks at. The game sets followSpeed to 0 and followTimer to 1 each time the glove takes
     * the ball (hand.c, ApplyAction, HAND_JOINED), which is on every hold press of a mash, and the point then follows
     * the ball at 0 to 2 units a game frame. The setting keeps followSpeed at its top, and raises the top. */
    cameraInfo.followTimer = 0;
    cameraInfo.followSpeed = FOLLOW_MAX * g;
    /* The body: MoveCam added pull = min(excess / 2, 10) toward the point, where excess is the distance beyond the
     * wanted one. Add what g times that allows (g times the pull, up to g times the limit), and let the next UpdateActor
     * keep the larger speed. */
    ((ActorPhysics *)camera.phys)->maxSpeed = 2.0f * BODY_PULL * g;
    dx = camera.pos.x - cameraInfo.actor.pos.x;
    dz = camera.pos.z - cameraInfo.actor.pos.z;
    len = __builtin_sqrtf(dx * dx + dz * dz);
    want = cameraInfo.distance + levelHandScale * 10.0f / 0.05f;
    if (len <= want || len < 1.0f) {
        return;
    }
    pull = (len - want) / 2.0f;
    extra = (pull * g < BODY_PULL * g ? pull * g : BODY_PULL * g) - (pull < BODY_PULL ? pull : BODY_PULL);
    camera.vel.x -= extra * dx / len;
    camera.vel.z -= extra * dz / len;
    speed = __builtin_sqrtf(camera.vel.x * camera.vel.x + camera.vel.z * camera.vel.z);
    if (speed > BODY_SPEED * g) {
        camera.vel.x *= BODY_SPEED * g / speed;
        camera.vel.z *= BODY_SPEED * g / speed;
    }
}
