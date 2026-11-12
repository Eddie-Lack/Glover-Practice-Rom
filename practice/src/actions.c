/* The quick actions that change the game: reset, warp, frame advance and slow motion. */
#include "actions.h"
#include "besttimes.h"
#include "chaos.h"
#include "collview.h"
#include "demo.h"
#include "corrupt.h"
#include "earlytitle.h"
#include "game.h"
#include "hud.h"
#include "menu.h"
#include "noclip.h"
#include "practice.h"
#include "setaction.h"
#include "spawn.h"
#include "recording.h"
#include "card.h"
#include "savestate.h"

const char *const actionNames[ACTION_COUNT] = {
    [ACTION_RESET] = "Reset level",
    [ACTION_WARP] = "Warp",
    [ACTION_FRAME_ADVANCE] = "Frame advance",
    [ACTION_FRAME_STEP] = "Frame step",
    [ACTION_SLOW_MOTION] = "Slow motion",
    [ACTION_HUD] = "Hide HUD",
    [ACTION_CHECKPOINT] = "Last checkpoint",
    [ACTION_GOTO_CHECKPOINT] = "Go to checkpoint",
    [ACTION_SAVE_STATE] = "Save state",
    [ACTION_LOAD_STATE] = "Load state",
    [ACTION_RECORD] = "Record input",
    [ACTION_PLAY] = "Play recording",
};

_Static_assert(__builtin_offsetof(Run, rngFrozen) == 0x0A && __builtin_offsetof(Run, rngValue) == 0x0C,
               "tests/gametools.py has the offsets");
_Static_assert(__builtin_offsetof(Run, moonJump) == 0x33 && __builtin_offsetof(Run, noclipSpeed) == 0x35,
               "tests/noclip.py has the offsets");
_Static_assert(__builtin_offsetof(Run, collView) == 0x2C && __builtin_offsetof(Run, frameLog) == 0x31,
               "tests/collview.py and tests/framelog.py have the offsets");
_Static_assert(__builtin_offsetof(Run, teleport) == 0x10 && __builtin_offsetof(Run, collEnemies) == 0x3B,
               "tests/collview2.py has the offsets");

_Static_assert(__builtin_offsetof(Run, spawnKind) == 0x3E && __builtin_offsetof(Run, actorsMod) == 0x3F,
               "tests/actors.py has the offsets");

_Static_assert(__builtin_offsetof(Run, missedTaps) == 0x44 && __builtin_offsetof(Run, chaos) == 0x45,
               "tests/missedtaps.py and tests/chaos.py have the offsets");

_Static_assert(__builtin_offsetof(Run, freeCam) == 0x46 && __builtin_offsetof(Run, ghost) == 0x47,
               "tests/freecam.py and tests/ghost.py have the offsets");

_Static_assert(__builtin_offsetof(Run, setGlove) == 0x36 && __builtin_offsetof(Run, setMarker) == 0x3A,
               "tests/setaction.py has the offsets");

_Static_assert(__builtin_offsetof(Run, warpLives) == 0x49 && __builtin_offsetof(Run, warpPower) == 0x4B,
               "tests/warpgive.py has the offsets");

Run run = { .warpLevel = LEVEL_ATLANTIS1, .warpDifficulty = 1, .noclipSpeed = 2 };

static volatile u16 requests; /* one bit per action */
static u8 warpGive;          /* 1: give the Warp menu's lives, garibs and power-up once the new level has started */
static u32 warpStarts;       /* practice.levelStarts when the warp was asked for */
static s8 warpTarget = -1;   /* the level that mode 8 starts, once the current one is freed */

void ActionRequest(int action) {
    switch (action) {
        case ACTION_FRAME_ADVANCE:
            run.frameAdvance ^= 1;
            run.steps = 0;
            break;
        case ACTION_FRAME_STEP:
            if (run.frameAdvance) {
                run.steps++;
            }
            break;
        case ACTION_SLOW_MOTION:
            run.slowMotion = (run.slowMotion + 1) % 4;
            break;
        case ACTION_HUD:
            run.hudHidden ^= 1;
            break;
        default:
            requests |= 1 << action;
            break;
    }
}

int ActionsInLevel(void) {
    return (gameInfo.mode == GAME_MODE_LEVEL || gameInfo.mode == GAME_MODE_CAMEO) && gameInfo.level < LEVEL_PRESENT &&
           levelExitTimer == 0 && newLevExitTimer == 0 && worldExitTimer == 0;
}

/* From the title screen: what LevelSelectScreen does once its fade ends, with the Warp menu's difficulty. */
static void StartFromTitle(int level) {
    autoPlayback = 0;
    currentSaveSlot = SAVE_SLOT_LEVEL_SELECT;
    memclear(&saveSlot[SAVE_SLOT_LEVEL_SELECT], sizeof(SaveSlot));
    saveSlot[SAVE_SLOT_LEVEL_SELECT].difficultyLevel = run.warpDifficulty;
    LeaveTitle();
    StartNewGame();
    ballLeftBehind = -1;
    StartNewLevel(level);
}

int ActionsOnTitle(void) {
    return gameInfo.mode == GAME_MODE_MENU || (gameInfo.mode == GAME_MODE_LEVEL && gameInfo.level >= LEVEL_PRESENT);
}

/* Leaves the level through the Time Trial's restart exit, which frees the level and starts gameInfo.level again
 * in mode 8. A warp changes gameInfo.level in between. */
int ActionsRestart(int level) {
    if (ActionsInLevel() && (gameInfo.exitFlags & 1)) {
        /* An exit is running (the pause menu's Quit, or the early title's): ExitLevel would ignore this one. */
        return 0;
    } else if (ActionsInLevel()) {
        /* A demo (the title's, or one from the Warp menu) ends here: the level that follows is not one. */
        DemoAbort();
        warpTarget = level;
        ExitLevel(EXIT_TIMETRIAL_RESTART, 1, 0);
        /* The level's loader counts its garibs into numGaribs. The pause menu's Time Trial restart sets it to 0 after
         * this exit; without that, each reset added the level's garibs again (0/50, then 0/100). */
        numGaribs = 0;
        return 1;
    } else if (ActionsOnTitle()) {
        DemoAbort();
        StartFromTitle(level);
        return 1;
    }
    return 0;
}

int CheckpointCount(void) {
    int n = 0;

    for (Platform *p = platformList.head.next; p != NULL && p != &platformList.head; p = p->next) {
        n += p->restartGate != NULL;
    }
    return n;
}

Platform *CheckpointAt(int i) {
    for (Platform *p = platformList.head.next; p != NULL && p != &platformList.head; p = p->next) {
        if (p->restartGate != NULL && i-- == 0) {
            return p;
        }
    }
    return NULL;
}

/* The conditions under which the pause menu offers its checkpoint choice (game.c, UpdatePause): on Hard always;
 * otherwise not in a boss or bonus level or a cameo, with Glover or the ball on the ground and no death or loss
 * under way. */
static int PauseOffersCheckpoint(void) {
    int level = gameInfo.level;

    if (saveSlot[currentSaveSlot].difficultyLevel == DIFFICULTY_HARD) {
        return 1;
    }
    if (level == 13 || level == 18 || level == 23 || level == 28 || level == 33 || level == 14 || level == 19 ||
        level == 24 || level == 29 || level == 34 || level == 41 || gameInfo.cameoActive != 0) {
        return 0;
    }
    return (hand.obj->onGround || ball.link == &hand) && handControl.deathTimer == 0 && ballControl.loseTimer == 0 &&
           (ballStatic || ball.obj->onGround || ((ball.flags & 0x200000) && ball.state->waterState));
}

/* What the pause menu's checkpoint choice does (game.c, UpdatePause): RestartPlayer does the rest on the next
 * game frame. A gate never touched has no camera place yet; the pause menu gives it one behind the gate. */
static void Respawn(Platform *gate, int taint) {
    if (gate == NULL || !ActionsInLevel() || gameInfo.level == LEVEL_SPACEBOSS3 || gamePaused) {
        return;
    }
    if (taint) {
        BestTimesTaint(BT_ACTORS);
    }
    handControl.checkpoint = gate;
    gate->restartGate->restartTimer = 65;
    if (gate->restartGate->untouched) {
        gate->restartGate->cameraPos = gate->actor.pos;
        gate->restartGate->cameraPos.z += 100.0f;
    }
}

/* The game fades its live volumes towards the option volumes (UpdatePause), so a mute sets the option volume.
 * The game copies the option volumes into the save slot in StartNewGame, so a new game started while muted
 * keeps no sound in that slot until the options change it. */
void ActionsToggleMute(int arg) {
    if (arg == 0) {
        if (!run.musicMuted) {
            run.musicVolume = curSoundMUSVol;
            curSoundMUSVol = 0;
        } else {
            curSoundMUSVol = run.musicVolume;
        }
        run.musicMuted ^= 1;
    } else {
        if (!run.sfxMuted) {
            run.sfxVolume = curSoundFXVol;
            curSoundFXVol = 0;
        } else {
            curSoundFXVol = run.sfxVolume;
        }
        run.sfxMuted ^= 1;
    }
}

/* Choice 0 gives back the rubber ball; choice n gives pick-up n - 1. Ball types last until changed, as the
 * pick-ups in the levels do; spells last 1000 game frames, as the cheats give them. */
static void GivePickup(int choice) {
    int type = choice - 1;

    if (!ActionsInLevel()) {
        return;
    }
    if (type < 0) {
        ActivateCollectable(-1, -1);
    } else {
        ActivateCollectable(type, type >= PICKUP_BOWLINGBALL && type <= PICKUP_CRYSTALBALL ? -1 : 1000);
    }
}

static void Wait(void) {
    void *msg;

    osRecvMesg(&main_msgQ, &msg, OS_MESG_BLOCK);
}

/* A save or a load stops the game for a moment. The request waits one message of the main queue, and that frame
 * shows "saving..." or "loading...". WANTED is 0 for a request that is refused at once, and for one that does not
 * stop the game (the end of a recording). Returns 1 if the request runs now. */
static u16 announced;
u32 actionsAnnounced; /* the requests that waited: the tests count them */

static int Announce(int action, int wanted, const char *text) {
    if (!wanted || (announced & (1 << action))) {
        announced &= ~(1 << action);
        return 1;
    }
    announced |= 1 << action;
    actionsAnnounced++;
    HudMessage(text);
    return 0;
}

void ActionsGameFrame(void) {
    u16 r = requests | announced;

    requests = 0;
    StateGameFrame();
    if (CardPending()) {
        CardGameFrame();
    }
    if (r & (1 << ACTION_RESET)) {
        if (StateInstantReset()) {
            recording.playing = recording.recording = 0;
            CollViewReset();
        } else {
            ActionsRestart(gameInfo.level);
        }
    }
    if (r & (1 << ACTION_WARP)) {
        u32 starts = practice.levelStarts;

        if (ActionsRestart(run.warpLevel) && (run.warpLives | run.warpGaribs | run.warpPower)) {
            warpGive = 1;
            warpStarts = starts;
        }
        CollViewReset();
    }
    if (r & (1 << ACTION_CHECKPOINT)) {
        /* The pause menu's own choice is a part of the game. A state where that menu hides the choice taints. */
        Respawn(handControl.checkpoint, !PauseOffersCheckpoint());
    }
    if (r & (1 << ACTION_GOTO_CHECKPOINT)) {
        /* A checkpoint the player has not reached moves Glover. */
        Respawn(CheckpointAt(run.checkpoint), 1);
    }
    if ((r & (1 << ACTION_SAVE_STATE)) && Announce(ACTION_SAVE_STATE, StateSaveReady(), "saving...")) {
        StateSave();
    }
    if ((r & (1 << ACTION_LOAD_STATE)) && Announce(ACTION_LOAD_STATE, StateLoadReady(stateSlot), "loading...")) {
        recording.playing = recording.recording = 0;
        StateLoad();
        CollViewReset();
    }
    if ((r & (1 << ACTION_RECORD)) && Announce(ACTION_RECORD, !recording.recording && StateSaveReady(), "saving...")) {
        RecordingToggle();
    }
    if ((r & (1 << ACTION_PLAY)) &&
        Announce(ACTION_PLAY, recording.count != 0 && StateLoadReady(STATE_SLOT_RECORD), "loading...")) {
        RecordingPlay();
    }
    if (run.cheat) {
        /* ActivateCheat turns a cheat on or off, with the game's own checks and message. */
        ActivateCheat(run.cheat - 1, 1);
        run.cheat = 0;
    }
    /* The menu (the controller thread) sets these at any time: they are taken with interrupts off, and only
     * when one is set, so the game's frames do no more with every feature off. */
    if (run.rngReset | run.rngCalls) {
        u32 mask = __osDisableInt();
        int rngReset = run.rngReset;
        int rngCalls = run.rngCalls;
        run.rngReset = 0;
        run.rngCalls = 0;
        __osRestoreInt(mask);
        /* The seed changes in the middle of the level. */
        BestTimesTaint(BT_OTHER);
        if (rngReset) {
            rseed = run.rngValue = RNG_LEVEL_START;
            HudRngRestart();
        }
        if (rngCalls) {
            /* The steps act on the frozen value too, so the freeze keeps them. A Random call of the draw thread
             * between two steps goes on from the stepped seed. */
            if (run.rngFrozen) {
                for (int i = 0; i < rngCalls; i++) {
                    run.rngValue = RngNext(run.rngValue);
                }
            } else {
                for (int i = 0; i < rngCalls; i++) {
                    rseed = RngNext(rseed);
                }
                run.rngValue = rseed;
            }
            /* The HUD's call count leaves out these steps. */
            HudRngMoved();
        }
    }
    if (run.rngFrozen) {
        rseed = run.rngValue;
    }
    if (run.teleport) {
        /* Above the place, with no speed: the game lets Glover fall from there. */
        if (ActionsInLevel()) {
            BestTimesTaint(BT_ACTORS);
            hand.pos = hand.prevPos = run.teleportPos;
            hand.vel.x = hand.vel.y = hand.vel.z = 0.0f;
        }
        run.teleport = 0;
    }
    if (run.chaos | chaos.dropArmed | chaos.pendingAdd) {
        ChaosGameFrame();
    }
    if (corrupt.active | corrupt.requests) {
        CorruptGameFrame();
    }
    if (DemoBusy()) {
        DemoGameFrame();
    }
    if (EarlyTitleBusy()) {
        EarlyTitleGameFrame();
    }
    NoclipGameFrame();
    SetActionGameFrame();
    SpawnGameFrame();
    if (run.givePickup) {
        BestTimesTaint(BT_ACTORS);
        GivePickup(run.pickup);
        run.givePickup = 0;
    }
    if (warpGive && practice.levelStarts != warpStarts && (player.levelFrames > 0 || frameCount >= 150) && ActionsInLevel()) {
        /* The level has reset the garibs and the power-ups. The ball takes a power-up only once the level's intro is over: the in-game timer runs, or 150 game frames passed. */
        warpGive = 0;
        BestTimesTaint(BT_OTHER);
        if (run.warpLives) {
            player.lives = run.warpLives - 1;
        }
        player.garibs = run.warpGaribs;
        if (run.warpPower) {
            GivePickup(run.warpPower - 1);
        }
    }
    if (warpTarget >= 0 && gameInfo.mode == GAME_MODE_SUB_LEVEL) {
        gameInfo.level = warpTarget;
        warpTarget = -1;
        if (demo.start) {
            demo.start = 0;
            DemoSetup();
        }
    }
    if (run.frameAdvance || run.slowMotion) {
        HudResetLag();
    }
    while (run.frameAdvance && run.steps == 0 && !menuOpen) {
        Wait();
        /* A card request made while the game is stopped runs with the game as it stands. */
        if (CardPending()) {
            CardGameFrame();
        }
    }
    if (run.steps > 0) {
        run.steps--;
    }
    for (int i = 0; i < run.slowMotion; i++) {
        Wait();
    }
}
