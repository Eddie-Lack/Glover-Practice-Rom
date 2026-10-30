/* The team's early title menu (earlytitle.h). */
#include "actions.h"
#include "demo.h"
#include "earlytitle.h"
#include "game.h"
#include "hud.h"
#include "recording.h"

EarlyTitle earlyTitle;

_Static_assert(__builtin_offsetof(EarlyTitle, request) == 0x02 && __builtin_offsetof(EarlyTitle, frames) == 0x04,
               "tests/titlemenu.py has the offsets");

void EarlyTitleRequest(int what) {
    earlyTitle.request = what;
}

/* Frees what the title or the level holds, as the game does before it starts a level from the title. The normal title
 * has no level (the exit that led to it freed the level, and a second FreeLevel never returns); it holds objects and
 * overlays of its own, which KillTitle frees. Every other place has a level loaded: a level, the title flythrough,
 * the early title and its level select. In mode 1, levelSelectCount is 0 until DoMenu or TitleScreen has set up, and
 * again after the exit that leads to the title, after KillTitle and after this function: nothing is loaded then. */
void LeaveTitle(void) {
    if (gameInfo.mode == GAME_MODE_MENU && levelSelectCount == 0) {
        /* Nothing is loaded. */
    } else if (gameInfo.mode == GAME_MODE_MENU && !earlyTitle.on) {
        onlyDrawBackdrops = 0;
        KillTitle();
    } else {
        ClearAllText();
        FreeBackdropList();
        frameCount = frameCount2 = 0;
        FreeLevel();
    }
    frameCount = frameCount2 = 0;
}

/* Leaves what is loaded and sets up mode 1 with levelSelectCount 0, which makes DoMenu and TitleScreen start. */
static void Free(void) {
    LeaveTitle();
    gameInfo.mode = GAME_MODE_MENU;
    levelSelectCount = 0;
}

void EarlyTitleGameFrame(void) {
    int request = earlyTitle.request;

    earlyTitle.request = 0;
    /* An exit for the early title that did not finish (a loaded state, a death) must not make a later Quit show it. */
    if (earlyTitle.pending && gameInfo.mode != GAME_MODE_MENU &&
        (!(gameInfo.exitFlags & 1) || gameInfo.exitType != EXIT_QUIT)) {
        earlyTitle.pending = 0;
    }
    if (request == EARLYTITLE_OPEN) {
        if (ActionsInLevel()) {
            if ((gameInfo.exitFlags & 1) || earlyTitle.pending) {
                /* ExitLevel(EXIT_QUIT) runs UpdateHiscoreTable again, whatever an exit in progress is. */
                HudMessage("early title: the level is ending");
                return;
            }
            /* The game's own way out of a level, as the pause menu's Quit: it fades out, frees the level and
             * starts mode 1 with levelSelectCount 0. PracticeDoMenu then calls TitleScreen. */
            earlyTitle.pending = 1;
            DemoAbort();
            recording.playing = recording.recording = 0;
            ExitLevel(EXIT_QUIT, 1, 0);
        } else if (ActionsOnTitle()) {
            recording.playing = recording.recording = 0;
            earlyTitle.pending = 0;
            Free();
            earlyTitle.on = 1;
        }
    } else if (request == EARLYTITLE_BACK && (earlyTitle.on || gameInfo.mode == GAME_MODE_LEVEL_SELECT)) {
        /* The state after the title flythrough ends (game.c, CheckForExits): DoMenu shows the normal title. */
        Free();
        earlyTitle.on = 0;
        earlyTitle.pending = 0;
        gameInfo.level = LEVEL_FLYTHRU;
        currentScreen = 1;
        booting = 0;
        disablePause = 0;
    }
    /* Any other way out of the early title (a warp, a reset) ends it. */
    if (earlyTitle.on && gameInfo.mode != GAME_MODE_MENU && gameInfo.mode != GAME_MODE_LEVEL_SELECT) {
        earlyTitle.on = 0;
    }
}

/* RunGame calls this in place of DoMenu (hooks/main.c.patch). */
void PracticeDoMenu(void) {
    if (earlyTitle.pending) {
        earlyTitle.pending = 0;
        earlyTitle.on = 1;
    }
    if (earlyTitle.on) {
        earlyTitle.frames++;
        TitleScreen();
        /* Continue and new game start a level, and level select changes the mode. */
        if (gameInfo.mode != GAME_MODE_MENU) {
            earlyTitle.on = 0;
        }
        return;
    }
    DoMenu();
}
