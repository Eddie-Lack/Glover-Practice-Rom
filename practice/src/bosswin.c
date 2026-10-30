/* The boss window trainer (bosswin.h). Reads the Atlantis boss, the Prehistoric boss and the last Space fight, and draws
 * when a hit counts. docs/game.md ("The bosses") lists the windows and where they come from. */
#include "actions.h"
#include "bosswin.h"
#include "fmt.h"
#include "game.h"
#include "safe.h"
#include "text.h"
#include "trainers.h"

#define LEVEL_ATLANTISBOSS 13
#define LEVEL_PREHISTORICBOSS 28

#define TYPE_WHALE 35
#define TYPE_BIG_RAPTOR 34
#define TYPE_EVIL_ROBOT 39

#define TOUCHED 0x04000000 /* an actor's flag: the ball or a shot touched it (collision.c) */

#define WHALE_GAP 49    /* passes from a hit to the next one that counts */
#define PREHIST_GAP 34
#define SPACE_GAP 20
#define CRAB_FLIPPED 200 /* brain.scratch jumps to 250 when the crab is flipped */
#define CRAB_KILL_MIN 31 /* the crab dies to a second hit while scratch is above this after the pass's decrement */
#define FISH_STUN 100

/* ---------------------------------------------------------------------------------------------------------
 * The game's data that the trainer reads
 */

/* enemies[] (actor.h, Enemy, 0x300 bytes): the actor, then the NME brain from 0xE4. Only the brain's fields that are
 * read are named. */
typedef struct BossEnemy {
    /* 0x000 */ Actor actor;
    /* 0x0E4 */ u8 brainTop[0x164];
    /* 0x248 */ s16 cooldownTimer;
    /* 0x24A */ u8 pad24A[0x26C - 0x24A];
    /* 0x26C */ s32 scratch;
    /* 0x270 */ f32 scratchF;
    /* 0x274 */ f32 tiltAngle;
    /* 0x278 */ s16 delayTimer;
    /* 0x27A */ s16 instrTimer;
    /* 0x27C */ s32 flags;
    /* 0x280 */ u8 rest[0x300 - 0x280];
} BossEnemy;

_Static_assert(sizeof(BossEnemy) == 0x300 && __builtin_offsetof(BossEnemy, scratch) == 0x26C &&
                   __builtin_offsetof(BossEnemy, delayTimer) == 0x278 && __builtin_offsetof(BossEnemy, flags) == 0x27C,
               "BossEnemy has the game's layout");

extern BossEnemy enemies[];
extern u8 portionHealth[3];
extern u8 immuneCount[3];
extern s32 spaceBossReload;
extern u8 bulletList[]; /* a circular list: the first word of a bullet is next; the head is the list's own address */

#define ENEMY_ROT_VEL_Y 0x94 /* Actor.rotVel.y */
#define BULLET_TYPE 0x161
#define BULLET_SPACEBALL 25
#define PLAT_PATH 0x38       /* Platform.path */
#define PLAT_TAG 0x6C
#define PATH_MOVING 0x50     /* PlatformPath.moving: the boss reads a shot's touch from it */

/* AnimState (actor.h): current at 2, speed at 0xC, time at 0x4C. */
typedef struct BossAnim {
    /* 0x00 */ s16 numAnims;
    /* 0x02 */ s16 current;
    /* 0x04 */ u8 pad04[0x08];
    /* 0x0C */ f32 speed;
    /* 0x10 */ u8 pad10[0x3C];
    /* 0x4C */ f32 time;
} BossAnim;

_Static_assert(__builtin_offsetof(BossAnim, time) == 0x4C, "BossAnim has the game's layout");

/* The hook's reading of the fight. */
typedef struct BossSnap {
    s32 n;           /* frameCount */
    u32 frame2;      /* frameCount2 */
    u8 boss;         /* BossId */
    s8 hits;         /* enemies[0]'s */
    s32 flags0;      /* enemies[0].actor.flags */
    /* Atlantis */
    s32 scratch[3];
    s32 stunFlags;   /* enemies[2].brain.flags: bit 0 is the stun */
    s16 delay0;
    f32 rotVelY;
    u8 touch;        /* the ball touches the whale: the flag, or the spheres overlap */
    u8 ballAir;      /* the ball is off the ground and free, and Glover is alive: a touch counts */
    /* Prehistoric */
    s16 anim;
    f32 animTime, animSpeed;
    f32 ballRadius, ballSpeed; /* the ball's first sphere and its speed in x and z */
    /* Space */
    u8 immune[3], portion[3];
    u8 moving[2];    /* a shot touched the left, the right shoulder */
    s32 reload;
    s32 away;        /* the boss's scratch: above 1, it is out of reach */
    s32 lasers;      /* its scratchF */
    u8 shots;
} BossSnap;

static BossSnap cur;     /* the newest reading */
static BossSnap prev;    /* the reading of the game frame before, when havePrev */

static f32 Sqrt(f32 v) {
    return __builtin_sqrtf(v);
}

static f32 Abs(f32 v) {
    return v < 0.0f ? -v : v;
}

/* The platform with this tag (1 to 5), cached. A walk is bounded and runs only when the cache misses. */
static u32 tagPlat[6];
static u8 tagTries[6];

static s16 PlatTag(u32 p) {
    return *(volatile s16 *)(p + PLAT_TAG);
}

static u32 FindTag(int tag) {
    u32 p = (u32)platformList.head.next;

    for (int i = 0; i < 400 && SafePointer((void *)p) && p != (u32)&platformList.head; i++) {
        if (PlatTag(p) == tag) {
            return p;
        }
        p = *(volatile u32 *)p;
    }
    return 0;
}

/* 1 when a shot touched the platform with this tag (its path's moving is above 0, which the boss turns into a hit). */
static int PlatTouched(int tag) {
    u32 p = tagPlat[tag], path;

    if (!SafePointer((void *)p) || PlatTag(p) != tag) {
        p = 0;
        if (tagTries[tag] == 0) {
            p = FindTag(tag);
            tagTries[tag] = 10; /* a miss waits 10 game frames before the next walk */
        } else {
            tagTries[tag]--;
        }
        tagPlat[tag] = p;
        if (p == 0) {
            return 0;
        }
    }
    path = *(volatile u32 *)(p + PLAT_PATH);
    if (!SafePointer((void *)path)) {
        return 0;
    }
    return *(volatile s16 *)(path + PATH_MOVING) > 0;
}

static int CountShots(void) {
    u32 head = (u32)bulletList, p = *(volatile u32 *)head;
    int n = 0;

    for (int i = 0; i < 64 && SafePointer((void *)p) && p != head; i++) {
        n += *(volatile u8 *)(p + BULLET_TYPE) == BULLET_SPACEBALL;
        p = *(volatile u32 *)p;
    }
    return n;
}

/* ---------------------------------------------------------------------------------------------------------
 * The hook
 */

static int ReadAtlantis(BossSnap *b, const BossEnemy *e) {
    f32 wx, wy, wz, rw, bx, by, bz, rb;
    const ActorObject *wo = e->actor.obj, *bo = ball.obj;

    for (int i = 0; i < 3; i++) {
        b->scratch[i] = enemies[i].scratch;
    }
    b->stunFlags = enemies[2].flags;
    b->delay0 = e->delayTimer;
    if (!ReadFloat((const f32 *)((const u8 *)e + ENEMY_ROT_VEL_Y), &b->rotVelY)) {
        return 0;
    }
    if (!SafePointer(wo) || !SafePointer(bo) || !ReadFloat(&e->actor.pos.x, &wx) || !ReadFloat(&e->actor.pos.y, &wy) ||
        !ReadFloat(&e->actor.pos.z, &wz) || !ReadFloat(&wo->spheres[0].radius, &rw) || !ReadFloat(&ball.pos.x, &bx) ||
        !ReadFloat(&ball.pos.y, &by) || !ReadFloat(&ball.pos.z, &bz) || !ReadFloat(&bo->spheres[0].radius, &rb)) {
        return 0;
    }
    /* The whale's one sphere is at its position (sphere flag 0x20), so the game's own test is two radii and a distance. */
    b->touch = (b->flags0 & TOUCHED) != 0;
    if ((wo->spheres[0].flags & 0x20) && rw > 0.0f) {
        f32 dx = bx - wx, dy = by - wy, dz = bz - wz, r = rw + rb;

        b->touch |= dx * dx + dy * dy + dz * dz <= r * r;
    }
    b->ballAir = bo->onGround == 0 && handControl.deathTimer == 0 && (ball.link == NULL || handControl.releaseTimer != 0);
    return 1;
}

static int ReadPrehist(BossSnap *b, const BossEnemy *e) {
    const BossAnim *an = e->actor.anim;
    const ActorObject *bo = ball.obj;
    f32 vx, vz;

    if (!SafePointer(an) || !SafePointer(bo) || !ReadFloat(&an->time, &b->animTime) ||
        !ReadFloat(&an->speed, &b->animSpeed) || !ReadFloat(&bo->spheres[0].radius, &b->ballRadius) ||
        !ReadFloat(&ball.vel.x, &vx) || !ReadFloat(&ball.vel.z, &vz)) {
        return 0;
    }
    b->anim = an->current;
    b->ballSpeed = Sqrt(vx * vx + vz * vz);
    return 1;
}

static int ReadSpace(BossSnap *b, const BossEnemy *e) {
    f32 lasers;

    if (!ReadFloat(&e->scratchF, &lasers)) {
        return 0;
    }
    b->lasers = (s32)lasers;
    for (int i = 0; i < 3; i++) {
        b->immune[i] = immuneCount[i];
        b->portion[i] = portionHealth[i];
    }
    b->moving[0] = PlatTouched(1) | PlatTouched(4);
    b->moving[1] = PlatTouched(2) | PlatTouched(5);
    b->reload = spaceBossReload;
    b->away = e->scratch;
    b->shots = (u8)CountShots();
    return 1;
}

int BossRead(void) {
    BossSnap *b = &cur;
    const BossEnemy *e = &enemies[0];
    int level = gameInfo.level, boss, type;

    /* The level first: it is the common exit. */
    if (level == LEVEL_ATLANTISBOSS) {
        boss = BOSS_ATLANTIS;
        type = TYPE_WHALE;
    } else if (level == LEVEL_PREHISTORICBOSS) {
        boss = BOSS_PREHIST;
        type = TYPE_BIG_RAPTOR;
    } else if (level == LEVEL_SPACEBOSS3) {
        boss = BOSS_SPACE;
        type = TYPE_EVIL_ROBOT;
    } else {
        return 0;
    }
    if (!ActionsInLevel() || gamePaused || (gameInfo.exitFlags & 1) || gameInfo.cameoActive != 0 || run.noclip) {
        return 0;
    }
    if (e->actor.type != type || e->actor.active == 0 || !SafePointer(e->actor.state)) {
        return 0;
    }
    b->n = frameCount;
    b->frame2 = (u32)frameCount2;
    b->boss = (u8)boss;
    b->hits = e->actor.state->hits;
    b->flags0 = e->actor.flags;
    switch (boss) {
        case BOSS_ATLANTIS:
            return ReadAtlantis(b, e);
        case BOSS_PREHIST:
            return ReadPrehist(b, e);
        default:
            return ReadSpace(b, e);
    }
}

/* ---------------------------------------------------------------------------------------------------------
 * The logic
 */

typedef struct Chan {
    s32 hitPass;   /* the pass of the part's last hit; -1: none seen */
    s32 openPass;  /* the pass from which its window was seen open since then; -1: not yet */
    u8 touching;   /* a touch is going on (one event for each) */
} Chan;

static struct {
    u8 havePrev;
    Chan ch[BCH_COUNT];
    BossEvent ev[BOSS_EVENTS];
    u8 nEv;
    u32 lastFrame2;
    s32 flipPass, stunPass; /* the crab's flip and the fish's stun start; -1: none */
    u8 stunOn;
    s16 prehistLeft; /* passes the Prehistoric boss's window had left at the last reading: the reason of a lost touch */
    BossView view;
} bw;

static void ClearState(void) {
    u8 *p = (u8 *)&bw;

    for (u32 i = 0; i < sizeof(bw); i++) {
        p[i] = 0;
    }
    for (int i = 0; i < BCH_COUNT; i++) {
        bw.ch[i].hitPass = bw.ch[i].openPass = -1;
    }
    bw.flipPass = bw.stunPass = -1;
}

void BossReset(void) {
    ClearState();
    for (int i = 0; i < 6; i++) {
        tagPlat[i] = 0;
        tagTries[i] = 0;
    }
}

static s16 Clamp16(int v) {
    return (s16)(v < -30000 ? -30000 : v > 30000 ? 30000 : v);
}

static u32 LastText(char *buf, int size, const BossEvent *e);
static char ChanLetter(int chan);

static void Push(u8 kind, u8 chan, u8 why, int late, int gap, int n) {
    for (int i = BOSS_EVENTS - 1; i > 0; i--) {
        bw.ev[i] = bw.ev[i - 1];
    }
    bw.ev[0].kind = kind;
    bw.ev[0].chan = chan;
    bw.ev[0].why = why;
    bw.ev[0].late = (s8)(late < -1 ? -1 : late > 120 ? 120 : late);
    bw.ev[0].gap = Clamp16(gap);
    bw.ev[0].n = Clamp16(n);
    if (bw.nEv < BOSS_EVENTS) {
        bw.nEv++;
    }
    bw.lastFrame2 = cur.frame2;
    {
        char text[24];
        u32 color = LastText(text, sizeof(text), &bw.ev[0]);

        TrainerResult(TRAINER_BOSS, color, "%c %s", ChanLetter(chan), text);
    }
}

/* A hit of part CH in pass PASS. */
static void RecordHit(int ch, u8 kind, int pass) {
    Chan *c = &bw.ch[ch];
    int timed = c->hitPass >= 0 && c->openPass >= 0;
    int gap = c->hitPass >= 0 ? pass - c->hitPass : 0;
    int late = timed ? (pass > c->openPass ? pass - c->openPass : 0) : -1;

    Push(kind, (u8)ch, 0, late, gap, 0);
    c->hitPass = pass;
    c->openPass = -1;
    /* The touch that made the hit may go on for a few passes. It is not an early touch. */
    c->touching = 1;
}

/* The window of part CH as the hook sees it: OPEN when a touch in pass N counts, else N_LEFT passes until it does.
 * The first pass that is seen open after a hit is the opening pass. */
static void Window(int ch, int open, int left, BossWin *w) {
    Chan *c = &bw.ch[ch];

    if (open) {
        if (c->openPass < 0) {
            c->openPass = cur.n;
        }
        if (c->hitPass < 0) {
            w->state = BWS_READY;
            w->n = 0;
        } else {
            w->state = BWS_OPEN;
            w->n = Clamp16(cur.n - c->openPass);
        }
    } else {
        c->openPass = -1;
        w->state = BWS_CLOSED;
        w->n = Clamp16(left);
    }
}

/* Passes until a touch counts in the whale: the game wipes its wait (delayTimer) when the spin has slowed below 0.05, and
 * each pass the drag multiplies the spin by 0.95 (docs/game.md). 0: a touch in this pass counts. */
static int WhaleLeft(const BossSnap *s) {
    f32 v = Abs(s->rotVelY);

    if (s->delay0 == 0 || s->scratch[0] == 2) {
        return 0;
    }
    for (int k = 0; k < 100; k++) {
        v *= 0.95f;
        if (v < 0.05f) {
            return k;
        }
    }
    return 100;
}

static void StepAtlantis(BossView *v) {
    const BossSnap *s = &cur, *p = &prev;
    int phase = s->hits >= 3 ? 0 : s->hits == 2 ? 1 : s->hits == 1 ? 2 : 3;
    int pass = s->n - 1;

    v->phase = (u8)phase;
    v->maxHits = 5;
    /* Hits. The whale takes three; the crab and the fish each die from one more. */
    if (bw.havePrev && s->hits < p->hits) {
        if (p->hits >= 3) {
            RecordHit(BCH_WHALE, s->hits == 2 ? BEK_SPLIT : BEK_HIT, pass);
        } else if (p->hits == 2) {
            Push(BEK_KILL, BCH_CRAB, 0, -1, bw.flipPass >= 0 ? pass - bw.flipPass : 0, 0);
        } else {
            Push(BEK_KILL, BCH_FISH, 0, -1, bw.stunPass >= 0 ? pass - bw.stunPass : 0, 0);
        }
    }
    /* The whale's window. */
    v->nWin = 1;
    v->win[0].state = BWS_NONE;
    if (phase == 0) {
        int left = WhaleLeft(s);
        Chan *c = &bw.ch[BCH_WHALE];

        Window(BCH_WHALE, left == 0, left, &v->win[0]);
        if (s->touch && s->ballAir) {
            v->flags |= BWF_TOUCH_NOW;
            /* A touch while the window is closed is one event; the ball may stay inside for several passes. */
            if (left > 0 && !c->touching) {
                Push(BEK_EARLY, BCH_WHALE, BWY_EARLY, -1, 0, left);
            }
            c->touching = 1;
        } else {
            c->touching = 0;
            if (s->touch) {
                v->flags |= BWF_TOUCH_GROUND;
            }
        }
    }
    /* The crab: flipped when scratch jumps to 250; a second hit kills it while scratch stays above 31. */
    v->crab = -1;
    if (phase == 1) {
        s32 sc = s->scratch[1];

        if (bw.havePrev && p->scratch[1] < CRAB_FLIPPED && sc >= CRAB_FLIPPED) {
            bw.flipPass = pass;
        }
        if (sc > CRAB_KILL_MIN) {
            v->crab = Clamp16(sc - CRAB_KILL_MIN - 1);
        } else if (sc >= 10) {
            v->crab = 0;
        }
    }
    /* The fish: stunned from the crash landing until it flies again (100 passes). */
    v->fish = -1;
    if (phase == 2) {
        if (s->stunFlags & 1) {
            if (!bw.stunOn) {
                bw.stunOn = 1;
                bw.stunPass = s->n;
            }
            v->fish = Clamp16(FISH_STUN - (s->n - bw.stunPass));
            if (v->fish < 0) {
                v->fish = 0;
            }
        } else {
            bw.stunOn = 0;
        }
    }
}

static void StepPrehist(BossView *v) {
    const BossSnap *s = &cur, *p = &prev;
    Chan *c = &bw.ch[BCH_PREHIST];
    int pass = s->n - 1;
    int hurt = s->anim == 12 || s->anim == 3;
    int left;

    v->maxHits = 3;
    v->nWin = 1;
    if (bw.havePrev && s->hits < p->hits) {
        RecordHit(BCH_PREHIST, BEK_HIT, pass);
    } else if (bw.havePrev && (s->flags0 & TOUCHED) && p->n == s->n - 1 && s->hits > 0 && s->hits == p->hits) {
        /* The ball touched the boss in pass N - 1, was shattered and gave no hit. The reason is from the reading before
         * that pass: the boss's animation then, the ball's speed and size. */
        int pHurt = p->anim == 12 || p->anim == 3;

        if (pHurt) {
            Push(BEK_LOST, BCH_PREHIST, BWY_HURT, -1, 0, bw.prehistLeft);
        } else if (p->ballSpeed <= 3.0f) {
            Push(BEK_LOST, BCH_PREHIST, BWY_SLOW, -1, 0, 0);
        } else if (p->ballRadius <= 9.0f) {
            Push(BEK_LOST, BCH_PREHIST, BWY_SMALL, -1, 0, 0);
        } else {
            Push(BEK_LOST, BCH_PREHIST, BWY_UNKNOWN, -1, 0, 0);
        }
    }
    /* The window: the boss is hurt while its animation is 12 or 3. It opens 34 passes after the hit; with no hit seen the
     * animation's time gives the rest (the two animations end at frame 66). */
    if (s->hits == 0) {
        v->win[0].state = BWS_NONE;
    } else {
        if (c->hitPass >= 0) {
            left = c->hitPass + PREHIST_GAP - s->n;
        } else if (s->animSpeed > 0.1f) {
            left = (int)((66.0f - s->animTime) / s->animSpeed);
        } else {
            left = 1;
        }
        Window(BCH_PREHIST, !hurt, left < 1 ? 1 : left, &v->win[0]);
    }
    bw.prehistLeft = v->win[0].state == BWS_CLOSED ? v->win[0].n : 0;
    v->ballRadius = s->ballRadius;
    v->ballSpeed = s->ballSpeed;
    if (s->ballRadius > 9.0f && s->ballSpeed > 3.0f) {
        v->flags |= BWF_SNOW_READY;
    }
}

static void StepSpace(BossView *v) {
    const BossSnap *s = &cur, *p = &prev;
    int pass = s->n - 1;
    int head = s->hits == 1;

    v->maxHits = 3;
    v->nWin = 3;
    for (int i = 0; i < 3; i++) {
        v->part[i] = s->portion[i];
    }
    /* Hits: a part's health goes down. */
    for (int i = 0; i < 3; i++) {
        if (bw.havePrev && s->portion[i] < p->portion[i]) {
            RecordHit(BCH_LEFT + i, BEK_HIT, pass);
        }
    }
    for (int i = 0; i < 3; i++) {
        BossWin *w = &v->win[i];
        Chan *c = &bw.ch[BCH_LEFT + i];
        int alive = i < 2 ? s->portion[i] > 0 && s->hits >= 2 : head && s->portion[2] > 0;
        /* A shoulder's count is 20 after its hit and the boss's pass counts it down before it looks at a touch; the head
         * looks first (nmeprocess2.c, NMEProcessEvilRobot). */
        int left = i < 2 ? s->immune[i] - 1 : s->immune[2];
        int touch = i < 2 ? s->moving[i] : (s->flags0 & TOUCHED) != 0;

        w->state = BWS_NONE;
        w->n = 0;
        if (!alive) {
            c->touching = 0;
            continue;
        }
        if (left < 0) {
            left = 0;
        }
        Window(BCH_LEFT + i, left == 0, left, w);
        /* A touch that the boss throws away: the shot is gone and nothing was hit. */
        if (touch && left > 0) {
            Push(BEK_LOST, (u8)(BCH_LEFT + i), BWY_EARLY, -1, 0, left);
        }
    }
    v->lasers = head ? Clamp16(s->lasers) : -1;
    if (head && s->lasers <= 0) {
        v->flags |= BWF_LEAVE;
    }
    v->away = s->away > 1 ? Clamp16(s->away) : 0;
    v->reload = Clamp16(s->reload);
    v->shots = s->shots;
}

void BossStep(void) {
    BossView *v = &bw.view;

    /* A new fight, or the same one started again: the level's frame count went back, or the boss's health went up. */
    if (bw.havePrev && (prev.boss != cur.boss || cur.n < prev.n || cur.hits > prev.hits ||
                        (cur.boss == BOSS_SPACE && (cur.portion[0] > prev.portion[0] || cur.portion[1] > prev.portion[1] ||
                                                    cur.portion[2] > prev.portion[2])))) {
        ClearState();
    }
    v->flags = 0;
    v->lasers = -1;
    v->crab = -1;
    v->fish = -1;
    v->away = 0;
    v->reload = 0;
    v->shots = 0;
    v->phase = 0;
    v->nWin = 0;
    for (int i = 0; i < 3; i++) {
        v->win[i].state = BWS_NONE;
        v->win[i].n = 0;
        v->part[i] = 0;
    }
    switch (cur.boss) {
        case BOSS_ATLANTIS:
            StepAtlantis(v);
            break;
        case BOSS_PREHIST:
            StepPrehist(v);
            break;
        default:
            StepSpace(v);
            break;
    }
    v->live = 1;
    v->boss = cur.boss;
    v->hits = (u8)(cur.hits < 0 ? 0 : cur.hits);
    v->frame2 = cur.frame2;
    v->lastFrame2 = bw.lastFrame2;
    v->nEvents = bw.nEv;
    for (int i = 0; i < BOSS_EVENTS; i++) {
        v->events[i] = bw.ev[i];
    }
    prev = cur;
    bw.havePrev = 1;
}

void BossFillView(BossView *out) {
    const u8 *src = (const u8 *)&bw.view;
    u8 *dst = (u8 *)out;

    for (u32 i = 0; i < sizeof(BossView); i++) {
        dst[i] = src[i];
    }
}

/* ---------------------------------------------------------------------------------------------------------
 * Drawing
 */

#define WHITE 0xFFFFFFFF
#define DIM 0x707888FF
#define LABEL 0xC0D0FFFF
#define GREEN 0x60FF60FF
#define YELLOW 0xFFE070FF
#define RED 0xFF5050FF

/* The panel is under the garib counter, right of the line keeper and above the MBZ-L panel (x 156, y 92). It holds 26
 * characters. The last 8 events are two rows of four in the free band at the top of the screen. */
#define BW_X 164
#define BW_Y 44
#define BW_HIST_X 8
#define BW_HIST_Y 3
#define LINE 10
#define SHOW_FRAMES 40 /* game frames that the newest event stays bright: 2 seconds */

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

static char ChanLetter(int chan) {
    static const char letters[BCH_COUNT] = { 'W', 'C', 'F', 'P', 'L', 'R', 'H' };

    return chan < BCH_COUNT ? letters[chan] : '?';
}

/* "+2 LATE", "ON TIME" with the time in tenths of a second for a late hit. */
static const char *LateText(char *buf, int size, int late) {
    if (late < 0) {
        Format(buf, size, "FIRST");
    } else if (late == 0) {
        Format(buf, size, "ON TIME");
    } else {
        Format(buf, size, "+%d (%d.%dS)", late, late / 20, late * 5 % 10);
    }
    return buf;
}

static u32 LateColor(int late) {
    return late < 0 ? WHITE : late == 0 ? GREEN : late <= 3 ? YELLOW : RED;
}

static const char *WhyText(const BossEvent *e, char *buf, int size) {
    switch (e->why) {
        case BWY_EARLY:
            Format(buf, size, "%d EARLY", e->n);
            break;
        case BWY_HURT:
            Format(buf, size, "BOSS HURT %d", e->n);
            break;
        case BWY_SLOW:
            Format(buf, size, "TOO SLOW");
            break;
        case BWY_SMALL:
            Format(buf, size, "NOT SNOWBALL");
            break;
        default:
            Format(buf, size, "NO HIT");
            break;
    }
    return buf;
}

/* The text of the newest event, the long form. */
static u32 LastText(char *buf, int size, const BossEvent *e) {
    char a[24];

    switch (e->kind) {
        case BEK_HIT:
        case BEK_SPLIT:
            if (e->gap > 0) {
                Format(buf, size, "%s %d %s", e->kind == BEK_SPLIT ? "SPLIT" : "HIT", e->gap, LateText(a, sizeof(a), e->late));
            } else {
                Format(buf, size, "%s %s", e->kind == BEK_SPLIT ? "SPLIT" : "HIT", LateText(a, sizeof(a), e->late));
            }
            return LateColor(e->late);
        case BEK_KILL:
            Format(buf, size, "%s DEAD", e->chan == BCH_CRAB ? "CRAB" : "FISH");
            return GREEN;
        case BEK_EARLY:
            Format(buf, size, "TOUCH %d EARLY", e->n);
            return YELLOW;
        case BEK_LOST:
            Format(buf, size, "LOST %s", WhyText(e, a, sizeof(a)));
            return RED;
        default:
            Format(buf, size, "-");
            return DIM;
    }
}

/* One history token: the part, then the gap and the lateness, or what was lost. 9 characters. */
static void DrawToken(Gfx **dl, int x, int y, const BossEvent *e) {
    char a[16];
    char c = ChanLetter(e->chan);

    switch (e->kind) {
        case BEK_HIT:
        case BEK_SPLIT:
            if (e->gap > 0) {
                if (e->late < 0) {
                    Line(dl, x, y, WHITE, "%c%d", c, e->gap);
                } else {
                    Line(dl, x, y, LateColor(e->late), "%c%d +%d", c, e->gap, e->late);
                }
            } else {
                Line(dl, x, y, WHITE, "%c1ST", c);
            }
            break;
        case BEK_KILL:
            Line(dl, x, y, GREEN, "%cDEAD", c);
            break;
        case BEK_EARLY:
            Line(dl, x, y, YELLOW, "%cE%d", c, e->n);
            break;
        default:
            Line(dl, x, y, RED, "%cX %s", c, e->why == BWY_EARLY ? (Format(a, sizeof(a), "E%d", e->n), a)
                                               : e->why == BWY_HURT ? (Format(a, sizeof(a), "H%d", e->n), a)
                                               : e->why == BWY_SLOW ? "SLOW" : e->why == BWY_SMALL ? "SMALL" : "?");
            break;
    }
}

/* The text of a window: "IN 12", "OPEN +3", "OPEN" or "-". Returns its colour. */
static u32 WindowText(char *buf, int size, const BossWin *w) {
    switch (w->state) {
        case BWS_CLOSED:
            Format(buf, size, "IN %d", w->n);
            return w->n <= 5 ? YELLOW : WHITE;
        case BWS_OPEN:
            Format(buf, size, "OPEN +%d", w->n);
            return GREEN;
        case BWS_READY:
            Format(buf, size, "OPEN");
            return GREEN;
        default:
            Format(buf, size, "-");
            return DIM;
    }
}

/* The window line of Atlantis and Prehistoric: the big text. */
static void DrawWindow(Gfx **dl, int x, int y, const BossWin *w) {
    char a[16];
    u32 color = WindowText(a, sizeof(a), w);

    TextPrint(dl, x, y, 2, color, a);
}

/* A Space part's row: its name, its health and its window. */
static void DrawPart(Gfx **dl, int x, int y, const char *name, int health, const BossWin *w) {
    char a[16];
    u32 color = WindowText(a, sizeof(a), w);

    Line(dl, x, y, color, "%-5s %d  %s", name, health, a);
}

void BossDraw(Gfx **dl, const BossView *v) {
    int x = BW_X, y = BW_Y;
    char buf[40];
    int fresh;

    if (!v->live) {
        return;
    }
    /* The last 8 events, newest first. */
    for (int i = 0; i < v->nEvents && i < BOSS_EVENTS; i++) {
        DrawToken(dl, BW_HIST_X + (i & 3) * 64, BW_HIST_Y + (i >> 2) * LINE, &v->events[i]);
    }
    /* The title. */
    switch (v->boss) {
        case BOSS_ATLANTIS: {
            static const char *const names[] = { "WHALE", "CRAB", "FISH", "DEAD" };

            Line(dl, x, y, LABEL, "ATLANTIS %s  HITS %d/%d", names[v->phase & 3], v->hits, v->maxHits);
            break;
        }
        case BOSS_PREHIST:
            Line(dl, x, y, LABEL, "PREHISTORIC  HITS %d/%d", v->hits, v->maxHits);
            break;
        default:
            Line(dl, x, y, LABEL, "SPACE  L%d R%d HEAD %d", v->part[0], v->part[1], v->part[2]);
            break;
    }
    y += LINE;
    if (v->boss == BOSS_SPACE) {
        DrawPart(dl, x, y, "LEFT", v->part[0], &v->win[0]);
        y += LINE;
        DrawPart(dl, x, y, "RIGHT", v->part[1], &v->win[1]);
        y += LINE;
        DrawPart(dl, x, y, "HEAD", v->part[2], &v->win[2]);
        y += LINE;
    } else {
        Line(dl, x, y + 4, LABEL, "WINDOW");
        DrawWindow(dl, x + 44, y, &v->win[0]);
        y += 16;
    }
    /* The newest event: bright for two seconds. */
    fresh = v->nEvents > 0 && v->frame2 - v->lastFrame2 < SHOW_FRAMES;
    if (v->nEvents > 0) {
        u32 color = LastText(buf, sizeof(buf), &v->events[0]);

        Line(dl, x, y, fresh ? color : DIM, "%s", buf);
    } else {
        Line(dl, x, y, DIM, "NO EVENT YET");
    }
    y += LINE;
    /* The boss's own timers: one line of text, a colour and a number for its "%d". */
    {
        const char *fmt = NULL;
        u32 color = WHITE;
        int num = 0;

        switch (v->boss) {
            case BOSS_ATLANTIS:
                if (v->phase == 0) {
                    if (v->flags & BWF_TOUCH_GROUND) {
                        fmt = "TOUCH: BALL NOT FREE";
                        color = YELLOW;
                    } else if (v->flags & BWF_TOUCH_NOW) {
                        fmt = "TOUCHING WHALE";
                    }
                } else if (v->phase == 1) {
                    num = v->crab;
                    if (num > 0) {
                        fmt = "CRAB KILL WITHIN %d";
                        color = num > 20 ? GREEN : YELLOW;
                    } else if (num == 0) {
                        fmt = "CRAB GETS UP";
                        color = RED;
                    } else {
                        fmt = "CRAB UP: FLIP IT";
                        color = DIM;
                    }
                } else if (v->phase == 2) {
                    num = v->fish;
                    if (num >= 0) {
                        fmt = "FISH STUN %d";
                        color = num > 20 ? GREEN : YELLOW;
                    } else {
                        fmt = "FISH FLYING";
                        color = DIM;
                    }
                }
                break;
            case BOSS_PREHIST:
                if (v->flags & BWF_SNOW_READY) {
                    fmt = "SNOWBALL READY";
                    color = GREEN;
                } else {
                    Line(dl, x, y, DIM, "BALL R %.1f V %.1f", (double)v->ballRadius, (double)v->ballSpeed);
                }
                break;
            default:
                break;
        }
        if (fmt != NULL) {
            Line(dl, x, y, color, fmt, num);
        }
    }
    if (v->boss == BOSS_SPACE) {
        if (v->away > 0) {
            Line(dl, x, y, RED, "AWAY %d", v->away);
            y += LINE;
        }
        if (v->lasers >= 0) {
            if (v->flags & BWF_LEAVE) {
                Line(dl, x, y, RED, "LASERS 0 LEAVES AFTER");
            } else {
                Line(dl, x, y, v->lasers <= 2 ? RED : WHITE, "LASERS %d", v->lasers);
            }
            y += LINE;
        }
        Line(dl, x, y, v->reload > 0 ? YELLOW : GREEN, "RELOAD %d  SHOTS %d", v->reload, v->shots);
    }
}
