/* The free camera (freecam.h). */
#include "actions.h"
#include "freecam.h"
#include "game.h"
#include "practice.h"
#include "safe.h"
#include "savestate.h"

#define STICK_DEAD 8
#define STICK_MAX 80.0f
/* The camera stays inside the range where the draw thread's maths is safe. */
#define LIMIT 100000.0f
#define FAST 4.0f
#define TURN 0.08f  /* radians a controller read */
#define MIN_FLAT 0.05f /* the smallest horizontal part of the view direction: never straight up or down */

FreeCam freeCam;

_Static_assert(__builtin_offsetof(FreeCam, views) == 0x34 && __builtin_offsetof(FreeCam, active) == 0x3C,
               "tests/freecam.py has the offsets");

/* The view that the draw thread builds: a level, with the world drawn. */
static int InView(void) {
    return (gameInfo.mode == GAME_MODE_LEVEL || gameInfo.mode == GAME_MODE_CAMEO) && gameInfo.level < LEVEL_PRESENT;
}

int FreeCamOn(void) {
    return run.freeCam && freeCam.active;
}

static f32 Axis(int stick) {
    if (stick > -STICK_DEAD && stick < STICK_DEAD) {
        return 0.0f;
    }
    f32 v = stick / STICK_MAX;

    return v > 1.0f ? 1.0f : v < -1.0f ? -1.0f : v;
}

static f32 Limit(f32 v) {
    return v > LIMIT ? LIMIT : v < -LIMIT ? -LIMIT : v;
}

/* Numbers that are nearly zero become zero: their products and squares could be denormal, and the FPU traps on those. */
static f32 Tidy(f32 v) {
    return (v < 1e-6f && v > -1e-6f) ? 0.0f : v;
}

/* A game vector, read as bits first. 0 if a part is unreadable. */
static int ReadVec(const Vec3 *v, f32 *out) {
    return ReadFloat(&v->x, &out[0]) && ReadFloat(&v->y, &out[1]) && ReadFloat(&v->z, &out[2]);
}

/* Puts a view into the camera in one piece: the draw thread reads it with interrupts off too. */
static void Commit(const Vec3 *pos, const Vec3 *dir) {
    u32 mask = __osDisableInt();

    freeCam.pos = *pos;
    freeCam.dir = *dir;
    __osRestoreInt(mask);
}

/* Turns a direction of length 1 so that it is never closer to straight up or down than MIN_FLAT of flat length. */
static void KeepFlat(Vec3 *dir) {
    f32 flat = __builtin_sqrtf(dir->x * dir->x + dir->z * dir->z);

    if (flat >= MIN_FLAT) {
        return;
    }
    if (flat >= 1e-3f) {
        dir->x *= MIN_FLAT / flat;
        dir->z *= MIN_FLAT / flat;
    } else {
        dir->x = 0.0f;
        dir->z = MIN_FLAT;
    }
    dir->y = (dir->y < 0.0f ? -1.0f : 1.0f) * __builtin_sqrtf(1.0f - MIN_FLAT * MIN_FLAT);
}

/* Places the camera at the game's. The game's values are checked first: a NaN would trap the FPU. */
static void Place(void) {
    f32 eye[3];
    f32 at[3];
    f32 d[3];
    Vec3 pos;
    Vec3 dir;

    if (!(ReadVec(&camera.pos, eye) && ReadVec(&cameraInfo.actor.pos, at))) {
        if (!ReadVec(&hand.pos, eye)) {
            eye[0] = eye[1] = eye[2] = 0.0f;
        }
        at[0] = eye[0];
        at[1] = eye[1];
        at[2] = eye[2];
    }
    d[0] = at[0] - eye[0];
    d[1] = at[1] - eye[1];
    d[2] = at[2] - eye[2];
    f32 len = __builtin_sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
    if (!(len > 1.0f)) {
        d[0] = 0.0f;
        d[1] = 0.0f;
        d[2] = 1.0f;
        len = 1.0f;
    }
    pos.x = eye[0];
    pos.y = eye[1];
    pos.z = eye[2];
    dir.x = Tidy(d[0] / len);
    dir.y = Tidy(d[1] / len);
    dir.z = Tidy(d[2] / len);
    KeepFlat(&dir);
    Commit(&pos, &dir);
    freeCam.start = practice.levelStarts;
    freeCam.loads = stateLoads;
    /* In a level's first game frames the game's camera has not been set yet. */
    freeCam.settled = frameCount > 1;
    freeCam.active = 1;
}

/* Turns the view to the right by a radians (to the left when a is negative). */
static void Yaw(Vec3 *dir, f32 a) {
    f32 s = __sinf(a);
    f32 c = __cosf(a);
    f32 dx = dir->x * c - dir->z * s;
    f32 dz = dir->z * c + dir->x * s;

    dir->x = dx;
    dir->z = dz;
}

/* Tilts the view up by a radians (down when a is negative). It stops short of straight up and down. */
static void Pitch(Vec3 *dir, f32 a) {
    f32 h = __builtin_sqrtf(dir->x * dir->x + dir->z * dir->z);
    f32 s = __sinf(a);
    f32 c = __cosf(a);
    f32 nh = h * c - dir->y * s;
    f32 ny = dir->y * c + h * s;

    if (!(nh >= MIN_FLAT) || !(h > 0.0f)) {
        return;
    }
    f32 len = __builtin_sqrtf(nh * nh + ny * ny);
    f32 k = nh / h / len;

    dir->x *= k;
    dir->z *= k;
    dir->y = ny / len;
}

/* Keeps the direction at length 1 against the rounding of many turns. Returns 0 when it had to be reset. */
static int Normalise(Vec3 *dir) {
    f32 len;

    dir->x = Tidy(dir->x);
    dir->y = Tidy(dir->y);
    dir->z = Tidy(dir->z);
    len = __builtin_sqrtf(dir->x * dir->x + dir->y * dir->y + dir->z * dir->z);
    if (len > 0.5f && len < 2.0f) {
        dir->x /= len;
        dir->y /= len;
        dir->z /= len;
        dir->x = Tidy(dir->x);
        dir->y = Tidy(dir->y);
        dir->z = Tidy(dir->z);
        KeepFlat(dir);
        return 1;
    }
    dir->x = dir->y = 0.0f;
    dir->z = 1.0f;
    return 0;
}

void FreeCamRead(void) {
    if (!run.freeCam || !InView()) {
        freeCam.active = 0;
        return;
    }
    /* pos and dir are six floats in a row. Only this thread writes them, so a bad value here came from outside. */
    if (!freeCam.active || freeCam.start != practice.levelStarts || freeCam.loads != stateLoads ||
        (!freeCam.settled && frameCount > 1) || !SafeFloats(&freeCam.pos.x, 6)) {
        Place();
    }
    Vec3 pos = freeCam.pos;
    Vec3 dir = freeCam.dir;
    u16 held = controllerdata[1].button;
    f32 speed = (f32)(1 << run.freeCam);
    f32 turn = TURN;
    f32 forward = Axis(controllerdata[1].stickY);
    f32 right = Axis(controllerdata[1].stickX);
    f32 up = 0.0f;
    f32 yaw = 0.0f;
    f32 pitch = 0.0f;

    if (held & CONT_R) {
        speed *= FAST;
    }
    if (held & CONT_Z) {
        speed /= FAST;
        turn /= FAST;
    }
    if (held & CONT_UP) {
        up += 1.0f;
    }
    if (held & CONT_DOWN) {
        up -= 1.0f;
    }
    if (held & (CONT_CRIGHT | CONT_RIGHT)) {
        yaw += turn;
    }
    if (held & (CONT_CLEFT | CONT_LEFT)) {
        yaw -= turn;
    }
    if (held & CONT_CUP) {
        pitch += turn;
    }
    if (held & CONT_CDOWN) {
        pitch -= turn;
    }
    if (yaw != 0.0f) {
        Yaw(&dir, yaw);
    }
    if (pitch != 0.0f) {
        Pitch(&dir, pitch);
    }
    Normalise(&dir);
    if (forward != 0.0f || right != 0.0f || up != 0.0f || yaw != 0.0f || pitch != 0.0f) {
        /* The view's right is (-dz, dx) over the flat length: the game's own convention (noclip.c). KeepFlat keeps
         * the length above MIN_FLAT; the test is for the sake of the division. */
        f32 h = __builtin_sqrtf(dir.x * dir.x + dir.z * dir.z);

        if (h > 1e-3f) {
            f32 rx = -dir.z / h;
            f32 rz = dir.x / h;

            pos.x = Limit(pos.x + speed * (forward * dir.x + right * rx));
            pos.y = Limit(pos.y + speed * (forward * dir.y + up));
            pos.z = Limit(pos.z + speed * (forward * dir.z + right * rz));
            freeCam.moves++;
            freeCam.settled = 1;
        }
    }
    Commit(&pos, &dir);
}

void FreeCamEye(f32 *eye) {
    if (FreeCamOn()) {
        eye[0] = freeCam.viewEye.x;
        eye[1] = freeCam.viewEye.y;
        eye[2] = freeCam.viewEye.z;
    }
}

/* The draw thread: SetupViewing calls this in place of guLookAtHilite (the game draws with ShadingMode 2). The
 * game's up vector stays unused: the free camera keeps the world's up. The light directions stay as given. The
 * camera is copied once with interrupts off. If a value in it is not a normal number, or the view is nearly
 * straight up or down, the game's own view is built for that frame. */
void PracticeguLookAtHilite(Mtx *m, void *l, void *h, f32 xEye, f32 yEye, f32 zEye, f32 xAt, f32 yAt, f32 zAt, f32 xUp,
                            f32 yUp, f32 zUp, f32 xl1, f32 yl1, f32 zl1, f32 xl2, f32 yl2, f32 zl2, s32 twidth,
                            s32 theight) {
    Vec3 pos;
    Vec3 dir;
    int use;
    u32 mask = __osDisableInt();

    use = run.freeCam && freeCam.active;
    pos = freeCam.pos;
    dir = freeCam.dir;
    __osRestoreInt(mask);
    if (use && SafeFloats(&pos.x, 3) && SafeFloats(&dir.x, 3)) {
        f32 flat = __builtin_sqrtf(dir.x * dir.x + dir.z * dir.z);

        use = flat > 1e-3f;
    } else {
        use = 0;
    }
    if (!use) {
        guLookAtHilite(m, l, h, xEye, yEye, zEye, xAt, yAt, zAt, xUp, yUp, zUp, xl1, yl1, zl1, xl2, yl2, zl2, twidth,
                       theight);
        return;
    }
    freeCam.gameEye.x = xEye;
    freeCam.gameEye.y = yEye;
    freeCam.gameEye.z = zEye;
    freeCam.viewEye = pos;
    guLookAtHilite(m, l, h, pos.x, pos.y, pos.z, pos.x + dir.x, pos.y + dir.y, pos.z + dir.z, 0.0f, 1.0f, 0.0f, xl1,
                   yl1, zl1, xl2, yl2, zl2, twidth, theight);
    freeCam.views++;
}
