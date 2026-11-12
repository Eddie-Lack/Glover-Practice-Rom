/* The hooks: the game calls these functions in place of its own (hooks/). Each calls the game's function. */
#include "actions.h"
#include "besttimes.h"
#include "card.h"
#include "collview.h"
#include "corrupt.h"
#include "earlytitle.h"
#include "framelog.h"
#include "game.h"
#include "gametimer.h"
#include "ghost.h"
#include "hud.h"
#include "input.h"
#include "menu.h"
#include "moonaim.h"
#include "paddisplay.h"
#include "practice.h"
#include "saveedit.h"
#include "settings.h"
#include "text.h"
#include "trainers.h"

Practice practice;

static u8 atlas[TEXT_ATLAS_SIZE] __attribute__((aligned(8)));

/* The overlay's display lists, one for each of the game's two frame buffers, like the game's own lists. */
#define OVERLAY_SIZE 5960
static Gfx overlayList[2][OVERLAY_SIZE] __attribute__((aligned(8)));

/* Called by the boot hook after the loader has copied the practice segment. */
void PracticeInit(void) {
    /* The game's heap gets the loader's memory back as it was before. */
    for (u32 *p = (u32 *)loader_VRAM; p < (u32 *)loader_BSS_END; p++) {
        *p = 0;
    }
    TextInit(atlas);
    MenusInit();
    SettingsDefault();
    SettingsLoadAtBoot();
    practice.magic = PRACTICE_MAGIC;
}

/* The fog switch. The draw thread builds a frame's display list in one go, from SendLastFrame to PrintTimers. The
 * game's logic never reads fog.on. While the switch is on, fog.on is 0 between the two calls and the level's value
 * otherwise. */
static u8 fogSaved;
static u8 fogForced;

/* The start of each frame's display list: DrawGraphics calls this in place of SendLastFrame. */
void PracticeSendLastFrame(void) {
    SendLastFrame();
    if (run.fogOff && fog.on) {
        fogSaved = fog.on;
        fogForced = 1;
        fog.on = 0;
        practice.fogFrames++;
    }
}

/* The end of each frame's display list: DrawGraphics calls this in place of PrintTimers. The game's list grows
 * by one command for the HUD and the menu, and one for the collision view, only on frames that draw them. */
void PracticePrintTimers(void) {
    Gfx *start = overlayList[draw_buffer & 1];
    Gfx *g = start;
    Gfx *end = start + OVERLAY_SIZE - 8;

    if (fogForced) {
        /* A level load in between may have set fog.on itself; then it keeps the new value. */
        if (fog.on == 0) {
            fog.on = fogSaved;
        }
        fogForced = 0;
    }
    PrintTimers();
    practice.drawFrames++;
    if (run.collView) {
        CollViewDraw();
    }
    if (run.ghost) {
        GhostDraw();
    }
    _Static_assert(HUD_COUNT == 11, "every HUD item belongs in this test");
    /* With nothing to show, this returns at once: the draw thread's time delays the game's own work during loads,
     * and even a little more of it moved the start of a level by a game frame (docs/testing.md). */
    int hud = !run.hudHidden && (settings.hud[HUD_TIMER].on | settings.hud[HUD_INPUT].on | settings.hud[HUD_GLOVE].on |
                                 settings.hud[HUD_BALL].on | settings.hud[HUD_ACTION].on |
                                 settings.hud[HUD_FRAMES].on | settings.hud[HUD_LAG].on | settings.hud[HUD_RNG].on |
                                 settings.hud[HUD_LAST].on | settings.hud[HUD_GAME].on |
                                 settings.hud[HUD_WATCH].on);
    if (!menuOpen && !hud && !run.frameAdvance && !run.slowMotion && !(run.moonJump | run.noclip | run.chaos | run.freeCam | run.cameraSpeed) &&
        !run.trainers &&
        !(run.setMarker | run.actorsMod) && !CorruptMarker() && !HudMessageShown()) {
        return;
    }
    textShadow = settings.hudShadow;
    /* The text stops before the end of the list, which keeps room for the end command. */
    TextBegin(&g, end);
    if (hud) {
        HudDraw(&g, end - 1000);
    }
    HudDrawStatus(&g);
    if (run.trainers) {
        TrainersDraw(&g, end - 200);
    }
    if (menuOpen) {
        MenuDraw(&g);
    }
    if (g - start <= 12) {
        return; /* only the set-up commands: nothing to draw */
    }
    *g++ = GfxEndDisplayList();
    osWritebackDCache(start, (g - start) * sizeof(Gfx));
    *glistp++ = GfxDisplayList(start);
    practice.textFrames++;
}

/* The start of each game frame's logic: RunGame calls this in place of ClearText. While the practice menu is
 * open, the game's logic waits here, one video frame message at a time. */
void PracticeClearText(void) {
    if (menuOpen && settings.menuPauses) {
        u32 start = __osViIntrCount;
        while (menuOpen && settings.menuPauses) {
            void *msg;
            osRecvMesg(&main_msgQ, &msg, OS_MESG_BLOCK);
            /* The Savestate menu's card items (Check, Delete) run here, with the menu open. */
            if (CardPending()) {
                CardGameFrame();
            }
        }
        GameTimerExclude(__osViIntrCount - start);
        HudResetLag();
    }
    ActionsGameFrame();
    TrainersGameFrame();
    if (run.collHoles && run.collView) {
        HolesGameFrame();
    }
    GhostGameFrame();
    HudTick();
    ClearText();
}

/* 1 while PracticeProcessInput holds the difficulty at Easy (saveedit.h). */
volatile u8 difficultySwapped;

/* The glove's controls: MainGameLoop calls this in place of ProcessInput. On Medium and Hard the retail game
 * reverses the stick while Glover stands on the ball. The practice ROM uses the Easy direction unless the
 * setting asks for the retail one. ProcessInput reads the difficulty only for that (docs/game.md). */
void PracticeProcessInput(void) {
    SaveSlot *slot = &saveSlot[currentSaveSlot];
    u8 difficulty = slot->difficultyLevel;

    if (run.frameLog) {
        FrameLogTick();
    }
    if (run.trainers) {
        TrainersProcessInput();
    }
    if (!settings.ballNormal) {
        /* The controller thread can run inside ProcessInput. The save file editor waits while this is set. */
        difficultySwapped = 1;
        slot->difficultyLevel = DIFFICULTY_EASY;
    }
    ProcessInput();
    slot->difficultyLevel = difficulty;
    difficultySwapped = 0;
}

/* The start of each level: StartNewLevel calls this in place of SeedRandom. */
void PracticeSeedRandom(void) {
    SeedRandom();
    /* The in-game timer fix (docs/game.md, "The in-game timer"). A level with a bonus timer sets timer321 to 0
     * and nothing sets it back, so in every later level the timer started at once. DoBoss still sets it for
     * bonus levels and Time Trials after this. */
    timer321 = 4;
    practice.levelStarts++;
    HudLevelStart();
    BestTimesLevelStart();
    /* A level that starts after a request to quit for the early title is not the way to the title. */
    earlyTitle.pending = 0;
}

/* A finished level: CheckForLevelCompleted updates the game's own records of the level (its best time, the level
 * and garib bits). The practice best time is noted first, from the same state. */
s32 PracticeCheckForLevelCompleted(void) {
    BestTimesLevelDone();
    return CheckForLevelCompleted();
}

/* The Hard difficulty ends a level in CheckForExits without the end of level screen: the game sets the record
 * itself and goes to the name screen (PrepareSong), or starts the title (InitOverlayList). Each is the first call
 * on its path after the exit is known. The practice best time is noted there. A hub exit has already called
 * CheckForLevelCompleted, but its level has no time and is not noted twice. */
void PracticePrepareSong(u8 song) {
    BestTimesLevelDone();
    PrepareSong(song);
}

void PracticeInitOverlayList(void) {
    BestTimesLevelDone();
    InitOverlayList();
}

/* StartNewLevel calls this in place of InitScreenDebris, after LoadLevel. */
void PracticeInitScreenDebris(void) {
    InitScreenDebris();
    PadDisplayLevelStart();
}
