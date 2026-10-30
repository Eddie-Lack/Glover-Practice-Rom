/* Moon jump and noclip (noclip.h). */
#include "actions.h"
#include "game.h"
#include "input.h"
#include "noclip.h"
#include "safe.h"

/* Actor.flags: 0x01 the actor falls under gravity, 0x10 it collides with the landscape (actor.c,
 * UpdateActorCollisionAndGravity). Flag 0x08, collision with other actors, stays on. */
#define FLAG_GRAVITY 0x01
#define FLAG_LANDSCAPE 0x10
#define FLAGS_OFF (FLAG_GRAVITY | FLAG_LANDSCAPE)

/* The moon jump's rise in units a game frame. UpdateActor limits the glove's vertical speed to 15. */
#define MOON_SPEED 8.0f
#define STICK_FULL 60
#define STICK_DEAD 8
#define STICK_MAX 80.0f
/* The glove stays inside the range where the game's own maths and the collision view stay safe. */
#define LIMIT 20000.0f
#define FAST 4.0f

/* The buttons that the game does not see in noclip: every one but Start and the C buttons. */
#define HIDDEN (CONT_A | CONT_B | CONT_Z | CONT_L | CONT_R | CONT_UP | CONT_DOWN | CONT_LEFT | CONT_RIGHT)

static u8 flying;       /* noclip has taken the flags away */
static s32 flyLevel;    /* the level in which it did */
static s32 savedFlags;  /* the bits of FLAGS_OFF that the glove had */
static s32 flyFrame;    /* frameCount2 when the glove last moved */

static int Playing(void) {
    return ActionsInLevel() && !gamePaused;
}

static int MoonPort1(void) {
    return (input.held & CONT_L) && input.stickY >= STICK_FULL;
}

static int MoonHeld(void) {
    return (input.held2 & CONT_L) || MoonPort1();
}

void NoclipHide(u8 cntlr) {
    if (run.moonJump && MoonPort1()) {
        input.hidden |= CONT_L;
    }
    if (run.noclip && Playing() && cntlr == 0) {
        controllerdata[cntlr].button &= ~HIDDEN;
        controllerdata[cntlr].stickX = 0;
        controllerdata[cntlr].stickY = 0;
    }
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

/* Moves the glove one step. The velocity is zero when UpdateActor adds it to the position, so the game's own
 * movement adds nothing. The faces that the glove touched are forgotten: with no landscape collision the game
 * does not refresh them, and its friction step reads them. */
static void Fly(void) {
    if (!SafePointer(hand.obj) || !SafeFloats(&hand.pos.x, 3) || !SafeFloat(&camera.rot.y)) {
        return;
    }
    if (!flying) {
        flying = 1;
        flyLevel = gameInfo.level;
        savedFlags = 0;
    }
    savedFlags |= hand.flags & FLAGS_OFF;
    hand.flags &= ~FLAGS_OFF;
    for (int i = 0; i < 4; i++) {
        hand.obj->planes[i] = NULL;
    }
    hand.vel.x = hand.vel.y = hand.vel.z = 0.0f;

    f32 speed = (f32)(2 << run.noclipSpeed);
    f32 s = __sinf(camera.rot.y);
    f32 c = __cosf(camera.rot.y);
    f32 forward = Axis(input.stickY);
    f32 right = Axis(input.stickX);
    f32 up = 0.0f;

    if (input.held & CONT_R) {
        speed *= FAST;
    }
    if (input.held & CONT_UP) {
        up += 1.0f;
    }
    if (input.held & CONT_DOWN) {
        up -= 1.0f;
    }
    /* Forward is (sin, cos) of the camera's angle and right is (-cos, sin): the game's ProcessInput. */
    hand.pos.x = Limit(hand.pos.x + speed * (s * forward - c * right));
    hand.pos.y = Limit(hand.pos.y + speed * up);
    hand.pos.z = Limit(hand.pos.z + speed * (c * forward + s * right));
}

int NoclipFlying(void) {
    return run.noclip && Playing();
}

/* A savestate holds the glove's flags as they are. While noclip has taken bits away, the saved copy gets them
 * back. */
void NoclipBeforeSave(void) {
    if (flying && ActionsInLevel()) {
        hand.flags |= savedFlags;
    }
}

void NoclipAfterSave(void) {
    if (flying && ActionsInLevel()) {
        hand.flags &= ~FLAGS_OFF;
    }
}

/* The loaded state has its own flags. Noclip takes them again on the next frame if it is on. */
void NoclipAfterLoad(void) {
    flying = 0;
}

void NoclipGameFrame(void) {
    if (flying && flyLevel != gameInfo.level) {
        /* The saved bits belong to the glove of the old level. */
        flying = 0;
    }
    if (run.noclip) {
        /* RunGame calls this for every message of the main queue: the glove moves once a game frame. */
        if (Playing() && frameCount2 != flyFrame) {
            flyFrame = frameCount2;
            Fly();
        }
    } else if (flying) {
        /* Off: the glove has its flags again and falls from where it is. */
        if (ActionsInLevel()) {
            hand.flags |= savedFlags;
        }
        flying = 0;
    }
    if (run.moonJump && !run.noclip && Playing() && MoonHeld()) {
        hand.vel.y = MOON_SPEED;
    }
}
