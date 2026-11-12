/* The Fear boss sweep (sweep.h) and its view (Inspect, Fear boss sweep results). */
#include "actions.h"
#include "fmt.h"
#include "game.h"
#include "hud.h"
#include "menu.h"
#include "sweep.h"
#include "text.h"

#define LEVEL_FEAR3 32
#define LEVEL_FEARBOSS 33
#define EXIT_LEVEL 1
#define SWITCH_TAG 20
#define BALL_BURST 2
#define PATH_WAITS_FOR_BALL (-3)

/* Game data that the practice header does not declare. The offsets are gloveworld's (platform.h, actor.h). */
extern u8 enemies[];  /* enemies[0].actor is Frankie; the actor's anim pointer is at 0xDC */
extern u8 rain[];     /* Weather: lightning, the thunder flashes left, at 0x0F */
extern u8 ballPain;   /* a static of DoFortressBoss: game frames the ball has been in Frankie's aura */
#define PLATFORM_EXIT_TYPE(p) (*(s32 *)((u8 *)(p) + 0x18))
#define PLATFORM_PATH(p) (*(u8 **)((u8 *)(p) + 0x38))
#define PLATFORM_TAG(p) (*(s16 *)((u8 *)(p) + 0x6C))
#define PATH_MOVING(path) (*(s16 *)((path) + 0x50))
#define PATH_CURRENT(path) (*(u8 *)((path) + 0x57))

enum {
    STAGE_OFF,
    STAGE_WARP,     /* ask for the warp to Fear 3 */
    STAGE_FEAR3,    /* wait in Fear 3, then move the glove and the ball onto the exit */
    STAGE_EXIT,     /* the exit spin: A held */
    STAGE_TRANSIT,  /* the end of level screen and the boss's first game frames: A on every second read */
    STAGE_BOSS,     /* watch the bursts */
};

#define FEAR3_WAIT 200   /* game frames in Fear 3 before the move */
#define STAGE_LIMIT 2000 /* game frames a stage may take */

Sweep sweep;

static SweepResult *const results = (SweepResult *)SWEEP_RESULTS;
static u8 bursts;
static u8 prevBall;
static u8 pressA; /* the controller thread's toggle */
static s32 startLives;
static u32 nearestSq;
static u8 page; /* the view's page */

static Platform *FindPlatform(int byTag, int value) {
    for (Platform *p = platformList.head.next; p != &platformList.head; p = p->next) {
        if (byTag ? PLATFORM_TAG(p) == value : PLATFORM_EXIT_TYPE(p) == value) {
            return p;
        }
    }
    return NULL;
}

static void NextRun(int status) {
    SweepResult *r = &results[sweep.run];

    r->status = status;
    r->end = frameCount;
    if (status == SWEEP_HIT) {
        sweep.hits++;
    }
    sweep.done++;
    if (++sweep.run >= SWEEP_RUNS) {
        sweep.stage = STAGE_OFF;
        HudMessage("sweep done");
        return;
    }
    sweep.stage = STAGE_WARP;
    sweep.frames = 0;
}

void SweepStart(int arg) {
    (void)arg;
    for (int k = 0; k < SWEEP_RUNS; k++) {
        SweepResult *r = &results[k];
        *r = (SweepResult){ 0 };
        r->rain = SWEEP_RAIN(k);
        r->pain = SWEEP_PAIN(k);
    }
    sweep = (Sweep){ STAGE_WARP, 0, 0, 0, 0 };
    MenuClose();
}

void SweepStop(int arg) {
    (void)arg;
    sweep.stage = STAGE_OFF;
}

void SweepText(char *buf, int size, int arg) {
    (void)arg;
    Format(buf, size, "%d/%d runs %d hits p%d", sweep.done, SWEEP_RUNS, sweep.hits, page + 1);
}

void SweepGameFrame(void) {
    SweepResult *r = &results[sweep.run];
    int inLevel = gameInfo.mode == GAME_MODE_LEVEL || gameInfo.mode == GAME_MODE_CAMEO;

    if (++sweep.frames > STAGE_LIMIT && sweep.stage != STAGE_WARP) {
        NextRun(SWEEP_NO_BURST);
        return;
    }
    switch (sweep.stage) {
        case STAGE_WARP:
            if (ActionsRestart(LEVEL_FEAR3)) {
                sweep.stage = STAGE_FEAR3;
                sweep.frames = 0;
            }
            break;
        case STAGE_FEAR3:
            if (!inLevel || gameInfo.level != LEVEL_FEAR3 || frameCount < FEAR3_WAIT) {
                sweep.frames = frameCount < FEAR3_WAIT ? 0 : sweep.frames;
                break;
            }
            player.lives = 9;
            if (levelExitTimer != 0) {
                rain[0x0F] = r->rain;
                sweep.stage = STAGE_EXIT;
                sweep.frames = 0;
            } else {
                Platform *exit = FindPlatform(0, EXIT_LEVEL);

                if (exit != NULL) {
                    hand.pos = exit->actor.pos;
                    hand.pos.y += 25.0f;
                    ball.pos = exit->actor.pos;
                    ball.pos.y += 12.0f;
                }
            }
            break;
        case STAGE_EXIT:
            if (gameInfo.exitFlags & 1) {
                sweep.stage = STAGE_TRANSIT;
                sweep.frames = 0;
            }
            break;
        case STAGE_TRANSIT:
            if (inLevel && gameInfo.level == LEVEL_FEARBOSS) {
                ballPain = r->pain;
                r->carried = rain[0x0F];
                bursts = 0;
                prevBall = 0;
                startLives = -1;
                nearestSq = 0xFFFFFFFF;
                sweep.stage = STAGE_BOSS;
                sweep.frames = 0;
            }
            break;
        case STAGE_BOSS: {
            Actor *frankie = (Actor *)enemies;
            s16 anim = frankie->anim != NULL ? ((s16 *)frankie->anim)[1] : -1;
            Platform *sw = FindPlatform(1, SWITCH_TAG);
            u8 *path = sw != NULL ? PLATFORM_PATH(sw) : NULL;
            u8 state = ballControl.state;

            if (frameCount > 400 && anim == 0 && r->aura == 0) {
                r->aura = frameCount;
            }
            if (state == BALL_BURST && prevBall != BALL_BURST && bursts < 2) {
                bursts++;
                if (bursts == 1) {
                    r->burst1 = frameCount;
                    r->seed1 = rseed;
                } else {
                    r->burst2 = frameCount;
                    r->seed2 = rseed;
                    startLives = player.lives;
                }
            }
            prevBall = state;
            if (bursts == 2) {
                f32 dx = ball.pos.x - sw->actor.pos.x;
                f32 dy = ball.pos.y - sw->actor.pos.y;
                f32 dz = ball.pos.z - sw->actor.pos.z;
                f32 d = dx * dx + dy * dy + dz * dz;

                if (d < 1e9f && (u32)d < nearestSq) {
                    nearestSq = (u32)d;
                    r->nearest = (u16)(__builtin_sqrtf(d) * 10.0f);
                }
            }
            if (path != NULL && (PATH_MOVING(path) != PATH_WAITS_FOR_BALL || PATH_CURRENT(path) != 0)) {
                NextRun(SWEEP_HIT);
            } else if (bursts == 2 && player.lives != startLives) {
                NextRun(SWEEP_MISS);
            }
            break;
        }
    }
}

void SweepRead(u8 cntlr) {
    ContPad *c = &controllerdata[cntlr];

    if (cntlr != 0) {
        return;
    }
    switch (sweep.stage) {
        case STAGE_FEAR3:
            c->button = 0;
            c->stickX = c->stickY = 0;
            break;
        case STAGE_EXIT:
            c->button = CONT_A;
            break;
        case STAGE_TRANSIT:
            pressA ^= 1;
            c->button = pressA ? CONT_A : 0;
            break;
        case STAGE_BOSS:
            /* Through the boss's first game frames, then nothing: the opening locks the controller anyway. */
            pressA ^= 1;
            c->button = (frameCount <= 3 && pressA) ? CONT_A : 0;
            c->stickX = c->stickY = 0;
            break;
    }
}

/* ---- The view: 16 runs a page. Left and right turn the page, B goes back. ---- */

#define VIEW_X 8
#define VIEW_Y 24
#define VIEW_W 304
#define ROW_H 10
#define ROWS 16


static void SweepOpen(void) {
}

static int SweepInput(u16 pressed, u16 held) {
    (void)held;
    if (pressed & CONT_B) {
        return 0;
    }
    if ((pressed & CONT_RIGHT) && (page + 1) * ROWS < SWEEP_RUNS) {
        page++;
    }
    if ((pressed & CONT_LEFT) && page > 0) {
        page--;
    }
    return 1;
}

static const char *const statusNames[] = { "-", "miss", "HIT", "fail" };

static void SweepDraw(Gfx **dl) {
    char buf[64];

    TextRect(dl, VIEW_X, VIEW_Y, VIEW_X + VIEW_W, VIEW_Y + 16 + (ROWS + 3) * ROW_H, MenuPanelColor(0));
    TextRect(dl, VIEW_X, VIEW_Y, VIEW_X + VIEW_W, VIEW_Y + 13, MenuPanelColor(1));
    SweepText(buf, sizeof(buf), 0);
    TextPrint(dl, VIEW_X + 6, VIEW_Y + 3, 1, 0xFFE070FF, buf);
    TextPrint(dl, VIEW_X + 6, VIEW_Y + 16, 1, 0x8090A8FF, "rn p c  b1 seed1     b2 seed2    aur end");
    for (int i = 0; i < ROWS; i++) {
        int k = page * ROWS + i;
        if (k >= SWEEP_RUNS) {
            break;
        }
        const SweepResult *r = &results[k];
        Format(buf, sizeof(buf), "%2d %d %d %3d %08x %3d %08x %3d %3d %-4s %3d.%d", r->rain, r->pain, r->carried,
               r->burst1, r->seed1, r->burst2, r->seed2, r->aura, r->end, statusNames[r->status & 3],
               r->nearest / 10, r->nearest % 10);
        TextPrint(dl, VIEW_X + 6, VIEW_Y + 16 + (i + 1) * ROW_H, 1, r->status == SWEEP_HIT ? 0x70FF70FF : 0xFFFFFFFF,
                  buf);
    }
}

const View sweepView = { SweepOpen, SweepInput, SweepDraw };
