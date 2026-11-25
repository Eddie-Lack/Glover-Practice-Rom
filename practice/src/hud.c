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
#include "memview.h"
#include "menu.h"
#include "missedtaps.h"
#include "practice.h"
#include "recording.h"
#include "settings.h"
#include "text.h"

#define WHITE 0xFFFFFFFF
#define DIM 0x707888FF
#define LABEL 0xC0D0FFFF
#define MISSED 0xFFA020FF
/* The input display: a button that is not held is dim and half see-through; a held one is white on a blue box. */
#define UNLIT 0x60687890
#define HELD_BOX 0x2850D0E0
#define RED 0xFF5050FF
#define YELLOW 0xFFE070FF
#define NOTE 0x80C8FFFF

/* Every item and note stays inside the part of the screen that a TV shows: SAFE pixels from each edge of the 320 by
 * 240 picture. A line of text is TEXT_H pixels high with its shadow, and the lines of an item are ROW apart. */
#define SCREEN_W 320
#define SCREEN_H 240
#define SAFE 16
#define RIGHT (SCREEN_W - SAFE)
#define TEXT_H 8
#define ROW 10

/* The rows of the notes, below the game's score and clear of the level's countdown at the top centre. The default
 * places of the HUD items (settings.c) leave these rows free. */
#define STATUS_Y 44    /* frame advance or slow motion at the right; the free camera at the left */
#define MODIFIED_Y 58  /* the MODIFIED line at the right, which may be long */
#define RECORDING_Y 68 /* a recording or a playback at the right: they never run with frame advance or slow motion */
/* The message line, centred, between the glove's lines and the input display. */
#define MESSAGE_Y 156

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
        if (!(gameInfo.exitFlags & 1)) {
            playLevel = gameInfo.level;
            playFrames = player.levelFrames;
        } else if (playLevel >= 0) {
            /* An exit runs: the level's time is the last time from now on, through the end of level screen. */
            lastLevel = playLevel;
            lastFrames = player.levelFrames;
            playLevel = -1;
        }
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
    /* The lag counts for one level. */
    lagFrames = 0;
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

/* V, moved as little as it takes for the SIZE pixels from V to lie between LO and HI, or at LO if they cannot. */
static int Fit(int v, int size, int lo, int hi) {
    if (v + size > hi) {
        v = hi - size;
    }
    if (v < lo) {
        v = lo;
    }
    return v;
}

/* Where each item and note was drawn on the last frame that drew it, for the tests (tests/hudlayout.py). */
HudBox hudBoxes[HUD_BOX_COUNT];
static u8 boxId;

static void NoteBox(int id, int x0, int y0, int x1, int y1) {
    HudBox *b = &hudBoxes[id];

    b->x0 = x0;
    b->y0 = y0;
    b->x1 = x1;
    b->y1 = y1;
    b->frame = (u16)practice.drawFrames;
}

void HudFit(int *x, int *y, int width, int above, int below) {
    /* The shadow is one pixel right of the text and one below it. */
    *x = Fit(*x, width + 1, SAFE, RIGHT);
    *y = Fit(*y - above, above + below + TEXT_H, SAFE, SCREEN_H - SAFE) + above;
    NoteBox(boxId, *x, *y - above, *x + width + 1, *y + below + TEXT_H);
}

/* One line of text at X, Y, moved inside the safe area. An item placed at the right edge ends at the edge. */
static void Put(Gfx **dl, int x, int y, u32 color, const char *text) {
    HudFit(&x, &y, TextWidth(text, 1), 0, 0);
    TextPrint(dl, x, y, 1, color, text);
}

static void Line(Gfx **dl, int x, int y, u32 color, const char *fmt, ...) {
    char buf[64];
    va_list args;

    va_start(args, fmt);
    FormatV(buf, sizeof(buf), fmt, args);
    va_end(args);
    Put(dl, x, y, color, buf);
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

/* The width of the labels (the sum of their dx) and of the stick's two numbers, "%+4d %+4d". */
#define BUTTONS_W 112
#define INPUT_W (BUTTONS_W + 9 * TEXT_ADVANCE - 1)
/* The presses of the input history, one after another. */
#define HISTORY_STEP 72

/* The input history's entries: the button, the in-game timer's frame of the press, and how many game frames it
 * was held. Returns the number of entries. */
static int HistoryText(char text[PRESS_HISTORY][16]) {
    int n = 0;

    for (; n < PRESS_HISTORY; n++) {
        const Press *p = &presses[n];
        const char *name = "?";

        if (p->bit == 0) {
            break;
        }
        for (u32 b = 0; b < ARRAY_COUNT(buttonLabels); b++) {
            if (buttonLabels[b].bit == p->bit) {
                name = buttonLabels[b].text;
            }
        }
        Format(text[n], 16, (p->bit & CONT_C_ALL) ? "c%s %d:%d" : "%s %d:%d", name, p->frame, p->frames);
    }
    return n;
}

/* The input display at X, Y; above it the history and the missed taps (missedtaps.h), as one block. */
static void DrawInput(Gfx **dl, int x, int y) {
    char history[PRESS_HISTORY][16];
    char missed[24];
    int n = settings.inputHistory ? HistoryText(history) : 0;
    int w = INPUT_W;
    int above = settings.inputHistory ? ROW : 0;

    if (n > 0 && (n - 1) * HISTORY_STEP + TextWidth(history[n - 1], 1) > w) {
        w = (n - 1) * HISTORY_STEP + TextWidth(history[n - 1], 1);
    }
    if (run.missedTaps) {
        Format(missed, sizeof(missed), "missed taps %u", missedTaps.total);
        above += ROW;
        if (TextWidth(missed, 1) > w) {
            w = TextWidth(missed, 1);
        }
    }
    HudFit(&x, &y, w, above, 0);

    int bx = x;
    for (u32 i = 0; i < ARRAY_COUNT(buttonLabels); i++) {
        const ButtonLabel *b = &buttonLabels[i];
        int held = b->bit != 0 && (input.held & b->bit);
        u32 color = held ? WHITE : UNLIT;

        if (held) {
            TextRect(dl, bx - 1, y - 1, bx + 6, y + 8, HELD_BOX);
        }
        /* A button of a tap that the game missed lately shows in orange (missedtaps.h). */
        if (b->bit != 0 && run.missedTaps && MissedTapsShown(b->bit)) {
            color = MISSED;
        }
        TextPrint(dl, bx, y, 1, color, b->text);
        bx += b->dx;
    }
    {
        char stick[16];
        Format(stick, sizeof(stick), "%+4d %+4d", input.stickX, input.stickY);
        TextPrint(dl, bx, y, 1, (input.stickX | input.stickY) ? WHITE : UNLIT, stick);
    }
    for (int i = 0; i < n; i++) {
        TextPrint(dl, x + i * HISTORY_STEP, y - ROW, 1, presses[i].holding ? WHITE : LABEL, history[i]);
    }
    if (run.missedTaps) {
        TextPrint(dl, x, y - above, 1, missedTaps.last ? MISSED : DIM, missed);
    }
}

static f32 Sqrt(f32 v) {
    return __builtin_sqrtf(v);
}

/* Each field has a space before it down to -9999.9, and the vertical speed down to -99.99. */
static void DrawActor(Gfx **dl, int x, int y, const char *name, const Actor *a) {
    f32 h = Sqrt(a->vel.x * a->vel.x + a->vel.z * a->vel.z);

    Line(dl, x, y, WHITE, "%s%8.1f%8.1f%8.1f h%6.2f v%7.2f", name, (double)a->pos.x, (double)a->pos.y,
         (double)a->pos.z, (double)h, (double)a->vel.y);
}

const char *HandActionName(int mode) {
    return mode >= 0 && mode < (int)ARRAY_COUNT(handActionNames) ? handActionNames[mode] : "?";
}

static void DrawAction(Gfx **dl, int x, int y) {
    s16 mode = hand.state != NULL ? hand.state->mode : -1;
    char number[8];
    const char *name = HandActionName(mode);
    s8 type = ballControl.type;

    /* A mode without a name shows as its number. */
    if (mode >= 0 && !(mode < (int)ARRAY_COUNT(handActionNames))) {
        Format(number, sizeof(number), "%d", mode);
        name = number;
    }
    /* Flag 0x1000 counts as ground for the game's own checks (hand.c). */
    int ground = (hand.obj != NULL && hand.obj->onGround) || (hand.flags & 0x1000);

    Line(dl, x, y, WHITE, "%s %d  %s  lock %d  ball %s", name, handControl.actionTime, ground ? "ground" : "air",
         handControl.actionLock, type >= 0 && type < (s8)ARRAY_COUNT(ballNames) ? ballNames[type] : "?");
}

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

/* 1 while the level being played is not kept as a practice best time (besttimes.h). The game's own demos count as
 * modified there, but nobody plays them. */
static int Tainted(void) {
    return bestTimesState.taint != 0 && autoPlayback != 2;
}

int HudStatusShown(void) {
    return Tainted() || recording.recording || recording.playing;
}

/* The MODIFIED line: the word, and the reason for the tools that have one. */
static void DrawModified(Gfx **dl, int setShown) {
    char buf[48];
    int n = 0;
    int modified = run.moonJump | run.noclip;

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
    /* Whatever else spoils the practice best time shows the word alone, until the level starts again. */
    if (modified || Tainted()) {
        boxId = HUD_BOX_MODIFIED;
        Put(dl, RIGHT, MODIFIED_Y, RED, buf);
    }
}

/* A recording or a playback: a dot and the menu's text (Savestate, Recording), ending at the right edge. */
static void DrawRecording(Gfx **dl) {
    char buf[32];
    u32 color;
    int x;

    if (recording.recording) {
        Format(buf, sizeof(buf), "recording %u", recording.count);
        color = RED;
    } else {
        Format(buf, sizeof(buf), "playing %u/%u", recording.frame, recording.count);
        color = 0x60E060FF;
    }
    x = RIGHT - 1 - TextWidth(buf, 1) - 9;
    TextRect(dl, x, RECORDING_Y + 1, x + 6, RECORDING_Y + 7, color);
    TextPrint(dl, x + 9, RECORDING_Y, 1, WHITE, buf);
    NoteBox(HUD_BOX_RECORDING, x, RECORDING_Y, RIGHT, RECORDING_Y + TEXT_H);
}

/* The notes that show whatever the HUD items are: the MODIFIED line, the camera icon, the free camera, frame advance
 * or slow motion, and a recording. Each has a row of its own. */
void HudDrawStatus(Gfx **dl) {
    static const char *const slow[] = { "", "1/2", "1/3", "1/4" };
    /* A set action (setaction.h) shows the marker's reason for a few seconds. */
    int setShown = run.setMarker > 0;

    if (setShown) {
        run.setMarker--;
    }
    /* A tool that has the menu's panel shows nothing under it. */
    if (menuOpen && menuView != NULL) {
        return;
    }
    DrawModified(dl, setShown);
    if (run.cameraSpeed) {
        DrawCameraIcon(dl);
    }
    if (FreeCamOn()) {
        boxId = HUD_BOX_FREECAM;
        /* The camera is not part of the game: a note, not a reason of the MODIFIED marker. */
        Put(dl, SAFE, STATUS_Y, NOTE, "free camera");
    }
    boxId = HUD_BOX_SPEED;
    if (run.frameAdvance) {
        Put(dl, RIGHT, STATUS_Y, YELLOW, "frame advance");
    } else if (run.slowMotion) {
        Line(dl, RIGHT, STATUS_Y, YELLOW, "slow %s", slow[run.slowMotion % 4]);
    }
    if (recording.recording || recording.playing) {
        DrawRecording(dl);
    }
}

/* The message line. With the menu open it is drawn after the menu, on a solid box. */
void HudDrawMessage(Gfx **dl) {
    if (messageFrames == 0) {
        return;
    }
    messageFrames--;

    int x = SCREEN_W / 2 - TextWidth(hudMessageText, 1) / 2;
    int y = MESSAGE_Y;

    boxId = HUD_BOX_MESSAGE;
    HudFit(&x, &y, TextWidth(hudMessageText, 1), 0, 0);
    if (menuOpen) {
        TextRect(dl, x - 4, y - 3, x + TextWidth(hudMessageText, 1) + 5, y + TEXT_H + 2,
                 (MenuPanelColor(1) & ~0xFFu) | 0xFF);
    }
    TextPrint(dl, x, y, 1, WHITE, hudMessageText);
}

/* The watch list (memview.h): one line for each address that is set. */
static void DrawWatch(Gfx **dl, int x, int y) {
    int n = 0;

    for (int i = 0; i < WATCH_COUNT; i++) {
        n += watches[i] != 0;
    }
    if (n > 0) {
        HudFit(&x, &y, 17 * TEXT_ADVANCE - 1, 0, (n - 1) * ROW);
        WatchDraw(dl, x, y);
    }
}

void HudDraw(Gfx **dl, Gfx *end) {
    /* A tool that has the menu's panel shows nothing under it. */
    if (menuOpen && menuView != NULL) {
        return;
    }
    for (int i = 0; i < HUD_COUNT; i++) {
        const HudPlace *p = &settings.hud[i];
        int x = p->x * 2;
        int y = p->y * 2;

        /* Each item needs at most about 60 characters of 6 commands each. */
        if (!p->on || end - *dl < 400) {
            continue;
        }
        boxId = i;
        switch (i) {
            case HUD_TIMER:
                if (InLevel()) {
                    DrawTimer(dl, x, y);
                }
                break;
            case HUD_INPUT:
                DrawInput(dl, x, y);
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
                DrawWatch(dl, x, y);
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
