/* Watching the game's demo recordings (demo.h). */
#include "actions.h"
#include "demo.h"
#include "earlytitle.h"
#include "game.h"

Demo demo;

const u8 demoLevels[DEMO_COUNT] = { 10, 16, 20, 25, 30, 15, 35, 39 };
_Static_assert(LEVEL_SPACEBOSS3 == 40, "the cutscene (level 39) comes last: it leaves for Space boss 3 (DemoGameFrame)");

void DemoRequest(int what) {
    demo.request = what;
}

/* What DemoKeyRun does: the demo's save slot, Medium, no cheats, a new game. */
void DemoSetup(void) {
    currentSaveSlot = SAVE_SLOT_LEVEL_SELECT;
    saveSlot[SAVE_SLOT_LEVEL_SELECT].difficultyLevel = DIFFICULTY_MEDIUM;
    ActivateCheat(22, 0);
    cheatCount = 1;
    /* DoMenu loads the title's textures when booting is 0. The press start screen leaves it 1, and the title that
     * follows the demo goes on to its menu, which needs them. */
    booting = 0;
    StartNewGame();
    autoPlayback = 2;
}

void DemoAbort(void) {
    demo.all = demo.start = demo.advance = 0;
    if (autoPlayback == 2) {
        JallocFree(&recordKeys);
        rkp = NULL;
        nextKeyFrame = -1;
        exitCount = 0;
        playbackButtons = 0;
        autoPlayback = 0;
    }
}

/* From the title, or after a demo has left for it: the steps of DemoKeyRun. Without the two StopDrawing calls the
 * title that follows the demo stops while it loads. */
static void Begin(void) {
    StopDrawing("dkr");
    memclear(cfb_16_a, 0x25800);
    memclear(cfb_16_b, 0x25800);
    LeaveTitle();
    globalFontScale = 1.0f;
    StopDrawing("dkr");
    doFades = 1;
    DemoSetup();
    StartNewLevel(demoLevels[demo.index]);
}

void DemoGameFrame(void) {
    int request = demo.request;
    int mode = gameInfo.mode;

    demo.request = 0;
    if (request) {
        if (ActionsOnTitle()) {
            demo.all = request == DEMO_ALL;
            demo.index = demo.all ? 0 : demo.choice;
            Begin();
        } else if (ActionsInLevel() && !(gameInfo.exitFlags & 1)) {
            /* The Time Trial's restart exit; actions.c calls DemoSetup when mode 8 has set the level. */
            ActionsRestart(demoLevels[request == DEMO_ALL ? 0 : demo.choice]);
            demo.all = request == DEMO_ALL;
            demo.index = demo.all ? 0 : demo.choice;
            demo.start = 1;
        }
    }
    if (demo.advance) {
        if (mode == GAME_MODE_SUB_LEVEL) {
            /* The cutscene ended and the game goes on to Space boss 3: leave as a quit does (game.c, CheckForExits). */
            demo.advance = demo.all = 0;
            levelSelectCount = 0;
            frameCount = 0;
            gameInfo.mode = GAME_MODE_MENU;
        } else if (mode == GAME_MODE_MENU && levelSelectCount == 0) {
            demo.advance = 0;
            if (demo.all && ++demo.index < DEMO_COUNT) {
                Begin();
            } else {
                demo.all = 0;
            }
        } else if ((mode == GAME_MODE_LEVEL || mode == GAME_MODE_CAMEO) && !(gameInfo.exitFlags & 1)) {
            /* A state was loaded: the demo is not ending. */
            demo.advance = 0;
        }
    }
}

void DemoRead(u8 cntlr) {
    ReadControllerFromData(cntlr);
    /* The game turns autoPlayback off when the recording ends or a button leaves it. */
    if (autoPlayback != 2) {
        demo.advance = 1;
    }
}
