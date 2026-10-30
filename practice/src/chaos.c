/* Chaos inputs (chaos.h). */
#include "actions.h"
#include "chaos.h"
#include "game.h"
#include "input.h"
#include "menu.h"
#include "noclip.h"
#include "recording.h"
#include "safe.h"

Chaos chaos;

_Static_assert(__builtin_offsetof(Chaos, added) == 0x0C && __builtin_offsetof(Chaos, pendingAdd) == 0x1C,
               "tests/chaos.py has the offsets");

/* Changes in 1000 game frames for rare, sometimes and often. */
static const u16 rates[4] = { 0, 5, 17, 50 };

/* The buttons that a press can be added for: not Start (it pauses) and not the D-pad (it steers). */
static const u16 addable[] = { CONT_A, CONT_B, CONT_Z, CONT_L, CONT_R, CONT_CUP, CONT_CDOWN, CONT_CLEFT, CONT_CRIGHT };

/* Positions stay in the range where the game's maths and the collision view are safe (noclip.c). */
#define LIMIT 20000.0f
#define SIDE 6.0f
#define UP 3.0f
#define SPEED 2.5f
/* A speed component stays within this, however many nudges add up. */
#define SPEED_LIMIT 60.0f

/* xorshift32. */
static u32 Next(void) {
    u32 x = chaos.rng;

    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    return chaos.rng = x;
}

/* A number from -1 to 1. */
static f32 Spread(void) {
    return (int)(Next() % 2001) / 1000.0f - 1.0f;
}

static f32 Limit(f32 v) {
    return v > LIMIT ? LIMIT : v < -LIMIT ? -LIMIT : v;
}

static f32 LimitSpeed(f32 v) {
    return v > SPEED_LIMIT ? SPEED_LIMIT : v < -SPEED_LIMIT ? -SPEED_LIMIT : v;
}

/* The tool changes things only while the game plays at full speed. Added and dropped presses would be recorded, but a
 * nudge of the glove would not. Nothing changes while a recording is made or played. */
static int Active(void) {
    return run.chaos && ActionsInLevel() && !gamePaused && !menuOpen && !run.frameAdvance && !recording.playing &&
           !recording.recording && !run.noclip;
}

static void Nudge(void) {
    if (!SafePointer(hand.obj) || !SafeFloats(&hand.pos.x, 3) || !SafeFloats(&hand.vel.x, 3)) {
        return;
    }
    if (Next() & 1) {
        /* Only up and sideways: a nudge down could push the glove into the floor. */
        hand.pos.x = Limit(hand.pos.x + SIDE * Spread());
        hand.pos.y = Limit(hand.pos.y + UP * (Spread() + 1.0f) / 2.0f);
        hand.pos.z = Limit(hand.pos.z + SIDE * Spread());
        hand.prevPos = hand.pos;
        chaos.nudgedPos++;
    } else {
        hand.vel.x = LimitSpeed(hand.vel.x + SPEED * Spread());
        hand.vel.y = LimitSpeed(hand.vel.y + SPEED * (Spread() + 1.0f) / 2.0f);
        hand.vel.z = LimitSpeed(hand.vel.z + SPEED * Spread());
        chaos.nudgedVel++;
    }
}

void ChaosGameFrame(void) {
    if (!chaos.seeded) {
        chaos.rng = osGetCount() | 1;
        chaos.seeded = 1;
    }
    if (!Active()) {
        chaos.pendingAdd = 0;
        chaos.dropArmed = 0;
        return;
    }
    /* RunGame calls ActionsGameFrame for every message of the main queue, not only the game frame's. */
    if (frameCount2 == chaos.lastFrame) {
        return;
    }
    chaos.lastFrame = frameCount2;
    chaos.frames++;
    if (Next() % 1000 >= rates[run.chaos & 3]) {
        return;
    }
    chaos.events++;
    switch (Next() % 3) {
        case 0:
            chaos.pendingAdd = addable[Next() % ARRAY_COUNT(addable)];
            break;
        case 1:
            chaos.dropArmed = 1;
            break;
        default:
            Nudge();
            break;
    }
}

void ChaosInput(u8 cntlr) {
    ContPad *c = &controllerdata[cntlr];

    /* input.pressed and input.held are controller 1's. */
    if (cntlr != 0 || !Active()) {
        return;
    }
    if (chaos.dropArmed) {
        /* The lowest button pressed on this read, but not Start. */
        u16 fresh = input.pressed & ~CONT_START;

        if (fresh) {
            u16 bit = fresh & -fresh;

            c->button &= ~bit;
            input.hidden |= bit;
            chaos.dropArmed = 0;
            chaos.dropped++;
        }
    }
    if (chaos.pendingAdd) {
        /* A button the player holds already would make no new press. */
        if (!(input.held & chaos.pendingAdd)) {
            c->button |= chaos.pendingAdd;
            chaos.added++;
        }
        chaos.pendingAdd = 0;
    }
}
