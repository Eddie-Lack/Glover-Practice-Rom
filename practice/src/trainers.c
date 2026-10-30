/* Trick trainers (trainers.h): the common code, the L mash trainer, the line keeper and the MBZ-L trainer. The Moon
 * Pound aim is in moonaim.c and the boss windows are in bosswin.c. */
#include "actions.h"
#include "bosswin.h"
#include "fmt.h"
#include "freecam.h"
#include "game.h"
#include "input.h"
#include "moonaim.h"
#include "practice.h"
#include "safe.h"
#include "savestate.h"
#include "settings.h"
#include "text.h"
#include "trainers.h"

TrainerDiag trainerDiag;
TrainerView trainerViews[2];
volatile u32 trainerViewSeq;
volatile u32 trainerViewBegin;

/* The compiler does not move reads or writes of the views across this. */
#define BARRIER() __asm__ volatile("" ::: "memory")

#define WHITE 0xFFFFFFFF
#define DIM 0x707888FF
#define LABEL 0xC0D0FFFF
#define GREEN 0x60FF60FF
#define YELLOW 0xFFE070FF
#define RED 0xFF5050FF
#define BLUE 0x60A0FFFF
#define ORANGE 0xFFA040FF

_Static_assert(__builtin_offsetof(TrainerDiag, steps) == 0x18 && __builtin_offsetof(Run, trainers) == 0x48 &&
                   sizeof(TrainerView) == 0x24C && __builtin_offsetof(TrainerView, mbz) == 0x38,
               "tests/trainers.py has the offsets");

/* ---------------------------------------------------------------------------------------------------------
 * The state of the game at the hook
 */

#define HAND_ACCEL 5.0f      /* hand.phys->accel (hand.c, MoveHand) */
#define HAND_MASS 10.0f
#define HAND_MAX_SPEED 14.0f
#define HAND_FRICTION_PLANE 0.6f
#define MOVE_FRESH 13.0f     /* ApplyAction sets the ball's moveSpeed to 13 on every action change */
#define BALL_JOINED_MAX 7.0f /* BallJoinedMaxSpeed, and the glove's moveSpeed (hand.c) */
#define BALL_DEEP_WATER 0x200000

/* What the trainers read from the game at the start of a game frame, before MainGameLoop changes it. */
typedef struct Snap {
    s32 frame2;     /* frameCount2 at the hook */
    u8 fresh;       /* made since the last logic run */
    u8 quiet;       /* 1: the trainers do not run now */
    s16 mode;       /* the glove's action, and the one before it */
    s16 prevMode;
    u8 waterState;  /* the ball's */
    u8 onGround;    /* the ball's obj->onGround */
    u8 planeUnder;  /* the ball touched a plane in the last game frame (planes[0]), which gives the ground friction */
    u8 difficulty;
    s8 ballType;
    s32 ballFlags;
    f32 ballMove;   /* the ball's horizontal move in the last game frame */
    f32 ballSpeed;  /* the ball's horizontal speed now: what ball.prevVel will be in the coming game frame */
    f32 camYaw;
    f32 ballFriction;     /* ball.friction: what UpdateActor uses on the ground in the coming game frame */
    f32 ballAccel, ballMass, ballMax, ballMin, ballPhysFriction;
    f32 handPhysFriction;
    /* The line keeper's: the camera and the mover (the ball when held or stood on, else the glove). */
    u8 lineOk;
    s32 camMode, camOverride, camDest, camOrbit, camAngleLock, camBlocked, camFall;
    f32 camPos[2], camVel[2], camTarget[2];
    f32 handMove;
    u8 handOnGround;
    /* The Moon Pound aim's. */
    u8 moonOk;
    MoonIn moon;
    /* The wall clip's: the ball and the glove at the hook. */
    u8 clipOk;
    u8 handPlane;       /* the glove touched a plane on the last game frame (planes[0]), which gives the ground friction */
    f32 ballPos[3], ballVel[3], handPos[3];
} Snap;

static Snap snap;
static u32 seenStarts, seenLoads;
static u8 dirty;             /* the four trick trainers hold state or a view */
static u8 bossDirty;         /* the boss trainer holds state or a view */
static u8 bossFresh;         /* BossRead found a fight at the hook of the game frame that snap.frame2 names */
static u8 clearRequest;      /* trainer bits to clear on the next game frame (the menu sets them) */

/* ---------------------------------------------------------------------------------------------------------
 * The trainer log and the result hold (trainers.h)
 */

TrainerLog trainerLog;
static TrainerView held;     /* the views of the trainers that hold a result (CopyPart) */
static s32 holdUntil[7];     /* for each trainer bit: the game frame (frameCount2) at which its hold ends */
static u8 ended;             /* trainer bits whose attempt ended in this logic run (BuildView starts their hold) */
static s16 logLevel = -1;

static int BitIndex(int bit) {
    int i = 0;

    while (i < 6 && !((bit >> i) & 1)) {
        i++;
    }
    return i;
}

/* Starts and ends a change of the log in the way that the draw thread can tell (begin and seq). */
static void LogBegin(void) {
    trainerLog.begin = trainerLog.seq + 1;
    BARRIER();
}

static void LogEnd(void) {
    BARRIER();
    trainerLog.seq = trainerLog.begin;
}

void TrainerResult(int bit, u32 color, const char *fmt, ...) {
    static const char tags[7][3] = { "LM", "LK", "MB", "MP", "BW", "MT", "WC" };
    TrainerLogLine *l;
    const char *tag = tags[BitIndex(bit)];
    char *t;
    va_list args;

    LogBegin();
    for (int i = TLOG_MAX - 1; i > 0; i--) {
        trainerLog.line[i] = trainerLog.line[i - 1];
    }
    l = &trainerLog.line[0];
    l->color = color;
    l->trainer = (u8)bit;
    t = l->text;
    *t++ = tag[0];
    *t++ = tag[1];
    *t++ = ' ';
    va_start(args, fmt);
    FormatV(t, (int)sizeof(l->text) - 3, fmt, args);
    va_end(args);
    trainerLog.total++;
    LogEnd();
    ended |= (u8)bit;
}

/* Takes the lines of the trainers in BITS out of the log. */
static void LogDrop(int bits) {
    int to = 0;

    LogBegin();
    for (int i = 0; i < TLOG_MAX; i++) {
        if (trainerLog.line[i].trainer & bits) {
            continue;
        }
        trainerLog.line[to++] = trainerLog.line[i];
    }
    for (; to < TLOG_MAX; to++) {
        trainerLog.line[to].trainer = 0;
    }
    LogEnd();
}

/* The hold in game frames: 20 a second. */
static int HoldFrames(void) {
    return settings.trainerHold > 1 ? (settings.trainerHold - 1) * 20 : 0;
}

/* Copies the part of a view that belongs to trainer BIT. */
static void CopyPart(TrainerView *d, const TrainerView *src, int bit) {
    switch (bit) {
        case TRAINER_LMASH: {
            u8 metro = d->lmash.metro; /* the metronome's box is not held */

            d->lmash = src->lmash;
            d->lmash.metro = metro;
            break;
        }
        case TRAINER_LINE:
            d->line = src->line;
            break;
        case TRAINER_MBZL:
            for (int i = 0; i < MBZ_KEEP; i++) {
                d->mbz[i] = src->mbz[i];
            }
            d->mbzCount = src->mbzCount;
            break;
        case TRAINER_MOON:
            d->moon = src->moon;
            break;
        case TRAINER_BOSS:
            d->boss = src->boss;
            break;
        case TRAINER_CLIP:
            d->clip = src->clip;
            break;
        default:
            break;
    }
}

/* The views of the trainers whose attempts ended are kept, and a held view takes the place of the live one. */
static void ApplyHold(TrainerView *v) {
    for (int i = 0; i < 7; i++) {
        int bit = 1 << i;

        if (ended & bit) {
            if (bit != TRAINER_MOON) {
                CopyPart(&held, v, bit); /* the Moon Pound aim's view is kept by its own step */
            }
            holdUntil[i] = snap.frame2 + HoldFrames();
        }
        if ((v->on & bit) && holdUntil[i] - snap.frame2 > 0) {
            CopyPart(v, &held, bit);
            if (bit == TRAINER_MBZL && v->mbzCount > 0) {
                v->mbz[0].doneFrame = (u32)v->frame2; /* the newest result stays in full */
            } else if (bit == TRAINER_BOSS) {
                v->boss.frame2 = v->boss.lastFrame2;
            }
        }
    }
    ended = 0;
}

/* The conditions for the trainers to run (docs/plan.md, "Trick trainers"): a level with the glove under the
 * player's control, no pause, no free camera, no noclip or moon jump. */
static int Quiet(void) {
    if (!ActionsInLevel() || gamePaused || controlMethod != CONTROL_HAND || handControl.noControlTimer != 0) {
        return 0;
    }
    if (FreeCamOn() || run.noclip || run.moonJump) {
        return 0;
    }
    return SafePointer(hand.state) && SafePointer(ball.state) && SafePointer(hand.obj) && SafePointer(ball.obj) &&
           SafePointer(hand.phys) && SafePointer(ball.phys);
}

static f32 Sqrt(f32 v) {
    return __builtin_sqrtf(v);
}

/* The line keeper's reads. 0 if a number is not usable: the trainer is then silent. */
static int ReadLine(Snap *s) {
    f32 hx, hz, hpx, hpz;

    if (!(ReadFloat(&camera.pos.x, &s->camPos[0]) && ReadFloat(&camera.pos.z, &s->camPos[1]) &&
          ReadFloat(&camera.vel.x, &s->camVel[0]) && ReadFloat(&camera.vel.z, &s->camVel[1]) &&
          ReadFloat(&cameraInfo.actor.pos.x, &s->camTarget[0]) && ReadFloat(&cameraInfo.actor.pos.z, &s->camTarget[1]) &&
          ReadFloat(&hand.pos.x, &hx) && ReadFloat(&hand.pos.z, &hz) && ReadFloat(&hand.prevPos.x, &hpx) &&
          ReadFloat(&hand.prevPos.z, &hpz))) {
        return 0;
    }
    s->camMode = cameraInfo.mode;
    s->camOverride = cameraInfo.overrideTimer;
    s->camDest = cameraInfo.destActive;
    s->camOrbit = cameraInfo.orbiting;
    s->camAngleLock = cameraInfo.angleLock;
    s->camBlocked = cameraInfo.blocked;
    s->camFall = cameraInfo.fallTime;
    s->handOnGround = hand.obj->onGround != 0;
    s->handMove = Sqrt((hx - hpx) * (hx - hpx) + (hz - hpz) * (hz - hpz));
    return 1;
}

/* The Moon Pound aim's reads. */
static int ReadMoon(Snap *s) {
    MoonIn *m = &s->moon;
    const ActorPhysics *hp = hand.phys;
    f32 scale;

    if (!(ReadFloat(&hand.pos.x, &m->pos[0]) && ReadFloat(&hand.pos.y, &m->pos[1]) && ReadFloat(&hand.pos.z, &m->pos[2]) &&
          ReadFloat(&hand.vel.x, &m->vel[0]) && ReadFloat(&hand.vel.y, &m->vel[1]) && ReadFloat(&hand.vel.z, &m->vel[2]) &&
          ReadFloat(&hand.obj->spheres[0].radius, &m->radius) && ReadFloat(&gravity, &m->gravity) &&
          ReadFloat(&levelHandScale, &scale) && ReadFloat(&hp->friction, &m->handFriction) &&
          ReadFloat(&camera.rot.y, &m->camYaw))) {
        return 0;
    }
    m->slamRadius = 16.0f * scale / 0.05f;
    m->actionLock = handControl.actionLock;
    m->actionTime = handControl.actionTime;
    m->mode = hand.state->mode;
    m->waterState = hand.state->waterState;
    m->onGround = hand.obj->onGround;
    m->aHeld = 0;
    m->scaled = player.powerups[4] != 0 || gameInfo.level == LEVEL_SPACEBOSS3;
    m->noExit = gameInfo.level < 8 && saveSlot[currentSaveSlot].hubTarget != saveSlot[currentSaveSlot].hubLevel &&
                !ballStatic && !gameInfo.worldComplete[6];
    return 1;
}

/* The wall clip's reads. */
static int ReadClip(Snap *s) {
    return ReadFloat(&ball.pos.x, &s->ballPos[0]) && ReadFloat(&ball.pos.y, &s->ballPos[1]) &&
           ReadFloat(&ball.pos.z, &s->ballPos[2]) && ReadFloat(&ball.vel.x, &s->ballVel[0]) &&
           ReadFloat(&ball.vel.z, &s->ballVel[2]) && ReadFloat(&hand.pos.x, &s->handPos[0]) &&
           ReadFloat(&hand.pos.y, &s->handPos[1]) && ReadFloat(&hand.pos.z, &s->handPos[2]) &&
           ((s->ballVel[1] = 0.0f), (s->handPlane = hand.obj->planes[0] != NULL), 1);
}

/* 1 if the state was read. A value that is not a normal number makes the trainers quiet. */
static int ReadSnap(Snap *s) {
    const ActorPhysics *bp = ball.phys;
    const ActorPhysics *hp = hand.phys;
    f32 bx, bz, px, pz, vx, vz;

    if (!(ReadFloat(&ball.pos.x, &bx) && ReadFloat(&ball.pos.z, &bz) && ReadFloat(&ball.prevPos.x, &px) &&
          ReadFloat(&ball.prevPos.z, &pz) && ReadFloat(&ball.vel.x, &vx) && ReadFloat(&ball.vel.z, &vz) &&
          ReadFloat(&camera.rot.y, &s->camYaw) && ReadFloat(&ball.friction, &s->ballFriction) &&
          ReadFloat(&bp->accel, &s->ballAccel) && ReadFloat(&bp->mass, &s->ballMass) &&
          ReadFloat(&bp->maxSpeed, &s->ballMax) && ReadFloat(&bp->minSpeed, &s->ballMin) &&
          ReadFloat(&bp->friction, &s->ballPhysFriction) && ReadFloat(&hp->friction, &s->handPhysFriction))) {
        return 0;
    }
    if (s->ballMass < 0.5f || s->ballMax <= 0.0f) {
        return 0;
    }
    s->ballMove = Sqrt((bx - px) * (bx - px) + (bz - pz) * (bz - pz));
    s->ballSpeed = Sqrt(vx * vx + vz * vz);
    s->mode = hand.state->mode;
    s->prevMode = hand.state->prevMode;
    s->waterState = ball.state->waterState;
    s->onGround = ball.obj->onGround;
    s->planeUnder = ball.obj->planes[0] != NULL;
    s->difficulty = saveSlot[currentSaveSlot].difficultyLevel;
    s->ballType = ballControl.type;
    s->ballFlags = ball.flags;
    s->lineOk = (run.trainers & TRAINER_LINE) ? ReadLine(s) : 0;
    s->moonOk = (run.trainers & TRAINER_MOON) ? ReadMoon(s) : 0;
    s->clipOk = (run.trainers & TRAINER_CLIP) ? ReadClip(s) : 0;
    return 1;
}

/* The pad that the logic reads: copied from pad[ActiveController] when the logic runs. */
typedef struct TPad {
    u8 button[14];
    u8 pressed[14];
    f32 mag;
    s16 stickX, stickY;
} TPad;

static void ReadPad(TPad *p) {
    const Pad *src = &pad[ActiveController];

    for (int i = 0; i < 14; i++) {
        p->button[i] = src->button[i] != 0;
        p->pressed[i] = src->pressed[i] != 0;
    }
    if (!ReadFloat(&src->mag, &p->mag)) {
        p->mag = 0.0f;
    }
    p->stickX = src->stickX;
    p->stickY = src->stickY;
}

/* ---------------------------------------------------------------------------------------------------------
 * L mash
 */

#define LM_MOVES 20
#define LM_REST_GAP 9          /* a stand part this long is a rest */
#define LM_ATTEMPT_GAP 30      /* a gap this long starts a new attempt */
#define LM_OTHER_FRAMES 20     /* game frames in other glove actions before the trainer lets go */
#define LM_RUN_GAP 12          /* game frames after the last press that end a mash run (the trainer log's attempt) */
#define LM_RUN_MIN 3           /* presses that make a run */
#define MASH_WARMUP 100        /* frames of the perfect mash before the target is measured */
#define MASH_FRAMES 140
#define LM_AIR_FRAMES 3        /* game frames off the ground before the ball counts as in the air */
#define WEAK_PUSH 0.867f       /* the stand push below which the cap of 7 returns on the ground */
#define MAX_MOVE 30.0f         /* a move in one game frame above this is a teleport (a respawn, a checkpoint), not play */

static struct {
    s32 tick;
    u8 kind;               /* the last press */
    s32 kindTick;
    u8 verdict, gap, over;
    u8 cells;
    u8 strip[LM_STRIP];
    f32 moves[LM_MOVES];
    u8 nMoves;
    u8 head;
    f32 peak, move;
    u8 other;              /* game frames in other actions in a row */
    u8 airTicks;           /* game frames in a row with the ball off the ground */
    f32 lastYaw;
    u8 haveYaw;
    u8 warn;
    f32 target;            /* and the key it was made for */
    s32 keyMag;
    u8 keyAir;
    f32 keyFriction, keyAccel, keyMass, keyMax, keyMin;
    u8 keyValid;
    u8 metro;              /* the metronome runs: the first hold press of an attempt started it */
    s32 metroTick;         /* the game frame of that press: the beats fall on it and on every 2nd frame after it */
    s32 lastPress;         /* the game frame of the newest L press */
    u8 runPresses, runOn;  /* the presses of the mash run that goes on (0: none) and those on time, for the log */
    u8 beatPresses, beatOn; /* the presses of the metronome's run, and those on the beat */
} lm;

static void LMashReset(void) {
    u8 *p = (u8 *)&lm;

    for (u32 i = 0; i < sizeof(lm); i++) {
        p[i] = 0;
    }
}

/* The ball's steady move in a perfect mash (a press every second game frame) with the pad held straight, on
 * flat ground or in the air. A scalar port of the model in tools/tricks/lmash.py. The ball moves along the pad
 * direction, and only speeds are needed. The model was checked against the game on 41 logs (to 1e-5). */
static f32 MashTarget(f32 mag, int air, const Snap *s) {
    f32 k = HAND_ACCEL / 0.5f * s->ballAccel * 10.0f / s->ballMass * 0.32f;
    f32 standAdd = HAND_MASS / (s->ballMass < 10.0f ? 15.0f : s->ballMass * 1.5f);
    f32 airFriction = 1.0f - (1.0f - s->ballPhysFriction) / 4.0f;
    f32 handFree = 1.0f - (1.0f - s->handPhysFriction) / 4.0f;
    f32 idleFriction = 1.0f - (1.0f - s->ballPhysFriction) * 4.0f;
    f32 bv = 7.0f, hv = 0.0f, bmove = BALL_JOINED_MAX, bfric = s->ballPhysFriction;
    int joined = 1, hplane = 1, coll = 0, padOn = mag > 0.0f;
    f32 sum = 0.0f;

    if (idleFriction < 0.0f) {
        idleFriction = 0.0f;
    }
    for (int t = 0; t < MASH_FRAMES; t++) {
        int press = (t & 1) == 0;
        int wasJoined = joined;
        f32 start = bv, moved, old, a;

        /* UpdateActor(hand): the speed limit, then the friction. */
        if (hv > HAND_MAX_SPEED) {
            hv = HAND_MAX_SPEED;
        }
        hv *= hplane ? HAND_FRICTION_PLANE : handFree;
        if (air) {
            hplane = 0;
        } else if (coll) {
            hplane = wasJoined;
        }
        /* UpdateActor(ball) */
        if (bv > s->ballMax) {
            bv = s->ballMax;
        }
        moved = bv;
        bv *= air ? airFriction : bfric;
        if (moved < s->ballMin && !air) {
            bv = 0.0f;
        }
        /* ProcessInput */
        old = bv;
        a = padOn ? k * mag / (wasJoined ? 120.0f : 180.0f) : 0.0f;
        if (wasJoined) {
            if (old <= 2.0f) {
                a *= 1.25f;
            }
            bv += a;
            if (old != 0.0f && bv > bmove) {
                bv = old <= bmove ? bmove : bv * 0.9f;
            }
        } else {
            bv += a;
            if (old != 0.0f && bv > s->ballMax) {
                bv = old <= s->ballMax ? s->ballMax : bv * 0.9f;
            }
            if (old <= bmove && bv > bmove) {
                bv = bmove;
            }
        }
        /* MoveBall, MoveHand */
        bfric = padOn ? s->ballPhysFriction : idleFriction;
        coll = !wasJoined;
        if (wasJoined) {
            if (press) {
                bv += hv * standAdd;
                joined = 0;
                bmove = MOVE_FRESH;
            } else {
                bmove = BALL_JOINED_MAX;
            }
        } else if (press) {
            bv = start;
            hv = start;
            joined = 1;
            bmove = MOVE_FRESH;
        } else {
            bmove = BALL_JOINED_MAX;
        }
        if (t >= MASH_WARMUP) {
            sum += start > s->ballMax ? s->ballMax : start;
        }
    }
    return sum / (MASH_FRAMES - MASH_WARMUP);
}

/* The angle in [-pi, pi). The inputs are finite and small (ReadFloat). */
static f32 WrapAngle(f32 a) {
    f32 turns = (a + 3.14159265f) / 6.28318531f;
    s32 whole = (s32)turns;

    if ((f32)whole > turns) {
        whole--;
    }
    return a - (f32)whole * 6.28318531f;
}

/* The trainer log's lines for the end of an L mash run and of a metronome run. */
static void LMashEndRun(void) {
    int gaps = lm.runPresses - 1;
    int good = lm.runOn * 10 >= gaps * 9;
    int fair = lm.runOn * 10 >= gaps * 7;

    if ((run.trainers & TRAINER_LMASH) && lm.runPresses >= LM_RUN_MIN) {
        TrainerResult(TRAINER_LMASH, good ? GREEN : fair ? YELLOW : RED, "%dP %d ON TIME %.1f %s", lm.runPresses, lm.runOn,
                      (double)lm.peak, good ? "GOOD" : fair ? "OK" : "SLOPPY");
    }
    lm.runPresses = lm.runOn = 0;
}

static void MetroEndRun(void) {
    if ((run.trainers & TRAINER_METRO) && lm.beatPresses >= LM_RUN_MIN) {
        int good = lm.beatOn * 10 >= lm.beatPresses * 9;
        int fair = lm.beatOn * 10 >= lm.beatPresses * 7;

        TrainerResult(TRAINER_METRO, good ? GREEN : fair ? YELLOW : RED, "%d OF %d ON THE BEAT", lm.beatOn, lm.beatPresses);
    }
    lm.beatPresses = lm.beatOn = 0;
}

static void LMashStep(const Snap *s, const TPad *p) {
    int inBall = s->mode == HAND_JOINED || s->mode == HAND_BALLWALK;
    int deep = (s->ballFlags & BALL_DEEP_WATER) && s->waterState;
    int press = inBall && !deep && p->pressed[PADB_L];
    f32 yawStep = 0.0f;
    s32 mag = (s32)(p->mag * 4.0f + 0.5f); /* in quarter units */
    int air;

    lm.tick++;
    if (lm.runPresses && lm.tick - lm.lastPress >= LM_RUN_GAP) {
        LMashEndRun();
    }
    if (lm.metro && lm.tick - lm.lastPress > LM_ATTEMPT_GAP) {
        lm.metro = 0;
        MetroEndRun();
    }
    if (lm.haveYaw) {
        yawStep = WrapAngle(s->camYaw - lm.lastYaw);
        if (yawStep < 0.0f) {
            yawStep = -yawStep;
        }
    }
    lm.lastYaw = s->camYaw;
    lm.haveYaw = 1;

    /* The strip: the glove's action on this frame, and the press. */
    for (int i = 0; i + 1 < LM_STRIP; i++) {
        lm.strip[i] = lm.strip[i + 1];
    }
    lm.strip[LM_STRIP - 1] = (s->mode == HAND_JOINED ? 1 : s->mode == HAND_BALLWALK ? 2 : 0) | (press ? 0x80 : 0);
    if (lm.cells < LM_STRIP) {
        lm.cells++;
    }

    if (!inBall) {
        if (++lm.other >= LM_OTHER_FRAMES) {
            u8 cells = lm.cells;

            if (lm.metro) {
                MetroEndRun();
            }
            u8 strip[LM_STRIP];

            for (int i = 0; i < LM_STRIP; i++) {
                strip[i] = lm.strip[i];
            }
            LMashReset();
            lm.cells = cells;
            for (int i = 0; i < LM_STRIP; i++) {
                lm.strip[i] = strip[i];
            }
        }
        return;
    }
    lm.other = 0;
    /* A mash at full speed skips over the floor now and then: the ball is in the air after 3 frames off the ground. */
    lm.airTicks = s->onGround ? 0 : lm.airTicks < 255 ? lm.airTicks + 1 : 255;
    air = lm.airTicks >= LM_AIR_FRAMES;

    /* The ball's move in the last game frame. A teleport is not a move. */
    if (s->ballMove <= MAX_MOVE) {
        lm.move = s->ballMove;
        lm.moves[lm.head] = s->ballMove;
        lm.head = (lm.head + 1) % LM_MOVES;
        if (lm.nMoves < LM_MOVES) {
            lm.nMoves++;
        }
        if (lm.move > lm.peak) {
            lm.peak = lm.move;
        }
    }

    if (press) {
        u8 kind = s->mode == HAND_JOINED ? LM_STAND : LM_HOLD;
        s32 gap = lm.kind != LM_NONE && lm.kind != kind ? lm.tick - lm.kindTick : 0;

        lm.verdict = LMV_NONE;
        lm.gap = 0;
        lm.over = 0;
        if (gap > LM_ATTEMPT_GAP || gap == 0) {
            /* The first press of an attempt. */
            lm.peak = lm.move;
        } else {
            lm.gap = (u8)gap;
            if (gap < 2) {
                lm.verdict = LMV_EARLY;
            } else if (gap == 2) {
                lm.verdict = LMV_ON_TIME;
            } else if (kind == LM_STAND) {
                lm.verdict = LMV_LATE;
                lm.over = (u8)(gap - 2);
            } else if (gap >= LM_REST_GAP) {
                lm.verdict = LMV_REST;
                lm.peak = lm.move;
            } else {
                lm.verdict = LMV_SLOW;
                lm.over = (u8)(gap - 2);
            }
        }
        lm.kind = kind;
        lm.kindTick = lm.tick;
        lm.lastPress = lm.tick;
        lm.runPresses++;
        lm.runOn += lm.verdict == LMV_ON_TIME;
        if (!lm.metro && kind == LM_HOLD) {
            lm.metro = 1;
            lm.metroTick = lm.tick;
            lm.beatPresses = lm.beatOn = 0;
        }
        if (lm.metro) {
            lm.beatPresses++;
            lm.beatOn += ((lm.tick - lm.metroTick) & 1) == 0;
        }
    }

    /* The target, for the pad that is held now. */
    if (mag > 0 && (!lm.keyValid || mag != lm.keyMag || air != lm.keyAir || s->ballPhysFriction != lm.keyFriction ||
                    s->ballAccel != lm.keyAccel || s->ballMass != lm.keyMass || s->ballMax != lm.keyMax ||
                    s->ballMin != lm.keyMin)) {
        lm.target = MashTarget((f32)mag / 4.0f, air, s);
        lm.keyMag = mag;
        lm.keyAir = air;
        lm.keyFriction = s->ballPhysFriction;
        lm.keyAccel = s->ballAccel;
        lm.keyMass = s->ballMass;
        lm.keyMax = s->ballMax;
        lm.keyMin = s->ballMin;
        lm.keyValid = 1;
    } else if (mag == 0) {
        lm.target = 0.0f;
        lm.keyValid = 0;
    }

    /* The warnings. */
    lm.warn = 0;
    if (mag == 0) {
        lm.warn |= LMW_RELEASED;
    } else if (!air && 1.6f * p->mag / 180.0f < WEAK_PUSH) {
        lm.warn |= LMW_WEAK;
    }
    if (yawStep > 0.02f) {
        lm.warn |= LMW_CAMERA;
    }
    if (air) {
        lm.warn |= LMW_AIR;
    }
    if (s->ballType != 0) {
        lm.warn |= LMW_OTHER_BALL;
    }
    if (settings.ballNormal && s->difficulty != DIFFICULTY_EASY) {
        lm.warn |= LMW_NORMAL;
    }
}

static void LMashFillView(LMashView *v) {
    f32 sum = 0.0f;

    v->kind = lm.kind;
    v->verdict = lm.verdict;
    v->gap = lm.gap;
    v->over = lm.over;
    v->warn = lm.warn;
    v->cells = lm.cells;
    v->metro = lm.metro ? (((lm.tick - lm.metroTick) & 1) == 0 ? 2 : 1) : 0;
    for (int i = 0; i < LM_STRIP; i++) {
        v->strip[i] = lm.strip[i];
    }
    for (int i = 0; i < lm.nMoves; i++) {
        sum += lm.moves[i];
    }
    v->move = lm.move;
    v->peak = lm.peak;
    v->avg20 = lm.nMoves ? sum / lm.nMoves : 0.0f;
    v->target = lm.target;
}

/* ---------------------------------------------------------------------------------------------------------
 * MBZ-L
 */

enum { MS_IDLE, MS_WAIT_AIM, MS_AIMED, MS_CANCELLED, MS_WAIT_L, MS_RESULT, MS_COLLECT };

#define MBZ_AIM_B 6      /* the frame on which the aim shows, and Z cancels it */
#define MBZ_AIM_A 4
#define MBZ_Z_WAIT 4     /* frames in the aim without Z before the verdict */
#define MBZ_L_WAIT 3     /* frames after the best L frame before the verdict */
#define MBZ_COLLECT 7    /* frames after the L until the last speed */
#define MBZ_MAX_WAIT 12  /* frames after the press until the aim must show */

static struct {
    MbzAttempt cur;
    MbzAttempt hist[MBZ_KEEP];
    u8 histCount;
    u8 state;
    s32 start;          /* the tick of frame 0 */
    u8 startBtn;        /* PADB_B or PADB_A */
    u8 startMode;
    u8 aimMode;
    u8 stood;           /* L pressed in JOINED while the start button was held */
    s8 stoodFrame;
    u8 zHeld;           /* Z was down after the L frame */
    u8 zLate;           /* frames between the aim and the cancel */
} mb;

static void MbzReset(void) {
    u8 *p = (u8 *)&mb;

    for (u32 i = 0; i < sizeof(mb); i++) {
        p[i] = 0;
    }
}

static void MbzClearAttempt(MbzAttempt *a) {
    u8 *p = (u8 *)a;

    for (u32 i = 0; i < sizeof(*a); i++) {
        p[i] = 0;
    }
    a->zFrame = a->aimFrame = a->cancelFrame = a->lFrame = a->lTarget = -1;
}

static const char *MbzVerdictText(const MbzAttempt *a, char *buf, int size, u32 *color, int brief);

/* The attempt's line in the trainer log, once its verdict is final and its speeds are in. */
static void MbzPush(void) {
    char buf[40];
    u32 color;
    const char *text = MbzVerdictText(&mb.cur, buf, sizeof(buf), &color, 1);
    char kind = mb.cur.kind == MBZ_B ? 'B' : 'A';

    if (mb.cur.speedFlags & 1) {
        TrainerResult(TRAINER_MBZL, color, "%c %.1f>%.1f %s", kind, (double)mb.cur.v0, (double)mb.cur.speed, text);
    } else {
        TrainerResult(TRAINER_MBZL, color, "%c %s", kind, text);
    }
}

static void MbzStart(int btn, const Snap *s, s32 tick) {
    if (mb.state == MS_COLLECT) {
        MbzPush(); /* a new attempt ends the collecting of the last one's speeds */
    }
    if (mb.cur.kind != MBZ_NONE) {
        for (int i = MBZ_KEEP - 1; i > 0; i--) {
            mb.hist[i] = mb.hist[i - 1];
        }
        mb.hist[0] = mb.cur;
        if (mb.histCount < MBZ_KEEP) {
            mb.histCount++;
        }
    }
    MbzClearAttempt(&mb.cur);
    mb.cur.kind = btn == PADB_B ? MBZ_B : MBZ_A;
    mb.state = MS_WAIT_AIM;
    mb.start = tick;
    mb.startBtn = btn;
    mb.startMode = s->mode;
    mb.aimMode = btn == PADB_B ? HAND_THROWAIM : HAND_PREWHACK;
    mb.stood = mb.zHeld = mb.zLate = 0;
    mb.stoodFrame = -1;
}

/* Drops an attempt that never reached the aim. */
static void MbzDiscard(void) {
    MbzClearAttempt(&mb.cur);
    mb.state = MS_IDLE;
}

static void MbzFinish(u8 verdict, int n, int collect) {
    mb.cur.verdict = verdict;
    mb.cur.n = (s8)n;
    mb.cur.done = 1;
    mb.cur.doneFrame = snap.frame2;
    mb.state = collect ? MS_COLLECT : MS_IDLE;
    if (!collect) {
        MbzPush();
    }
}

/* The speed of a perfect L: (fb + 2/3 fh) * V + a (docs/game.md, "Momentum BZ and MAZ"). The friction of the ball
 * on the ground is the value that MoveBall left; in the air it is the airborne factor. */
static f32 MbzBest(f32 v, f32 mag, const Snap *s) {
    f32 fb, fh, push = 1.6f * mag / 120.0f;
    f32 standAdd = HAND_MASS / (s->ballMass < 10.0f ? 15.0f : s->ballMass * 1.5f);

    if (s->planeUnder) {
        fb = s->ballFriction;
        fh = HAND_FRICTION_PLANE;
    } else {
        fb = 1.0f - (1.0f - s->ballPhysFriction) / 4.0f;
        fh = 1.0f - (1.0f - s->handPhysFriction) / 4.0f;
    }
    return (fb + standAdd * fh) * v + push;
}

static void MbzStep(const Snap *s, const TPad *p, s32 tick) {
    MbzAttempt *a = &mb.cur;
    int f;

    if (mb.state == MS_COLLECT) {
        int since = tick - mb.start - a->lFrame;

        if (since == 4) {
            a->speed3 = s->ballSpeed;
            a->speedFlags |= 2;
        } else if (since == MBZ_COLLECT) {
            a->speed6 = s->ballSpeed;
            a->speedFlags |= 4;
        }
        if (since >= MBZ_COLLECT) {
            mb.state = MS_IDLE;
            MbzPush();
        }
    }
    if (mb.state == MS_IDLE || mb.state == MS_COLLECT) {
        int b = p->pressed[PADB_B] && (s->mode == HAND_JOINED || s->mode == HAND_BOUNCE);
        int aa = p->pressed[PADB_A] && s->mode == HAND_JOINED;

        if (!(b || aa)) {
            return;
        }
        MbzStart(b ? PADB_B : PADB_A, s, tick);
    }

    f = tick - mb.start;
    /* What was pressed, by frame. */
    if (f < 16) {
        if (p->button[mb.startBtn]) {
            a->bMask |= 1 << f;
        }
        if (p->button[PADB_Z]) {
            a->zMask |= 1 << f;
        }
        if (p->pressed[PADB_L]) {
            a->lMask |= 1 << f;
        }
    }
    if (p->button[PADB_Z] && (a->zKind == 0 || (a->zKind == 1 && p->pressed[PADB_Z]))) {
        /* Down before the start (armed), or a press: one that follows a release counts as a fresh one. */
        a->zKind = (f == 0 && !p->pressed[PADB_Z]) ? 1 : 2;
        a->zFrame = a->zKind == 2 ? f : -1;
    }
    if (a->cancelFrame >= 0 && f == a->cancelFrame + 2 && p->button[PADB_Z]) {
        mb.zHeld = 1;
    }

    switch (mb.state) {
        case MS_WAIT_AIM:
            if (s->mode != mb.aimMode) {
                if (s->mode == HAND_RELEASE && f >= 1 && a->zKind == 2) {
                    MbzFinish(MBV_Z_FRESH, a->zFrame, 0);
                } else if (mb.stood && s->mode == HAND_BALLWALK) {
                    MbzFinish(MBV_L_EARLY_STOOD, (mb.startBtn == PADB_B ? MBZ_AIM_B : MBZ_AIM_A) + 1 - mb.stoodFrame,
                              0);
                } else if (mb.startMode == HAND_JOINED && s->mode == HAND_JOINED && p->pressed[PADB_L] &&
                           p->button[mb.startBtn] && f >= 1) {
                    /* A stand press before the aim: Glover stands on the ball next frame. */
                    mb.stood = 1;
                    mb.stoodFrame = (s8)f;
                } else if (!p->button[mb.startBtn] || f > MBZ_MAX_WAIT) {
                    MbzDiscard();
                }
                return;
            }
            a->aimFrame = (s8)f;
            a->v0 = s->ballSpeed;
            a->startAir = !s->onGround;
            mb.state = MS_AIMED;
            /* The aim shows on this frame, and Z may cancel it now. */
            __attribute__((fallthrough));
        case MS_AIMED:
            if (s->mode != mb.aimMode) {
                /* The aim is gone, with Z down or not. */
                MbzFinish(MBV_RELEASED, f - 1, 0);
            } else if (p->button[PADB_Z]) {
                a->cancelFrame = (s8)f;
                a->lTarget = (s8)(f + 1);
                mb.zLate = (u8)(f - a->aimFrame);
                mb.state = MS_CANCELLED;
            } else if (f - a->aimFrame >= MBZ_Z_WAIT) {
                MbzFinish(MBV_NO_Z, 0, 0);
            }
            return;
        case MS_CANCELLED:
            if (s->mode != HAND_JOINED) {
                MbzDiscard();
                return;
            }
            a->best = MbzBest(a->v0, p->mag, s);
            mb.state = MS_WAIT_L;
            /* This frame is the best one for the L. */
            __attribute__((fallthrough));
        case MS_WAIT_L:
            if (p->pressed[PADB_L] && f >= a->lTarget) {
                a->lFrame = (s8)f;
                mb.state = MS_RESULT; /* the speed shows on the next frame */
                return;
            }
            if (f >= a->lTarget + MBZ_L_WAIT) {
                u16 early = a->lMask & ((1 << a->lTarget) - 1);

                if (early != 0) {
                    int first = 0;

                    while (!(early & (1 << first))) {
                        first++;
                    }
                    MbzFinish(MBV_L_EARLY_LOST, a->lTarget - first, 0);
                } else {
                    MbzFinish(MBV_NO_L, 0, 0);
                }
            }
            return;
        default: /* MS_RESULT: the frame after the L */
            a->speed = s->ballSpeed;
            a->speedFlags |= 1;
            if (mb.zLate > 0) {
                MbzFinish(MBV_Z_LATE, mb.zLate, 1);
            } else if (a->lFrame > a->lTarget) {
                MbzFinish(MBV_L_LATE, a->lFrame - a->lTarget, 1);
            } else if (mb.zHeld) {
                MbzFinish(MBV_Z_HELD, 0, 1);
            } else {
                MbzFinish(MBV_PERFECT, 0, 1);
            }
            return;
    }
}

/* ---------------------------------------------------------------------------------------------------------
 * Line keeper
 */

#define LK_LATCH_FRAMES 4      /* frames with the same stick direction before the line is latched */
#define LK_SLOW 3.0f           /* the mover must be faster than this */
#define LK_BAND 0.10f          /* the dead band of the advice, in radians (6 degrees) */
#define LK_LOOK 2.0f
#define LK_SMOOTH 4
#define LK_SHARE_FRAMES 20
#define LK_CAM_KEEP 0.26667f   /* the camera body keeps this part of its speed each frame (camera.c) */
#define LK_TAP_UNITS 21.82f    /* what one frame of C turns the view: 21.82 / D radians (16 / (1 - 0.26667)) */

static struct {
    u8 haveRef, havePrevE;
    f32 ref, prevE;
    f32 hist[LK_SMOOTH];
    u8 nHist, headHist;
    s16 lastSX, lastSY;
    u8 same;               /* frames in a row with the stick where it is */
    u8 prevTap;            /* the C button of the last logic run: 0 none, 1 left, 2 right */
    u8 strip[LK_STRIP];
    u8 cells;
    f32 prevYaw;           /* the camera's yaw at the last run */
    /* The view. */
    u8 live, advice, why, walking;
    f32 err, rate, lead, tap, cross, speed, dist;
    u16 walkFrames;        /* logic runs of the walk that goes on (latched), for the log */
} lk;

static void LineReset(void) {
    u8 *p = (u8 *)&lk;

    for (u32 i = 0; i < sizeof(lk); i++) {
        p[i] = 0;
    }
}

/* Forgets the line, and keeps the strip. */
static void LineDrop(void) {
    lk.haveRef = lk.havePrevE = 0;
    lk.nHist = lk.headHist = 0;
    lk.same = 0;
    lk.live = 0;
    lk.advice = LK_NONE;
}

#define LK_WALK_MIN 20 /* frames of a walk that make a log line */

/* After LineStep: a walk that was latched and is not now has ended. */
static void LineWatch(void) {
    if (lk.live) {
        lk.walkFrames += lk.walkFrames < 0xFFFF;
        return;
    }
    if (lk.walkFrames >= LK_WALK_MIN && lk.why != LKW_JUMPED) {
        f32 off = lk.cross < 0.0f ? -lk.cross : lk.cross;

        TrainerResult(TRAINER_LINE, off < 2.0f ? GREEN : off < 6.0f ? YELLOW : RED, "%dF DRIFT %.1f %s", lk.walkFrames,
                      (double)off, lk.cross < 0.0f ? "LEFT" : "RIGHT");
    }
    lk.walkFrames = 0;
}

static void LineStep(const Snap *s, const TPad *p) {
    int tap = p->button[PADB_CRIGHT] ? 2 : p->button[PADB_CLEFT] ? 1 : 0;
    int tapped = lk.prevTap; /* frame n - 1: its tap is already in camera.vel */
    int walking = !(s->mode == HAND_JOINED || s->mode == HAND_BALLWALK);
    f32 speed = walking ? s->handMove : s->ballMove;
    f32 dx, dz, d, yaw;

    for (int i = 0; i + 1 < LK_STRIP; i++) {
        lk.strip[i] = lk.strip[i + 1];
    }
    lk.strip[LK_STRIP - 1] = (u8)tap;
    if (lk.cells < LK_STRIP) {
        lk.cells++;
    }
    lk.prevTap = (u8)tap;
    lk.walking = (u8)walking;
    lk.speed = speed;

    if (!s->lineOk || s->camMode != 1 || s->camOverride != 0 || s->camDest != 0 || s->camOrbit != 0 ||
        s->camAngleLock != 0 || s->camBlocked != 0 || s->camFall != 0) {
        LineDrop();
        lk.why = LKW_CAMERA;
        return;
    }
    if (speed > MAX_MOVE) {
        LineDrop();
        lk.why = LKW_JUMPED;
        return;
    }
    if (speed <= LK_SLOW || (walking && !s->handOnGround)) {
        LineDrop();
        lk.why = LKW_SLOW;
        return;
    }
    if (p->stickX == 0 || p->stickY == 0) {
        LineDrop();
        lk.why = LKW_NO_STICK;
        return;
    }
    dx = s->camPos[0] - s->camTarget[0];
    dz = s->camPos[1] - s->camTarget[1];
    d = Sqrt(dx * dx + dz * dz);
    if (d < 1.0f) {
        LineDrop();
        lk.why = LKW_CAMERA;
        return;
    }
    yaw = s->camYaw;

    /* The stick direction: a change starts a new line. */
    if (p->stickX == lk.lastSX && p->stickY == lk.lastSY && lk.same > 0) {
        if (lk.same < 255) {
            lk.same++;
        }
    } else {
        lk.haveRef = lk.havePrevE = 0;
        lk.nHist = lk.headHist = 0;
        lk.same = 1;
    }
    lk.lastSX = p->stickX;
    lk.lastSY = p->stickY;
    if (!lk.haveRef) {
        if (lk.same >= LK_LATCH_FRAMES) {
            lk.haveRef = 1;
            lk.ref = yaw;
            lk.cross = 0.0f;
        } else {
            lk.live = 0;
            lk.advice = LK_NONE;
            lk.why = LKW_LATCHING;
            return;
        }
    } else {
        /* The walker heads where the pad points, turned by the camera's yaw of the frame before. Each frame's move
         * shifts it sideways by its length times the sine of the yaw's error. A fall of the yaw is a drift to the right. */
        lk.cross -= speed * __sinf(WrapAngle(lk.prevYaw - lk.ref));
    }
    lk.prevYaw = yaw;

    {
        f32 rx = -__cosf(yaw), rz = __sinf(yaw);
        f32 lat = s->camVel[0] * rx + s->camVel[1] * rz;
        f32 tapSize = LK_TAP_UNITS / d;
        f32 e = WrapAngle(yaw - lk.ref) + lat / (1.0f - LK_CAM_KEEP) / d;
        f32 sum = 0.0f;

        if (lk.havePrevE) {
            f32 jump = tapped == 2 ? tapSize : tapped == 1 ? -tapSize : 0.0f;

            lk.hist[lk.headHist] = e - lk.prevE - jump;
            lk.headHist = (u8)((lk.headHist + 1) % LK_SMOOTH);
            if (lk.nHist < LK_SMOOTH) {
                lk.nHist++;
            }
        }
        lk.prevE = e;
        lk.havePrevE = 1;
        for (int i = 0; i < lk.nHist; i++) {
            sum += lk.hist[i];
        }
        lk.err = e;
        lk.rate = lk.nHist ? sum / lk.nHist : 0.0f;
        lk.lead = e + LK_LOOK * lk.rate;
        lk.tap = tapSize;
        lk.dist = d;
        lk.advice = lk.lead < -LK_BAND ? LK_RIGHT : lk.lead > LK_BAND ? LK_LEFT : LK_NONE;
        lk.live = 1;
        lk.why = LKW_OK;
    }
}

static void LineFillView(LineView *v) {
    int share = 0;
    f32 expect = 0.035f * lk.speed + (lk.walking ? 0.25f : 0.0f);

    v->live = lk.live;
    v->advice = lk.advice;
    v->why = lk.why;
    v->cells = lk.cells;
    v->walking = lk.walking;
    for (int i = 0; i < LK_STRIP; i++) {
        v->strip[i] = lk.strip[i];
    }
    for (int i = LK_STRIP - LK_SHARE_FRAMES; i < LK_STRIP; i++) {
        share += lk.strip[i] != 0;
    }
    v->share = (u8)share;
    v->expect = (u8)(expect > 1.0f ? 100 : (int)(expect * 100.0f + 0.5f));
    v->err = lk.err;
    v->rate = lk.rate;
    v->lead = lk.lead;
    v->tap = lk.tap;
    v->cross = lk.cross;
    v->speed = lk.speed;
    v->dist = lk.dist;
}

/* ---------------------------------------------------------------------------------------------------------
 * Wall clip (MAZ-R with the ball bearing: docs/game.md, "Walls and the ball bearing")
 */

#define CLIP_LO 6.0f            /* the ball bearing's radius: the nearest the centre is to a wall at the start of a frame */
#define CLIP_HI_BASE 2.99f      /* the window's top is a * speed + this: a is 2.95 with the glove in the air, 2.6 on the ground */
#define CLIP_SLOPE_AIR 2.95f    /* (the model in tools/tricks/bearing.py; it is the model's edge to 0.05 units for speeds 3.2 to 14) */
#define CLIP_SLOPE_GROUND 2.6f
#define CLIP_MIN_AIR 3.2f       /* no clip below this speed (the model: none at 3.1) */
#define CLIP_MIN_GROUND 5.5f    /* none at 5.0 */
#define CLIP_SQUARE 0.866f      /* a wall turned less than 30 degrees from the ball's direction */
#define CLIP_WATCH 8            /* game frames after the cancel in which the glove going through the wall is looked for */
#define CLIP_SHOW 90            /* game frames that the cancel's numbers stay */
#define CLIP_THROUGH 0.5f       /* the glove's centre this far behind the plane is a clip */

static struct {
    u8 hasResult;
    u8 verdict;
    u8 outcome;
    s8 rAt;
    u16 age;
    f32 dist, speed, hi;
    f32 n[3], pt[3];
    f32 behind;
    u32 counts;
    u8 live;                /* the live part below holds a wall */
    u8 why, liveVerdict, ground, angled;
    f32 lDist, lSpeed, lLo, lHi, lMin;
} cl;

static void ClipReset(void) {
    u8 *p = (u8 *)&cl;

    for (u32 i = 0; i < sizeof(cl); i++) {
        p[i] = 0;
    }
}

static void ClipStep(const Snap *s, const TPad *p) {
    int holding = s->mode == HAND_JOINED || s->mode == HAND_PREWHACK;
    static WallHit wall;

    if (cl.hasResult) {
        /* What came of the last cancel: the R, and whether the glove's centre crossed the wall's plane. */
        if (cl.age < 0xFFFF) {
            cl.age++;
        }
        if (cl.rAt < 0 && p->pressed[PADB_R] && cl.age <= CLIP_WATCH) {
            cl.rAt = (s8)cl.age;
        }
        if (cl.age <= CLIP_WATCH && cl.outcome == CO_PENDING) {
            f32 sd = 0.0f;

            for (int k = 0; k < 3; k++) {
                sd += cl.n[k] * (s->handPos[k] - cl.pt[k]);
            }
            if (sd < cl.behind) {
                cl.behind = sd;
            }
            if (sd < -CLIP_THROUGH) {
                cl.outcome = CO_CLIPPED;
            } else if (cl.age == CLIP_WATCH) {
                cl.outcome = CO_NO_CLIP;
            }
        }
        if (cl.age == CLIP_WATCH) {
            static const char *const names[] = { "IN", "FAR", "NEAR", "SLOW" };
            char rText[8];

            if (cl.rAt < 0) {
                Format(rText, sizeof(rText), "NO R");
            } else {
                Format(rText, sizeof(rText), "R+%d", cl.rAt);
            }
            TrainerResult(TRAINER_CLIP, cl.outcome == CO_CLIPPED ? GREEN : RED, "%s %.1f %s %s", names[cl.verdict & 3],
                          (double)cl.dist, rText, cl.outcome == CO_CLIPPED ? "CLIPPED" : "NO CLIP");
        }
        if (cl.age > CLIP_SHOW) {
            cl.hasResult = 0;
        }
    }
    cl.live = 0;
    if (!holding) {
        cl.why = CW_NOT_HOLDING;
        return;
    }
    if (s->ballType != BALL_MODE_BEARING) {
        cl.why = CW_NOT_BEARING;
        return;
    }
    if (!MoonWallScan(s->ballPos, s->ballVel, &wall)) {
        cl.counts = wall.counts;
        cl.why = CW_NO_WALL;
        return;
    }
    cl.counts = wall.counts;
    cl.why = CW_OK;
    cl.live = 1;
    cl.ground = s->handPlane;
    cl.angled = wall.cosA < CLIP_SQUARE;
    cl.lDist = wall.dist;
    cl.lSpeed = wall.speedN;
    cl.lLo = CLIP_LO;
    cl.lMin = cl.ground ? CLIP_MIN_GROUND : CLIP_MIN_AIR;
    cl.lHi = (cl.ground ? CLIP_SLOPE_GROUND : CLIP_SLOPE_AIR) * wall.speedN + CLIP_HI_BASE;
    if (wall.speedN < cl.lMin) {
        cl.liveVerdict = CV_SLOW;
    } else if (wall.dist < cl.lLo - 0.05f) {
        cl.liveVerdict = CV_NEAR;
    } else if (wall.dist > cl.lHi) {
        cl.liveVerdict = CV_FAR;
    } else {
        cl.liveVerdict = CV_IN;
    }
    if (s->mode == HAND_PREWHACK && p->button[PADB_Z]) {
        /* The cancel: this frame starts from the state that was read. */
        cl.hasResult = 1;
        cl.verdict = cl.liveVerdict;
        cl.dist = cl.lDist;
        cl.speed = cl.lSpeed;
        cl.hi = cl.lHi;
        cl.outcome = CO_PENDING;
        cl.rAt = -1;
        cl.age = 0;
        cl.behind = 0.0f;
        for (int k = 0; k < 3; k++) {
            cl.n[k] = wall.normal[k];
            cl.pt[k] = wall.point[k];
        }
    }
}

static void ClipFillView(ClipView *v) {
    v->why = cl.why;
    v->verdict = cl.liveVerdict;
    v->ground = cl.ground;
    v->angled = cl.angled;
    v->hasResult = cl.hasResult;
    v->rVerdict = cl.verdict;
    v->outcome = cl.outcome;
    v->rAt = cl.rAt;
    v->age = cl.age;
    v->dist = cl.lDist;
    v->speed = cl.lSpeed;
    v->lo = cl.lLo;
    v->hi = cl.lHi;
    v->minSpeed = cl.lMin;
    v->rDist = cl.dist;
    v->rSpeed = cl.speed;
    v->rHi = cl.hi;
    v->behind = cl.behind < 0.0f ? -cl.behind : 0.0f;
    v->counts = cl.counts;
}

/* ---------------------------------------------------------------------------------------------------------
 * The hook, the logic and the view
 */

static MoonView moonView;
static MoonView moonOld, moonOlder; /* the views of the last two game frames before moonView's */

static void BuildView(int on, int valid) {
    u32 seq = trainerViewSeq;
    TrainerView *v = &trainerViews[(seq + 1) & 1];
    int n = 0;

    /* The draw thread may run at any point. It copies the view that Seq names and then checks that Begin has not
     * reached the next publish after that one, which writes the same buffer. */
    trainerViewBegin = seq + 1;
    BARRIER();

    v->frame2 = snap.frame2;
    v->on = (u8)on;
    v->valid = (u8)valid;
    if (valid) {
        LMashFillView(&v->lmash);
        LineFillView(&v->line);
        ClipFillView(&v->clip);
        v->moon = moonView;
        if (mb.cur.kind != MBZ_NONE) {
            v->mbz[n++] = mb.cur;
        }
        for (int i = 0; i < mb.histCount && n < MBZ_KEEP; i++) {
            v->mbz[n++] = mb.hist[i];
        }
    }
    v->mbzCount = (u8)n;
    BossFillView(&v->boss);
    ApplyHold(v);
    BARRIER();
    trainerViewSeq = seq + 1;
}

/* Clears the state of the trainers, or of some of them, and the view. */
static void ResetBits(int bits) {
    if (bits & (TRAINER_LMASH | TRAINER_METRO)) {
        LMashReset();
    }
    if (bits & TRAINER_LINE) {
        LineReset();
    }
    if (bits & TRAINER_MOON) {
        MoonAimReset();
        u8 *mv = (u8 *)&moonView;

        for (u32 i = 0; i < sizeof(moonView); i++) {
            mv[i] = 0;
        }
        moonOld.live = moonOlder.live = 0;
    }
    if (bits & TRAINER_CLIP) {
        ClipReset();
    }
    if (bits & TRAINER_MBZL) {
        MbzReset();
        MbzClearAttempt(&mb.cur);
        mb.cur.kind = MBZ_NONE;
    }
    if (bits & TRAINER_BOSS) {
        BossReset();
        bossDirty = 0;
    }
}

/* Ends the holds of the trainers in BITS. */
static void ClearHolds(int bits) {
    for (int i = 0; i < 7; i++) {
        if ((bits >> i) & 1) {
            holdUntil[i] = 0;
        }
    }
    ended &= (u8)~bits;
}

static void ResetAll(void) {
    ResetBits(0xFF);
    ClearHolds(0xFF);
    dirty = 0;
    BuildView(run.trainers, 0);
}

static u16 PadMask(const u8 *flags) {
    u16 bits = 0;

    for (int b = 0; b < 14; b++) {
        bits |= (flags[b] != 0) << b;
    }
    return bits;
}

/* The pad as the hook saw it first and last in the game frame, for the test of trainerDiag. */
static u16 firstButtons, firstPressed, lastButtons, lastPressed;
static s32 firstFrame2 = -1;

void TrainersGameFrame(void) {
    Snap *s = &snap;
    const Pad *src;
    u16 buttons, pressed;

    if (!run.trainers) {
        if (dirty || bossDirty) {
            /* The last trainer was turned off: draw nothing. */
            ResetAll();
        }
        return;
    }
    trainerDiag.hookCalls++;
    src = &pad[ActiveController];
    buttons = PadMask(src->button);
    pressed = PadMask(src->pressed);
    if (firstFrame2 != frameCount2) {
        firstFrame2 = frameCount2;
        firstButtons = buttons;
        firstPressed = pressed;
    }
    lastButtons = buttons;
    lastPressed = pressed;

    if (clearRequest) {
        int bits = clearRequest;

        ResetBits(bits);
        ClearHolds(bits);
        LogDrop(bits);
        clearRequest = 0;
        if (bits & TRAINER_BOSS) {
            BuildView(run.trainers, dirty);
        }
    }
    if (gameInfo.level != logLevel) {
        /* The log survives a reset and a savestate load, which are how a trick is tried again. */
        logLevel = gameInfo.level;
        LogDrop(0xFF);
    }
    if (seenStarts != practice.levelStarts || seenLoads != stateLoads) {
        seenStarts = practice.levelStarts;
        seenLoads = stateLoads;
        ResetAll();
    }
    s->frame2 = frameCount2;
    s->fresh = 0;
    bossFresh = 0;
    /* The boss trainer has its own conditions: the Space fight keeps the glove still, and Quiet would stop it. */
    if (run.trainers & TRAINER_BOSS) {
        if (BossRead()) {
            bossFresh = 1;
        } else if (bossDirty) {
            BossReset();
            bossDirty = 0;
            BuildView(run.trainers, dirty);
        }
    }
    s->quiet = !((run.trainers & TRAINER_TRICKS) && Quiet() && ReadSnap(s));
    if (s->quiet) {
        if (dirty) {
            ResetBits(TRAINER_TRICKS);
            ClearHolds(TRAINER_TRICKS);
            dirty = 0;
            BuildView(run.trainers, 0);
        }
        return;
    }
    s->fresh = 1;
}

/* One game frame of the trainers, with the state of the hook and the pad of this frame. */
void TrainersProcessInput(void) {
    Snap *s = &snap;
    TPad p;
    static s32 tick;
    int tricks = s->fresh, boss = bossFresh;

    if (!(tricks || boss) || s->frame2 != frameCount2) {
        trainerDiag.skipped++;
        return;
    }
    s->fresh = 0;
    bossFresh = 0;
    if (boss) {
        BossStep();
        bossDirty = 1;
    }
    if (!tricks) {
        BuildView(run.trainers, dirty);
        return;
    }
    trainerDiag.frames++;
    {
        u16 buttons = PadMask(pad[ActiveController].button);
        u16 pressed = PadMask(pad[ActiveController].pressed);

        trainerDiag.firstDiff += firstButtons != buttons;
        trainerDiag.lastDiff += lastButtons != buttons;
        trainerDiag.firstPressedDiff += firstPressed != pressed;
        trainerDiag.lastPressedDiff += lastPressed != pressed;
    }
    trainerDiag.steps++;
    ReadPad(&p);
    tick++;
    if (run.trainers & (TRAINER_LMASH | TRAINER_METRO)) {
        LMashStep(s, &p);
    }
    if (run.trainers & TRAINER_LINE) {
        LineStep(s, &p);
        LineWatch();
    }
    if (run.trainers & TRAINER_MBZL) {
        MbzStep(s, &p, tick);
    }
    if ((run.trainers & TRAINER_CLIP) && s->clipOk) {
        ClipStep(s, &p);
    }
    if (run.trainers & TRAINER_MOON) {
        if (s->moonOk) {
            s->moon.aHeld = p.button[PADB_A];
            moonOlder = moonOld;
            moonOld = moonView;
            MoonAimStep(&s->moon, &moonView);
            /* The glove has just slammed: the Z that did it was read in the last game frame, which is cell 0 of the
             * view of the game frame before that. */
            if (s->mode == HAND_SLAM && moonOld.live && moonOlder.live && !moonView.live) {
                static const char *const names[] = { "NOT CHECKED", "LAND", "CLIP", "NO Z", "VOID" };
                u8 c = moonOlder.cell[0];

                held.moon = moonOlder;
                if (c == MC_LAND && moonOlder.hasTarget) {
                    TrainerResult(TRAINER_MOON, GREEN, "LAND %.1f FROM A HOLE", (double)moonOlder.dist);
                } else {
                    TrainerResult(TRAINER_MOON, c == MC_CLIP ? RED : c == MC_LAND ? GREEN : DIM, "%s", names[c < 5 ? c : 0]);
                }
            }
        } else {
            ResetBits(TRAINER_MOON);
        }
    }
    dirty = 1;
    BuildView(run.trainers, 1);
}

void TrainersToggle(int bit) {
    run.trainers ^= (u8)bit;
    clearRequest |= (u8)bit;
}

void TrainersText(char *buf, int size, int bit) {
    Format(buf, size, "%s", (run.trainers & bit) ? "on" : "off");
}

/* ---------------------------------------------------------------------------------------------------------
 * Drawing
 */


#define LM_X 8
#define LM_Y 116
#define MB_X 156
#define MB_Y 92
#define LINE 10

static void Line(Gfx **dl, int x, int y, u32 color, const char *fmt, ...) {
    char buf[48];
    va_list args;

    va_start(args, fmt);
    FormatV(buf, sizeof(buf), fmt, args);
    va_end(args);
    /* Text that would run off the screen is cut. */
    for (int n = 0; buf[n] != 0; n++) {
        if (x + (n + 1) * TEXT_ADVANCE > 320) {
            buf[n] = 0;
            break;
        }
    }
    TextPrint(dl, x, y, 1, color, buf);
}

static void DrawLMash(Gfx **dl, const LMashView *v) {
    int y = LM_Y;
    static const char *const warnNames[] = { "PAD TOO WEAK", "CAMERA TURNING", "PAD RELEASED", "IN AIR",
                                             "OTHER BALL", "NORMAL CONTROLS" };

    if (v->kind == LM_NONE) {
        Line(dl, LM_X, y, DIM, "L MASH");
    } else {
        const char *name = v->kind == LM_STAND ? "STAND" : "HOLD";

        switch (v->verdict) {
            case LMV_ON_TIME:
                Line(dl, LM_X, y, GREEN, "%s +%d ON TIME", name, v->gap);
                break;
            case LMV_LATE:
                Line(dl, LM_X, y, RED, "%s +%d LATE %d", name, v->gap, v->over);
                break;
            case LMV_SLOW:
                Line(dl, LM_X, y, v->over <= 3 ? YELLOW : RED, "%s +%d SLOW %d", name, v->gap, v->over);
                break;
            case LMV_REST:
                Line(dl, LM_X, y, DIM, "%s +%d REST", name, v->gap);
                break;
            case LMV_EARLY:
                Line(dl, LM_X, y, RED, "%s +%d EARLY", name, v->gap);
                break;
            default:
                Line(dl, LM_X, y, WHITE, "%s", name);
                break;
        }
    }
    y += LINE;
    /* The last 24 game frames, the glove holding (J) or standing on the ball (B); a bar under the press frames. */
    for (int i = 0; i < LM_STRIP; i++) {
        u8 c = v->strip[i];
        int x = LM_X + i * TEXT_ADVANCE;
        char text[2] = { '.', 0 };
        u32 color = DIM;

        if ((c & 3) == 1) {
            text[0] = 'J';
            color = BLUE;
        } else if ((c & 3) == 2) {
            text[0] = 'B';
            color = ORANGE;
        }
        if (c & 0x80) {
            color = WHITE;
            TextRect(dl, x, y + 9, x + 5, y + 11, WHITE);
        }
        TextPrint(dl, x, y, 1, color, text);
    }
    y += LINE + 3;
    Line(dl, LM_X, y, WHITE, "MOVE %.1f  PEAK %.1f", (double)v->move, (double)v->peak);
    y += LINE;
    if (v->target > 0.0f) {
        Line(dl, LM_X, y, WHITE, "AVG20 %.1f  TARGET %.1f", (double)v->avg20, (double)v->target);
    } else {
        Line(dl, LM_X, y, WHITE, "AVG20 %.1f", (double)v->avg20);
    }
    y += LINE;
    for (int w = 0; w < 6; w++) {
        if (v->warn & (1 << w)) {
            Line(dl, LM_X, y, RED, "%s", warnNames[w]);
            y += LINE;
        }
    }
}

/* The panel starts at x 156 and holds 27 characters. BRIEF gives the shorter texts that a list row has room for. */
static const char *MbzVerdictText(const MbzAttempt *a, char *buf, int size, u32 *color, int brief) {
    *color = RED;
    switch (a->verdict) {
        case MBV_PERFECT:
            *color = GREEN;
            return "PERFECT";
        case MBV_L_EARLY_LOST:
            Format(buf, size, "L EARLY %d: LOST", a->n);
            return buf;
        case MBV_L_EARLY_STOOD:
            Format(buf, size, brief ? "L EARLY %d: STOOD" : "L EARLY %d: STOOD ON BALL", a->n);
            return buf;
        case MBV_L_LATE:
            *color = YELLOW;
            Format(buf, size, "L LATE %d: -%.1f", a->n, (double)(a->best - a->speed));
            return buf;
        case MBV_NO_L:
            return "NO L";
        case MBV_Z_LATE:
            Format(buf, size, brief ? "Z LATE %d" : "Z TOO LATE %d: SPEED LOST", a->n);
            return buf;
        case MBV_Z_FRESH:
            Format(buf, size, brief ? "Z FRESH %d" : "Z FRESH ON %d: BALL DROPPED", a->n);
            return buf;
        case MBV_NO_Z:
            return brief ? "NO Z" : "NO Z: AIM STAYS";
        case MBV_Z_HELD:
            return brief ? "Z HELD" : "Z HELD ON BALL: DISMOUNT";
        case MBV_RELEASED:
            Format(buf, size, "%c RELEASED AT %d", a->kind == MBZ_B ? 'B' : 'A', a->n);
            return buf;
        default:
            *color = DIM;
            return "";
    }
}

/* The frames 0 to MBZ_TIMELINE-1 of an attempt in three rows (the start button, Z, L), with the cells where Z and
 * L belong lit. */
static void DrawTimeline(Gfx **dl, int x, int y, const MbzAttempt *a) {
    static const char names[3] = { 'B', 'Z', 'L' };
    int aim = a->kind == MBZ_B ? MBZ_AIM_B : MBZ_AIM_A;
    int cancel = a->cancelFrame >= 0 ? a->cancelFrame : aim;
    int target = a->lTarget >= 0 ? a->lTarget : cancel + 1;

    for (int row = 0; row < 3; row++) {
        char text[2] = { names[row], 0 };
        u16 mask = row == 0 ? a->bMask : row == 1 ? a->zMask : a->lMask;

        if (row == 0 && a->kind == MBZ_A) {
            text[0] = 'A';
        }
        TextPrint(dl, x, y + row * 8 - 1, 1, LABEL, text);
        for (int c = 0; c < MBZ_TIMELINE; c++) {
            int cx = x + 10 + c * 12;
            u32 back = 0x30384080;

            if ((row == 1 && c == cancel) || (row == 2 && c == target)) {
                back = 0x40A04090;
            }
            TextRect(dl, cx, y + row * 8, cx + 11, y + row * 8 + 7, back);
            if (mask & (1 << c)) {
                TextRect(dl, cx + 2, y + row * 8 + 2, cx + 9, y + row * 8 + 6, row == 0 ? ORANGE : row == 1 ? YELLOW : WHITE);
            }
        }
    }
}

/* A frame number, or "-" when the frame did not happen. */
static void FrameText(char *buf, int size, int frame) {
    if (frame < 0) {
        Format(buf, size, "-");
    } else {
        Format(buf, size, "%d", frame);
    }
}

static void DrawMbzDetail(Gfx **dl, int x, int y, const MbzAttempt *a) {
    char buf[40];
    char aim[4], z[4], l[4], best[4];
    u32 color;
    const char *text;

    FrameText(aim, sizeof(aim), a->aimFrame);
    FrameText(z, sizeof(z), a->zFrame);
    FrameText(l, sizeof(l), a->lFrame);
    FrameText(best, sizeof(best), a->lTarget);
    Line(dl, x, y, LABEL, "MBZ-L %c  %s", a->kind == MBZ_B ? 'B' : 'A',
         a->zKind == 1 ? "Z ARMED" : a->zKind == 2 ? "Z FRESH" : "Z -");
    y += LINE;
    if (a->zKind == 2) {
        Line(dl, x, y, WHITE, "Z %s  AIM %s", z, aim);
    } else {
        Line(dl, x, y, WHITE, "AIM %s", aim);
    }
    if (a->aimFrame >= 0) {
        Line(dl, x + (a->zKind == 2 ? 78 : 42), y, WHITE, "L %s  BEST %s", l, best);
    }
    y += LINE;
    if (a->aimFrame >= 0) {
        if (a->speedFlags & 1) {
            Line(dl, x, y, WHITE, "SPEED %.1f>%.1f GAIN %+.1f", (double)a->v0, (double)a->speed,
                 (double)(a->speed - a->v0));
        } else {
            Line(dl, x, y, WHITE, "SPEED %.1f", (double)a->v0);
        }
        y += LINE;
        if (a->best > 0.0f) {
            Line(dl, x, y, DIM, "BEST %.1f", (double)a->best);
            if (a->speedFlags & 2) {
                Line(dl, x + 60, y, DIM, "+3 %.1f", (double)a->speed3);
            }
            if (a->speedFlags & 4) {
                Line(dl, x + 108, y, DIM, "+6 %.1f", (double)a->speed6);
            }
            y += LINE;
        }
    }
    text = MbzVerdictText(a, buf, sizeof(buf), &color, 0);
    if (a->done) {
        Line(dl, x, y, color, "%s", text);
    } else {
        Line(dl, x, y, DIM, "...");
    }
    y += LINE;
    DrawTimeline(dl, x, y, a);
}

#define MBZ_SHOW_FRAMES 60 /* game frames the newest result stays in full */

static void DrawMbz(Gfx **dl, const TrainerView *v) {
    int y = MB_Y;
    int shown = 0;

    if (v->mbzCount == 0) {
        Line(dl, MB_X, y, DIM, "MBZ-L");
        return;
    }
    if (!v->mbz[0].done || (u32)v->frame2 - v->mbz[0].doneFrame < MBZ_SHOW_FRAMES) {
        DrawMbzDetail(dl, MB_X, y, &v->mbz[0]);
        y += 7 * LINE + 8;
        shown = 1;
    } else {
        Line(dl, MB_X, y, DIM, "MBZ-L");
        y += LINE;
    }
    for (int i = shown; i < v->mbzCount; i++) {
        const MbzAttempt *a = &v->mbz[i];
        char buf[40];
        u32 color;
        const char *text = MbzVerdictText(a, buf, sizeof(buf), &color, 1);

        if (a->speedFlags & 1) {
            Line(dl, MB_X, y, color, "%c %.1f>%.1f %s", a->kind == MBZ_B ? 'B' : 'A', (double)a->v0, (double)a->speed, text);
        } else {
            Line(dl, MB_X, y, color, "%c %s", a->kind == MBZ_B ? 'B' : 'A', text);
        }
        y += LINE;
    }
}

#define LK_X 8
#define LK_Y 48
#define LK_BAR_W 120
#define LK_RANGE 0.349f /* the bar's half width in radians (20 degrees) */

static void DrawLine(Gfx **dl, const LineView *v) {
    static const char *const whyText[] = { "", "CAMERA MOVES", "TOO SLOW", "NO DIAGONAL", "LATCHING", "GLOVE JUMPED" };
    int x = LK_X, y = LK_Y;

    if (!v->live) {
        Line(dl, x, y, DIM, "LINE KEEPER %s", v->why < 6 ? whyText[v->why] : "");
        return;
    }
    Line(dl, x, y, LABEL, "LINE KEEPER");
    if (v->advice == LK_RIGHT) {
        TextPrint(dl, x + 72, y - 3, 2, GREEN, "C-RIGHT");
    } else if (v->advice == LK_LEFT) {
        TextPrint(dl, x + 72, y - 3, 2, YELLOW, "C-LEFT");
    }
    y += 14;
    /* The bar: plus or minus 20 degrees, the dead band shaded, the marker at E. A turn to the right is a negative E. */
    {
        int mid = x + LK_BAR_W / 2;
        int band = (int)(LK_BAND / LK_RANGE * (LK_BAR_W / 2));
        f32 e = v->err;
        int m;

        TextRect(dl, x, y, x + LK_BAR_W + 1, y + 7, 0x30384090);
        TextRect(dl, mid - band, y, mid + band + 1, y + 7, 0x30803090);
        e = e > LK_RANGE ? LK_RANGE : e < -LK_RANGE ? -LK_RANGE : e;
        m = mid - (int)(e / LK_RANGE * (LK_BAR_W / 2));
        TextRect(dl, m - 1, y - 2, m + 2, y + 9, v->advice == LK_NONE ? GREEN : v->advice == LK_RIGHT ? ORANGE : YELLOW);
        Line(dl, x + LK_BAR_W + 6, y - 1, DIM, "%+.1f", (double)(v->err * 57.29578f));
    }
    y += 12;
    /* The last 40 game frames: a tick for each frame with C-left (above) or C-right (below) down. */
    for (int i = 0; i < LK_STRIP; i++) {
        int cx = x + i * 3;

        TextRect(dl, cx, y + 4, cx + 2, y + 5, 0x40485090);
        if (v->strip[i] == 1) {
            TextRect(dl, cx, y, cx + 2, y + 4, YELLOW);
        } else if (v->strip[i] == 2) {
            TextRect(dl, cx, y + 5, cx + 2, y + 9, ORANGE);
        }
    }
    y += 12;
    Line(dl, x, y, WHITE, "C SHARE %d/%d  EXP %d", v->share, 20, (int)v->expect * 20 / 100);
    y += LINE;
    Line(dl, x, y, WHITE, "OFF %.1f %s  TAP %.2f", (double)(v->cross < 0.0f ? -v->cross : v->cross),
         v->cross < 0.0f ? "L" : "R", (double)v->tap);
}

/* The metronome: a box that lights up on every 2nd game frame. */
#define METRO_X 8
#define METRO_Y 94
static void DrawMetronome(Gfx **dl, int state) {
    if (state) {
        TextRect(dl, METRO_X, METRO_Y, METRO_X + 48, METRO_Y + 16, state == 2 ? 0xFFFFFFFF : 0x303848FF);
    }
}

/* The wall clip: the numbers of a cancel on this frame, the bar of the window, and the last cancel. */
#define CL_X 156
#define CL_Y 150
#define CL_BAR_W 120

static void DrawClip(Gfx **dl, const ClipView *v) {
    int y = CL_Y;
    static const char *const verdictText[] = { "IN", "TOO FAR", "TOO CLOSE", "TOO SLOW" };

    if (v->why == CW_OK) {
        u32 color = v->verdict == CV_IN ? GREEN : RED;
        f32 span = v->hi * 1.25f > 50.0f ? v->hi * 1.25f : 50.0f;
        f32 k = (f32)CL_BAR_W / span;
        int d = (int)(v->dist * k);
        int x0 = CL_X + (int)(v->lo * k), x1 = CL_X + (int)(v->hi * k);

        Line(dl, CL_X, y, WHITE, "WALL %.1f  SPEED %.1f", (double)v->dist, (double)v->speed);
        y += LINE;
        Line(dl, CL_X, y, color, "WINDOW %.1f TO %.1f", (double)v->lo, (double)v->hi);
        y += LINE;
        TextRect(dl, CL_X, y + 3, CL_X + CL_BAR_W, y + 5, 0x404858FF);
        if (v->verdict != CV_SLOW) {
            TextRect(dl, x0, y + 1, x1 > x0 ? x1 : x0 + 1, y + 7, 0x30A030FF);
        }
        if (d > CL_BAR_W) {
            d = CL_BAR_W;
        }
        TextRect(dl, CL_X + d - 1, y - 1, CL_X + d + 1, y + 9, WHITE);
        y += 12;
        switch (v->verdict) {
            case CV_IN:
                Line(dl, CL_X, y, GREEN, "Z NOW: IN (%.1f TO SPARE)",
                     (double)((v->dist - v->lo) < (v->hi - v->dist) ? v->dist - v->lo : v->hi - v->dist));
                break;
            case CV_FAR:
                Line(dl, CL_X, y, RED, "Z NOW: TOO FAR BY %.1f", (double)(v->dist - v->hi));
                break;
            case CV_NEAR:
                Line(dl, CL_X, y, RED, "Z NOW: TOO CLOSE BY %.1f", (double)(v->lo - v->dist));
                break;
            default:
                Line(dl, CL_X, y, RED, "Z NOW: %s, NEEDS %.1f", verdictText[CV_SLOW], (double)v->minSpeed);
                break;
        }
        y += LINE;
        if (v->angled) {
            Line(dl, CL_X, y, YELLOW, "WALL AT AN ANGLE: A GUESS");
            y += LINE;
        }
    } else if (v->why == CW_NOT_BEARING) {
        Line(dl, CL_X, y, DIM, "WALL CLIP: BALL BEARING ONLY");
        y += LINE;
    } else if (v->why == CW_NO_WALL) {
        Line(dl, CL_X, y, DIM, "WALL CLIP: NO WALL AHEAD");
        y += LINE;
    }
    if (v->hasResult) {
        y += 2;
        Line(dl, CL_X, y, v->rVerdict == CV_IN ? GREEN : RED, "CANCEL AT %.1f: %s", (double)v->rDist,
             verdictText[v->rVerdict % 4]);
        y += LINE;
        if (v->rAt < 0) {
            Line(dl, CL_X, y, DIM, "R: NOT YET");
        } else {
            Line(dl, CL_X, y, v->rAt == 1 ? GREEN : YELLOW, "R +%d%s", v->rAt, v->rAt == 1 ? " ON TIME" : "");
        }
        y += LINE;
        if (v->outcome == CO_CLIPPED) {
            Line(dl, CL_X, y, GREEN, "CLIPPED (%.1f BEHIND)", (double)v->behind);
        } else if (v->outcome == CO_NO_CLIP) {
            Line(dl, CL_X, y, RED, "NO CLIP");
        }
    }
}

/* The trainer log: at the right under the game's counters, and at the left in the boss fights, where the boss panel
 * has the right. With the MBZ-L panel on, it stops at 5 lines before that panel (y 92). */
#define TLOG_X 158
#define TLOG_Y 48
#define TLOG_PITCH 9

static void DrawLog(Gfx **dl, int on, int bossLive) {
    static TrainerLog local;
    static const u8 lengths[] = { 0, 0, 5, 6, 8, 10 };
    int x = TLOG_X, y = TLOG_Y;
    int max = settings.trainerLog < ARRAY_COUNT(lengths) ? lengths[settings.trainerLog] : 0;
    int stable = 0;
    int shown = 0;

    if (max == 0) {
        return;
    }
    for (int tries = 0; tries < 4 && !stable; tries++) {
        u32 seq = trainerLog.seq;

        BARRIER();
        local = trainerLog;
        BARRIER();
        stable = trainerLog.begin == seq && local.seq == seq;
    }
    if (!stable) {
        return;
    }
    if (bossLive) {
        x = 8;
        y = 28;
    } else if ((on & TRAINER_MBZL) && max > 5) {
        max = 5;
    }
    for (int i = 0; i < TLOG_MAX && shown < max; i++) {
        const TrainerLogLine *l = &local.line[i];

        if (!(l->trainer & on)) {
            continue;
        }
        Line(dl, x, y, l->color, "%s", l->text);
        y += TLOG_PITCH;
        shown++;
    }
}

void TrainersDraw(Gfx **dl, Gfx *end) {
    static TrainerView local;
    int stable = 0;

    (void)end;
    /* A copy is used only if the game thread did not write that buffer meanwhile (see BuildView). */
    for (int tries = 0; tries < 4 && !stable; tries++) {
        u32 seq = trainerViewSeq;

        BARRIER();
        local = trainerViews[seq & 1];
        BARRIER();
        stable = trainerViewBegin - seq <= 1;
    }
    if (!stable) {
        return;
    }
    if ((local.on & TRAINER_BOSS) && local.boss.live) {
        BossDraw(dl, &local.boss);
    }
    DrawLog(dl, local.on, (local.on & TRAINER_BOSS) && local.boss.live);
    if (!local.valid) {
        return;
    }
    if (local.on & TRAINER_LMASH) {
        DrawLMash(dl, &local.lmash);
    }
    if (local.on & TRAINER_METRO) {
        DrawMetronome(dl, local.lmash.metro);
    }
    if (local.on & TRAINER_LINE) {
        DrawLine(dl, &local.line);
    }
    if (local.on & TRAINER_MBZL) {
        DrawMbz(dl, &local);
    }
    if (local.on & TRAINER_CLIP) {
        DrawClip(dl, &local.clip);
    }
    if (local.on & TRAINER_MOON) {
        MoonAimDraw(dl, &local.moon);
    }
}
