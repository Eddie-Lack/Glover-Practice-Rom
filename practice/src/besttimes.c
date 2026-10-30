/* The practice best times: one best in-game time for each level, kept in RAM apart from the game's save. */
#include "actions.h"
#include "besttimes.h"
#include "corrupt.h"
#include "fmt.h"
#include "game.h"
#include "hud.h"
#include "settings.h"

PracticeBests practiceBests;
BestTimesState bestTimesState;

/* The cheats that only open ways (level select, open portals, open levels) leave the run as it was played. */
#define NAVIGATION_CHEATS ((1 << 23) | (1 << 24) | (1 << 25))

/* StartNewLevel sets cheat bit 0 (Locate garibs) on a file at Easy, and nothing clears it until the console is
 * switched off. A run that starts with the bit set is not tainted by it. */
#define LOCATE_GARIBS 1

/* The bits of cheatActivated that the game itself set at the start of the level being played. */
static s32 startCheats;

static char message[48];

/* An in-game time as the HUD shows it: 20 game frames a second, each 5 hundredths. */
void BestTimesFormat(char *buf, int size, u32 frames) {
    u32 h = frames * 5;

    Format(buf, size, "%d:%02d.%02d", h / 6000, h / 100 % 60, h % 100);
}

int BestTimesCount(void) {
    int n = 0;

    for (int i = 0; i < BEST_LEVELS; i++) {
        n += practiceBests.frames[i] != 0;
    }
    return n;
}

void BestTimesClear(void) {
    for (int i = 0; i < BEST_LEVELS; i++) {
        practiceBests.frames[i] = 0;
    }
    bestTimesState.lastResult = BT_RESULT_NONE;
    bestTimesState.lastBit = -1;
}

void BestTimesTaint(int bits) {
    bestTimesState.taint |= bits;
}

void BestTimesLevelStart(void) {
    bestTimesState.taint = 0;
    startCheats = cheatActivated & LOCATE_GARIBS;
}

/* Called on every message of a game frame. It only reads switches and sets bits. The game's own cheat bits do not
 * count (see NAVIGATION_CHEATS and LOCATE_GARIBS). */
void BestTimesTick(void) {
    int t = 0;
    int ignored = NAVIGATION_CHEATS | startCheats;

    if (run.moonJump | run.noclip) {
        t |= BT_NOCLIP;
    }
    if (run.chaos) {
        t |= BT_CHAOS;
    }
    if (CorruptMarker()) {
        t |= BT_POKES;
    }
    if (run.actorsMod) {
        t |= BT_ACTORS;
    }
    if (run.setMarker) {
        t |= BT_SET;
    }
    if (run.frameAdvance | run.slowMotion) {
        t |= BT_TIME;
    }
    if (autoPlayback == 2) {
        t |= BT_OTHER; /* a demo */
    }
    if ((cheatActivated & ~ignored) | run.rngFrozen | settings.debugPad | run.cameraSpeed) {
        t |= BT_OTHER;
    }
    bestTimesState.taint |= t;
}

/* A level is finished: a time is noted at each of the three places that finish a level (docs/build.md, "Best
 * times"). The game updates its own records for gameInfo.prevLevel at or before them. */
void BestTimesLevelDone(void) {
    BestTimesState *st = &bestTimesState;
    char time[16];
    char best[16];
    int level = gameInfo.prevLevel;
    int bit;
    u32 frames = player.levelFrames;

    if (!(gameInfo.exitFlags & 2) || level < 0 || level >= NUM_LEVELS) {
        return;
    }
    bit = levelInfo[level].bit;
    /* Hubs, the cave and the other levels without a time have bit 30. */
    if (bit < 0 || bit >= BEST_LEVELS || frames == 0 || frames > 0xFFFF) {
        return;
    }
    st->lastBit = bit;
    st->lastFrames = frames;
    st->finished++;
    BestTimesFormat(time, sizeof(time), frames);
    /* The message shows when the HUD shows times at all. */
    int show = settings.hud[HUD_TIMER].on | settings.hud[HUD_LAST].on;

    if (st->taint) {
        st->lastResult = BT_RESULT_MODIFIED;
        Format(message, sizeof(message), "%s not kept: modified", time);
    } else if (practiceBests.frames[bit] == 0 || frames < practiceBests.frames[bit]) {
        practiceBests.frames[bit] = frames;
        st->lastResult = BT_RESULT_NEW;
        Format(message, sizeof(message), "new practice best %s", time);
    } else {
        st->lastResult = BT_RESULT_SLOWER;
        BestTimesFormat(best, sizeof(best), practiceBests.frames[bit]);
        Format(message, sizeof(message), "%s, practice best %s", time, best);
    }
    if (show) {
        HudMessage(message);
    }
}

/* The Hi Scores screen (credits.c) draws a world's page with printHiscores: the world's name and five rows,
 * each with a level, a name and a time. This draws the same page from the practice times, with hundredths and
 * no name. The rows slide in from alternate sides by Xos1, as in the game. It never writes the game's records.
 * With no practice time at all, or the setting on the game's records, the game draws the page. */
void PracticeprintHiscores(char *name, s32 start) {
    char text[32];

    if (bestTimesState.showGame || BestTimesCount() == 0) {
        printHiscores(name, start);
        return;
    }
    NEW_TEXT(40.0f - Xos1, 60, 255, 255, 128, 255, 0.7f, 0.7f, 1, name, 0);
    SetFont(1);
    for (int i = 0; i < 5; i++) {
        f32 slide = (i & 1) ? -Xos1 : Xos1;
        int y = 80 + i * 20;
        u32 frames = practiceBests.frames[start + i];

        Format(text, sizeof(text), "%6s", hsLevNames[6 + i][gameInfo.language]);
        NEW_TEXT((s16)(55.0f + slide), y, 255, 155, 155, 255, 1.0f, 1.0f, 1, text, 0);
        if (frames) {
            BestTimesFormat(text, sizeof(text), frames);
        } else {
            Format(text, sizeof(text), "-:--.--");
        }
        NEW_TEXT((s16)(190.0f + slide), y, 255, 155, 155, 255, 1.0f, 1.0f, 1, text, 0);
    }
}
