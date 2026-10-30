/* The HUD: items drawn over the game while a level runs, each switched on and placed in the menu. */
#include "actions.h"
#include "besttimes.h"
#include "corrupt.h"
#include "game.h"
#include "fmt.h"
#include "freecam.h"
#include "gametimer.h"
#include "hud.h"
#include "input.h"
#include "missedtaps.h"
#include "memview.h"
#include "settings.h"
#include "text.h"

#define WHITE 0xFFFFFFFF
#define DIM 0x707888FF
#define LABEL 0xC0D0FFFF
#define MISSED 0xFFA020FF

static u32 lagFrames;
static u32 lastViCount;
static u8 lagValid;

/* The level being played and its in-game time so far, and the same for the level before it. Kept only while the
 * last level time is shown. */
static s8 playLevel = -1;
static s32 playFrames;
static s8 lastLevel = -1;
static s32 lastFrames;

static const char *const handActionNames[] = {
    "idle", "walk", "jump", "joined", "throw", "prewhack", "whack", "skidding", "duck", "release", "getup",
    "throwaim", "throw2cup", "falling", "bounce", "bounce2cup", "airwhack", "throw2land", "slam", "slam2st",
    "ballwalk", "crawl", "cartwheel", "climbup", "climbdown", "cling", "clingup", "dropdown", "hurt", "push",
    "lob", "catch", "castspell", "rotor", "snatch", "sliding", "hopping", "frog idle", "frog jump",
    "frog walk", "swish", "nothing", "locate", "spin", "frog tongue",
};

static const char *const ballNames[] = { "rubber", "bowling", "power", "bearing", "beach", "snow", "crystal" };

static int InLevel(void);

/* The random number seed of the last game frame, the calls of the game's Random since the level started, and
 * the calls during the last game frame. Kept only while the seed is shown. The count is lost when the seed
 * changes by more than RNG_SEARCH calls (a savestate load, or the item turned on in the middle of a level). */
#define RNG_SEARCH 255
static u32 rngPrev;
static u32 rngCalls;
static u8 rngStep;
static u8 rngLost = 1;

/* One step of the game's generator (maths.c, Random). */
u32 RngNext(u32 r) {
    r ^= r << 1;
    if (!(r & 0x80000000)) {
        r++;
    } else if (r == 0x80000000) {
        r = 0;
    }
    return r;
}

static void RngTick(void) {
    u32 r = rngPrev;
    int n = 0;

    while (r != rseed && n < RNG_SEARCH) {
        r = RngNext(r);
        n++;
    }
    if (r != rseed) {
        rngLost = 1;
    }
    rngStep = n;
    rngCalls += n;
    rngPrev = rseed;
}

/* Video frames beyond the scheduler's aim between two game frames. In mupen64plus every game frame takes more
 * video frames than on a console (docs/testing.md), so the count is only meaningful in accurate emulators and
 * on hardware. */
void HudTick(void) {
    u32 now = __osViIntrCount;

    if (lagValid) {
        u32 elapsed = now - lastViCount;
        if (elapsed > desiredFrameRate) {
            lagFrames += elapsed - desiredFrameRate;
        }
    }
    lastViCount = now;
    lagValid = 1;
    if (settings.hud[HUD_RNG].on) {
        RngTick();
    }
    if (settings.hud[HUD_GAME].on) {
        GameTimerTick();
    }
    BestTimesTick();
    if (settings.hud[HUD_LAST].on && InLevel() && gameInfo.mode == GAME_MODE_LEVEL) {
        playLevel = gameInfo.level;
        playFrames = player.levelFrames;
    }
}

void HudRngRestart(void) {
    rngPrev = rseed;
    rngCalls = 0;
    rngLost = 0;
}

void HudRngMoved(void) {
    rngPrev = rseed;
}

void HudLevelStart(void) {
    if (playLevel >= 0) {
        lastLevel = playLevel;
        lastFrames = playFrames;
    }
    playLevel = -1;
    /* SeedRandom has just set the seed. */
    HudRngRestart();
}

/* Called after anything that stops the game's frames on purpose, such as the practice menu. */
void HudResetLag(void) {
    lagValid = 0;
}

/* A level that the player plays: not the logos, the title flythrough or the intro and outro, which also run as
 * levels. */
static int InLevel(void) {
    return (gameInfo.mode == GAME_MODE_LEVEL || gameInfo.mode == GAME_MODE_CAMEO) && gameInfo.level < LEVEL_PRESENT;
}

static void Line(Gfx **dl, int x, int y, u32 color, const char *fmt, ...) {
    char buf[64];
    va_list args;

    va_start(args, fmt);
    FormatV(buf, sizeof(buf), fmt, args);
    va_end(args);
    TextPrint(dl, x, y, 1, color, buf);
}

/* An in-game time in game frames, as minutes, seconds and hundredths, maybe with the frames. */
static void DrawTime(Gfx **dl, int x, int y, const char *label, s32 frames) {
    /* 20 game frames a second: each frame is 5 hundredths. */
    s32 hundredths = frames * 5;

    if (settings.timerFrames) {
        Line(dl, x, y, WHITE, "%s%d:%02d.%02d %5df", label, hundredths / 6000, hundredths / 100 % 60,
             hundredths % 100, frames);
    } else {
        Line(dl, x, y, WHITE, "%s%d:%02d.%02d", label, hundredths / 6000, hundredths / 100 % 60, hundredths % 100);
    }
}

static void DrawTimer(Gfx **dl, int x, int y) {
    DrawTime(dl, x, y, "", player.levelFrames);
}

typedef struct ButtonLabel {
    u16 bit;
    char text[2];
    s8 dx; /* where the next label starts, from this one */
} ButtonLabel;

/* The face buttons, the D-pad as four arrows, then c and the C buttons as four arrows. */
static const ButtonLabel buttonLabels[] = {
    { CONT_A, "A", 8 },  { CONT_B, "B", 8 },    { CONT_Z, "Z", 8 },     { CONT_START, "S", 8 },
    { CONT_L, "L", 8 },  { CONT_R, "R", 10 },   { CONT_UP, "^", 6 },    { CONT_DOWN, "v", 6 },
    { CONT_LEFT, "<", 6 }, { CONT_RIGHT, ">", 10 }, { 0, "c", 6 },      { CONT_CUP, "^", 6 },
    { CONT_CDOWN, "v", 6 }, { CONT_CLEFT, "<", 6 }, { CONT_CRIGHT, ">", 10 },
};

static void DrawInput(Gfx **dl, int x, int y) {
    int x0 = x;

    for (u32 i = 0; i < ARRAY_COUNT(buttonLabels); i++) {
        const ButtonLabel *b = &buttonLabels[i];
        u32 color = b->bit == 0 ? LABEL : (input.held & b->bit) ? WHITE : DIM;

        /* A button of a tap that the game missed lately shows in orange (missedtaps.h). */
        if (b->bit != 0 && run.missedTaps && MissedTapsShown(b->bit)) {
            color = MISSED;
        }
        TextPrint(dl, x, y, 1, color, b->text);
        x += b->dx;
    }
    Line(dl, x, y, (input.stickX | input.stickY) ? WHITE : DIM, "%+4d %+4d", input.stickX, input.stickY);
    if (run.missedTaps) {
        Line(dl, x0, y - (settings.inputHistory ? 20 : 10), missedTaps.last ? MISSED : DIM, "missed taps %u",
             missedTaps.total);
    }
}

/* The last presses: the button, the level's game frame of the press, and how many game frames it was held. */
static void DrawHistory(Gfx **dl, int x, int y) {
    for (int i = 0; i < PRESS_HISTORY; i++) {
        const Press *p = &presses[i];
        const char *name = "?";

        if (p->bit == 0) {
            break;
        }
        for (u32 b = 0; b < ARRAY_COUNT(buttonLabels); b++) {
            if (buttonLabels[b].bit == p->bit) {
                name = buttonLabels[b].text;
            }
        }
        if (p->bit & CONT_C_ALL) {
            Line(dl, x, y, p->holding ? WHITE : LABEL, "c%s %d:%d", name, p->frame, p->frames);
        } else {
            Line(dl, x, y, p->holding ? WHITE : LABEL, "%s %d:%d", name, p->frame, p->frames);
        }
        x += 72;
    }
}

static f32 Sqrt(f32 v) {
    return __builtin_sqrtf(v);
}

static void DrawActor(Gfx **dl, int x, int y, const char *name, const Actor *a) {
    f32 h = Sqrt(a->vel.x * a->vel.x + a->vel.z * a->vel.z);

    Line(dl, x, y, WHITE, "%s%7.1f%7.1f%7.1f h%5.2f v%6.2f", name, (double)a->pos.x, (double)a->pos.y,
         (double)a->pos.z, (double)h, (double)a->vel.y);
}

const char *HandActionName(int mode) {
    return mode >= 0 && mode < (int)ARRAY_COUNT(handActionNames) ? handActionNames[mode] : "?";
}

static void DrawAction(Gfx **dl, int x, int y) {
    s16 mode = hand.state != NULL ? hand.state->mode : -1;
    const char *name = HandActionName(mode);
    s8 type = ballControl.type;

    /* Flag 0x1000 counts as ground for the game's own checks (hand.c). */
    int ground = (hand.obj != NULL && hand.obj->onGround) || (hand.flags & 0x1000);

    Line(dl, x, y, WHITE, "%s %d  %s  lock %d  ball %s", name, handControl.actionTime, ground ? "ground" : "air",
         handControl.actionLock, type >= 0 && type < (s8)ARRAY_COUNT(ballNames) ? ballNames[type] : "?");
}

/* Frame advance and slow motion show at the top right whenever they are on. */
/* In drawn frames: two seconds at 20 a second. */
#define MESSAGE_FRAMES 40

const char *hudMessageText; /* the text now shown or last shown; the tests read it */
static u8 messageFrames;

void HudMessage(const char *text) {
    hudMessageText = text;
    messageFrames = MESSAGE_FRAMES;
}

int HudMessageShown(void) {
    return messageFrames > 0;
}

/* The camera icon (Game menu, Camera speed): 10 by 9 pixels, two thirds of the game's hearts, above the last digit of
 * the game's score (its digits are about x 278 to 293 and start at y 24). Drawn from rectangles. */
#define CAM_ICON_X 281
#define CAM_ICON_Y 11

static void DrawCameraIcon(Gfx **dl) {
    int x = CAM_ICON_X, y = CAM_ICON_Y;

    TextRect(dl, x - 1, y - 1, x + 11, y + 10, 0x000000A0);   /* outline */
    TextRect(dl, x + 3, y, x + 7, y + 2, 0xE8E8F0FF);         /* the viewfinder */
    TextRect(dl, x, y + 2, x + 10, y + 9, 0xE8E8F0FF);        /* the body */
    TextRect(dl, x + 2, y + 3, x + 8, y + 8, 0x303848FF);     /* the lens ring */
    TextRect(dl, x + 3, y + 4, x + 7, y + 7, 0x60A0FFFF);     /* the glass */
    TextRect(dl, x + 8, y + 2, x + 10, y + 3, 0xFF5050FF);    /* the light */
}

void HudDrawStatus(Gfx **dl) {
    static const char *const slow[] = { "", "1/2", "1/3", "1/4" };

    if (messageFrames > 0) {
        messageFrames--;
        TextPrint(dl, 160 - TextWidth(hudMessageText, 1) / 2, 186, 1, 0xFFFFFFFF, hudMessageText);
    }

    /* The glove is not played as in the retail game: the marker stays on whatever else is shown. */
    {
        char buf[48];
        int n = 0;
        int modified = run.moonJump | run.noclip;
        /* A set action (setaction.h) shows the marker for a few seconds. */
        int setShown = run.setMarker > 0;

        if (setShown) {
            run.setMarker--;
        }
        n += Format(buf + n, sizeof(buf) - n, "MODIFIED");
        if (run.noclip) {
            n += Format(buf + n, sizeof(buf) - n, " noclip");
        }
        if (run.moonJump) {
            n += Format(buf + n, sizeof(buf) - n, run.noclip ? " + moon jump" : " moon jump");
        }
        if (run.chaos) {
            n += Format(buf + n, sizeof(buf) - n, modified ? " + chaos" : " chaos");
            modified = 1;
        }
        if (CorruptMarker()) {
            n += Format(buf + n, sizeof(buf) - n, modified ? " + pokes" : " pokes");
            modified = 1;
        }
        if (!modified && setShown) {
            n += Format(buf + n, sizeof(buf) - n, " set action");
            modified = 1;
        } else if (!modified && run.actorsMod) {
            /* An actor was made or moved (spawn.h): the marker stays until the level changes or restarts. */
            n += Format(buf + n, sizeof(buf) - n, " actors");
            modified = 1;
        }
        if (modified) {
            TextPrint(dl, 316 - TextWidth(buf, 1), 14, 1, 0xFF5050FF, buf);
        }
    }

    if (run.cameraSpeed) {
        DrawCameraIcon(dl);
    }

    if (FreeCamOn()) {
        /* The camera is not part of the game: a note, not the MODIFIED marker. */
        TextPrint(dl, 308 - TextWidth("free camera", 1), 42, 1, 0x80C8FFFF, "free camera");
    }

    if (run.frameAdvance) {
        TextPrint(dl, 236, 28, 1, 0xFFE070FF, "frame advance");
    } else if (run.slowMotion) {
        Line(dl, 236, 28, 0xFFE070FF, "slow %s", slow[run.slowMotion % 4]);
    }
}

void HudDraw(Gfx **dl, Gfx *end) {
    for (int i = 0; i < HUD_COUNT; i++) {
        const HudPlace *p = &settings.hud[i];
        int x = p->x * 2;
        int y = p->y * 2;

        /* Each item needs at most about 60 characters of 6 commands each. */
        if (!p->on || end - *dl < 400) {
            continue;
        }
        switch (i) {
            case HUD_TIMER:
                if (InLevel()) {
                    DrawTimer(dl, x, y);
                }
                break;
            case HUD_INPUT:
                DrawInput(dl, x, y);
                if (settings.inputHistory) {
                    DrawHistory(dl, x, y - 10);
                }
                break;
            case HUD_GLOVE:
                if (InLevel()) {
                    DrawActor(dl, x, y, "G", &hand);
                }
                break;
            case HUD_BALL:
                if (InLevel()) {
                    DrawActor(dl, x, y, "B", &ball);
                }
                break;
            case HUD_ACTION:
                if (InLevel()) {
                    DrawAction(dl, x, y);
                }
                break;
            case HUD_FRAMES:
                Line(dl, x, y, WHITE, "frame %d", frameCount2);
                break;
            case HUD_LAG:
                Line(dl, x, y, WHITE, "lag %u", lagFrames);
                break;
            case HUD_RNG:
                if (rngLost) {
                    Line(dl, x, y, WHITE, "rng %08X", rseed);
                } else {
                    Line(dl, x, y, WHITE, "rng %08X #%u +%d", rseed, rngCalls, rngStep);
                }
                break;
            case HUD_WATCH:
                WatchDraw(dl, x, y);
                break;
            case HUD_GAME:
                GameTimerDraw(dl, x, y);
                break;
            case HUD_LAST:
                if (lastLevel >= 0) {
                    DrawTime(dl, x, y, "last ", lastFrames);
                }
                break;
        }
    }
}
