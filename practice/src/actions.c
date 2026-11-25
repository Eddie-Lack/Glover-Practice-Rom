/* The quick actions that change the game: reset, warp, frame advance and slow motion. */
#include "actions.h"
#include "besttimes.h"
#include "paddisplay.h"
#include "trainers.h"
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

Run run = { .warpLevel = LEVEL_ATLANTIS1, .warpDifficulty = 1, .noclipSpeed = 2, .padX = PAD_X_DEFAULT,
            .padY = PAD_Y_DEFAULT, .padSize = PAD_SIZE_DEFAULT, .logX = TLOG_X_DEFAULT, .logY = TLOG_Y_DEFAULT };

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
    /* The title (PRESS START) sets disablePause; the game clears it when the title exits to the main menu
     * (game.c, CheckForExits). A warp skips that exit. */
    disablePause = 0;
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
        if (pauseMode && pauseCount < 1000) {
            /* The pause menu is open: the game's own restart closes it as it exits (game.c, UpdatePause). Without
             * this the exit never ends and the screen stays black. */
            pauseCount = 1000;
        }
        if (player.health <= 0) {
            /* A reset during a death (drowning) would start the level with no health. */
            player.health = 3;
        }
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

/* Takes one message of the main queue and returns its kind: 9 is the game frame's (RunGame, main.c). */
static int Wait(void) {
    s16 *msg;

    osRecvMesg(&main_msgQ, (void **)&msg, OS_MESG_BLOCK);
    return msg != NULL ? *msg : 0;
}

#define GAME_FRAME_MESSAGE 9

/* A save or a load stops the game for a moment. The request waits one message of the main queue, and that frame
 * shows "saving..." or "loading...". WANTED is 0 for a request that is refused at once, and for one that does not
 * stop the game (the end of a recording). Returns 1 if the request runs now. */
static u16 announced;
u32 actionsAnnounced; /* the requests that waited: the tests count them */

/* The request waits one message of the main queue. Waiting longer, until the text is on the screen, would let the
 * draw thread change the game's memory (its Random calls) before the save, and the state would no longer give the
 * same game frames after a load. */
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

#define STATE_REQUESTS ((1 << ACTION_SAVE_STATE) | (1 << ACTION_LOAD_STATE) | (1 << ACTION_RECORD) | (1 << ACTION_PLAY))

/* The savestate requests. With ANNOUNCE 0 (the game is stopped by frame advance, and the picture already stands
 * still) they run at once. */
static void StateRequests(u16 r, int announce) {
    if ((r & (1 << ACTION_SAVE_STATE)) &&
        Announce(ACTION_SAVE_STATE, announce && StateSaveReady(), "saving...")) {
        StateSave();
    }
    if ((r & (1 << ACTION_LOAD_STATE)) &&
        Announce(ACTION_LOAD_STATE, announce && StateLoadReady(stateSlot), "loading...")) {
        recording.playing = recording.recording = 0;
        StateLoad();
        CollViewReset();
    }
    if ((r & (1 << ACTION_RECORD)) &&
        Announce(ACTION_RECORD, announce && !recording.recording && StateSaveReady() && !run.frameAdvance &&
                                    !run.slowMotion,
                 "saving...")) {
        RecordingToggle();
    }
    if ((r & (1 << ACTION_PLAY)) &&
        Announce(ACTION_PLAY, announce && recording.count != 0 && StateLoadReady(STATE_SLOT_RECORD), "loading...")) {
        RecordingPlay();
    }
}

/* The camera before each of the last game frames, for ActionsChordRepair. A C button that the game saw before the
 * menu's chord was complete changed the zoom (C-down) or started the first-person look (C-up). */
#define CAMERA_SNAPS 4
static struct {
    s32 frame2;
    s16 zoom;
    s32 control;
} cameraSnaps[CAMERA_SNAPS];
static u8 cameraSnapNext;
static volatile u8 repairPending;
static volatile s32 repairFrame;

void ActionsChordRepair(s32 frame2) {
    repairFrame = frame2;
    repairPending = 1;
}

static void CameraSnap(void) {
    if (!repairPending && cameraSnaps[(cameraSnapNext + CAMERA_SNAPS - 1) % CAMERA_SNAPS].frame2 != frameCount2) {
        cameraSnaps[cameraSnapNext].frame2 = frameCount2;
        cameraSnaps[cameraSnapNext].zoom = cameraZoom;
        cameraSnaps[cameraSnapNext].control = controlMethod;
        cameraSnapNext = (cameraSnapNext + 1) % CAMERA_SNAPS;
    }
    if (repairPending) {
        int best = -1;

        repairPending = 0;
        /* The newest snapshot taken before the first C button's game frame. */
        for (int i = 0; i < CAMERA_SNAPS; i++) {
            if (cameraSnaps[i].frame2 <= repairFrame && repairFrame - cameraSnaps[i].frame2 < CAMERA_SNAPS &&
                (best < 0 || cameraSnaps[i].frame2 > cameraSnaps[best].frame2)) {
                best = i;
            }
        }
        if (best >= 0 && ActionsInLevel()) {
            cameraZoom = cameraSnaps[best].zoom;
            if (controlMethod == CONTROL_CAMERA && cameraSnaps[best].control != CONTROL_CAMERA) {
                DeactivateCameraControlMode();
            }
        }
    }
}

/* Frame advance and slow motion count game frames: frameCount2 goes up once for each game frame that runs in a
 * level or a menu. */
static s32 lastFrame2;

/* The modes in which frameCount2 counts game frames (RunGame). In the others (a level's load, the end of level
 * screens) the game runs on, and frame advance stops at the next game frame. */
static int CountsFrames(void) {
    return gameInfo.mode == GAME_MODE_MENU || gameInfo.mode == GAME_MODE_LEVEL || gameInfo.mode == GAME_MODE_CAMEO;
}

void ActionsGameFrame(void) {
    u16 r = requests | announced;

    requests = 0;
    CameraSnap();
    StateGameFrame();
    if (CardPending()) {
        CardGameFrame();
    }
    if (r & (1 << ACTION_RESET)) {
        if (StateInstantReset()) {
            recording.playing = recording.recording = 0;
            CollViewReset();
        } else if (ActionsInLevel()) {
            /* On a title screen (the logos, PRESS START) a restart would start that screen again without its
             * texts, or stop the logos for good. */
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
    StateRequests(r, 1);
    if (run.cheat) {
        /* ActivateCheat turns a cheat on or off, with the game's own checks and message. */
        bestTimesMenuCheats |= 1u << (run.cheat - 1);
        ActivateCheat(run.cheat - 1, 1);
        run.cheat = 0;
    }
    if (run.cheatToggles) {
        u32 mask = __osDisableInt();
        u32 toggles = run.cheatToggles;
        run.cheatToggles = 0;
        __osRestoreInt(mask);
        bestTimesMenuCheats |= toggles;
        for (int i = 0; i < 32; i++) {
            if (toggles & (1u << i)) {
                ActivateCheat(i, 1);
            }
        }
    }
    /* The menu (the controller thread) sets these at any time: they are taken with interrupts off, and only
     * when one is set. With every feature off, the game's frames do no more than before. */
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
    /* RunGame calls this for every message of the main queue, and only some of them run a game frame. A step is
     * used up once a game frame has run, not by the message that let it go. */
    int ran = frameCount2 != lastFrame2;
    lastFrame2 = frameCount2;
    if (ran && run.frameAdvance && run.steps > 0) {
        run.steps--;
    }
    while (run.frameAdvance && run.steps == 0 && !menuOpen && CountsFrames()) {
        Wait();
        /* A card request made while the game is stopped runs with the game as it stands. */
        if (CardPending()) {
            CardGameFrame();
        }
        /* So do the savestate requests: the state is the frame on the screen. */
        if (requests & STATE_REQUESTS) {
            u32 mask = __osDisableInt();
            u16 sr = requests & STATE_REQUESTS;
            requests &= ~STATE_REQUESTS;
            __osRestoreInt(mask);
            StateRequests(sr, 0);
            lastFrame2 = frameCount2;
        }
    }
    if (ran && run.slowMotion && !run.frameAdvance && CountsFrames()) {
        /* 1/2, 1/3 or 1/4: the game frame's messages of 1, 2 or 3 more game frames pass without a frame. */
        for (int n = run.slowMotion; n > 0 && !menuOpen;) {
            if (Wait() == GAME_FRAME_MESSAGE) {
                n--;
            }
        }
    }
}
