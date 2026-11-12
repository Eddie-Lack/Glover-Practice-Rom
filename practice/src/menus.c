/* The practice menu's contents. */
#include "actions.h"
#include "besttimes.h"
#include "card.h"
#include "collview.h"
#include "corrupt.h"
#include "cpak.h"
#include "demo.h"
#include "earlytitle.h"
#include "fmt.h"
#include "game.h"
#include "gametimer.h"
#include "ghost.h"
#include "hud.h"
#include "memview.h"
#include "menu.h"
#include "recording.h"
#include "saveedit.h"
#include "savestate.h"
#include "settings.h"
#include "setaction.h"
#include "spawn.h"
#include "trainers.h"
#include "version.h"

#define ITEM(label, kind, lo, hi, ptr, sub, fn, names, info, arg, bind) \
    { label, kind, lo, hi, ptr, sub, fn, names, info, arg, bind, NULL, NULL }
#define TOGGLE(label, ptr) ITEM(label, ITEM_TOGGLE, 0, 1, ptr, NULL, NULL, NULL, NULL, 0, -1)
#define NUMBER(label, ptr, lo, hi) ITEM(label, ITEM_NUMBER, lo, hi, ptr, NULL, NULL, NULL, NULL, 0, -1)
#define CHOICE(label, ptr, names, n) ITEM(label, ITEM_CHOICE, 0, (n) - 1, ptr, NULL, NULL, names, NULL, 0, -1)
#define SUBMENU(label, menu) ITEM(label, ITEM_SUBMENU, 0, 0, NULL, &menu, NULL, NULL, NULL, 0, -1)
#define ACTION(label, fn, arg) ITEM(label, ITEM_ACTION, 0, 0, NULL, NULL, fn, NULL, NULL, arg, -1)
/* A quick action: its binding shows next to it. */
#define QUICK(label, action) ITEM(label, ITEM_ACTION, 0, 0, NULL, NULL, Request, NULL, NULL, action, action)
/* A quick action with a note in place of the key help while it is selected. */
#define QUICK_HINT(label, action, hint) \
    { label, ITEM_ACTION, 0, 0, NULL, NULL, Request, NULL, NULL, action, action, NULL, hint }
#define STATE(label, fn, info, arg) ITEM(label, ITEM_ACTION, 0, 0, NULL, NULL, fn, NULL, info, arg, -1)
/* A state item with a warning in place of the key help while it is selected. */
#define WARN_STATE(label, fn, info, arg, hint) \
    { label, ITEM_ACTION, 0, 0, NULL, NULL, fn, NULL, info, arg, -1, NULL, hint }
#define INFO(label, fn) ITEM(label, ITEM_INFO, 0, 0, NULL, NULL, NULL, NULL, fn, 0, -1)
#define BIND(label, action) ITEM(label, ITEM_BIND, 0, 0, NULL, NULL, NULL, NULL, NULL, action, -1)
#define VIEW(label, v) { label, ITEM_VIEW, 0, 0, NULL, NULL, NULL, NULL, NULL, 0, -1, &v, NULL }
/* A toggle with a warning in place of the key help while it is selected. */
#define WARN(label, ptr, hint) { label, ITEM_TOGGLE, 0, 1, ptr, NULL, NULL, NULL, NULL, 0, -1, NULL, hint }
/* A choice with a warning in place of the key help while it is selected. */
#define WARN_CHOICE(label, ptr, names, n, hint) \
    { label, ITEM_CHOICE, 0, (n) - 1, ptr, NULL, NULL, names, NULL, 0, -1, NULL, hint }
/* An action with a warning in place of the key help while it is selected. */
#define WARN_ACTION(label, fn, arg, hint) \
    { label, ITEM_ACTION, 0, 0, NULL, NULL, fn, NULL, NULL, arg, -1, NULL, hint }
/* A view with a warning in place of the key help while it is selected. */
#define WARN_VIEW(label, v, hint) { label, ITEM_VIEW, 0, 0, NULL, NULL, NULL, NULL, NULL, 0, -1, &v, hint }
#define MENU(name, title, items) const Menu name = { title, items, ARRAY_COUNT(items) }

static void Request(int action) {
    ActionRequest(action);
    /* The actions that change the level act once the menu is closed: the game is waiting while it is open. */
    if (action == ACTION_RESET || action == ACTION_WARP || action == ACTION_CHECKPOINT ||
        action == ACTION_GOTO_CHECKPOINT || action == ACTION_SAVE_STATE || action == ACTION_LOAD_STATE ||
        action == ACTION_RECORD || action == ACTION_PLAY) {
        MenuClose();
    }
}

static void OnOff(char *buf, int size, int on) {
    Format(buf, size, "%s", on ? "on" : "off");
}

/* HUD positions are in units of 2 pixels: x up to 160, y up to 120. */
#define PLACE(label, item) \
    NUMBER(label " x", &settings.hud[item].x, 0, 160), NUMBER(label " y", &settings.hud[item].y, 0, 120)

static const MenuItem layoutItems[] = {
    PLACE("Timer", HUD_TIMER),   PLACE("Input display", HUD_INPUT), PLACE("Glove", HUD_GLOVE),
    PLACE("Ball", HUD_BALL),     PLACE("Action", HUD_ACTION),       PLACE("Frame counter", HUD_FRAMES),
    PLACE("Lag counter", HUD_LAG),     PLACE("Random seed", HUD_RNG),     PLACE("Last level time", HUD_LAST),
    PLACE("Full-game timer", HUD_GAME), PLACE("Watch list", HUD_WATCH),
};
static MENU(layoutMenu, "HUD layout", layoutItems);

static const char *const splitNames[] = { "each level", "each crystal" };

static void GameTimerAction(int arg) {
    if (arg == 0) {
        GameTimerStart();
    } else {
        GameTimerReset();
    }
}

static void GameTimerText(char *buf, int size, int arg) {
    (void)arg;
    u32 h = GameTimerHundredths(gameTimer.elapsed);
    Format(buf, size, "%s %d:%02d.%02d", gameTimer.done ? "done" : gameTimer.running ? "running" : "stopped",
           h / 6000, h / 100 % 60, h % 100);
}

/* The splits: the level that each one ended, and its time. Filled in by MenusInit. */
static char splitLabels[SPLITS_MAX][4];
static MenuItem splitItems[SPLITS_MAX];
static MENU(splitMenu, "Splits", splitItems);

static void SplitText(char *buf, int size, int arg) {
    s8 level = gameTimer.splitLevel[arg];
    u32 end = gameTimer.splits[arg];
    u32 h = GameTimerHundredths(end - (arg > 0 ? gameTimer.splits[arg - 1] : 0));

    if (arg >= gameTimer.splitCount) {
        buf[0] = '\0';
        return;
    }
    Format(buf, size, "%s  %d:%02d.%02d", level >= 0 && level < NUM_LEVELS ? levelInfo[level].name : "?",
           h / 6000, h / 100 % 60, h % 100);
}

static const MenuItem gameTimerItems[] = {
    TOGGLE("Show the full-game timer", &settings.hud[HUD_GAME].on),
    CHOICE("Splits", &settings.splitByCrystal, splitNames, 2),
    ACTION("Start now", GameTimerAction, 0),
    ACTION("Stop and clear", GameTimerAction, 1),
    INFO("Time", GameTimerText),
    SUBMENU("Splits", splitMenu),
};
static MENU(gameTimerMenu, "Full-game timer", gameTimerItems);

static const MenuItem counterItems[] = {
    TOGGLE("Frame counter", &settings.hud[HUD_FRAMES].on),
    TOGGLE("Lag counter", &settings.hud[HUD_LAG].on),
    TOGGLE("Random seed", &settings.hud[HUD_RNG].on),
    TOGGLE("Last level time", &settings.hud[HUD_LAST].on),
};
static MENU(counterMenu, "Counters", counterItems);

static const char *const padDisplayNames[] = { "off", "controller 1", "controller 2" };

static const MenuItem hudItems[] = {
    TOGGLE("Timer", &settings.hud[HUD_TIMER].on),
    TOGGLE("Timer in frames too", &settings.timerFrames),
    TOGGLE("Input display", &settings.hud[HUD_INPUT].on),
    TOGGLE("Input history", &settings.inputHistory),
    WARN_CHOICE("Controller display", &run.padDisplay, padDisplayNames, 3, "From the next level start or reset"),
    TOGGLE("Glove position and speed", &settings.hud[HUD_GLOVE].on),
    TOGGLE("Ball position and speed", &settings.hud[HUD_BALL].on),
    TOGGLE("Glove action", &settings.hud[HUD_ACTION].on),
    SUBMENU("Counters", counterMenu),
    SUBMENU("Full-game timer", gameTimerMenu),
    TOGGLE("Text shadow", &settings.hudShadow),
    SUBMENU("Layout", layoutMenu),
    QUICK("Hide HUD", ACTION_HUD),
};
static MENU(hudMenu, "HUD", hudItems);

/* The level names come from the game's own table (levelInfo), filled in by MenusInit. */
#define WARP_LEVELS 42
static const char *levelNames[WARP_LEVELS];
static const char *const difficultyNames[] = { "Easy", "Medium", "Hard" };

static void CheckpointText(char *buf, int size, int arg) {
    (void)arg;
    if (!ActionsInLevel()) {
        Format(buf, size, "not in a level");
    } else {
        Format(buf, size, "%d of %d", run.checkpoint, CheckpointCount() - 1);
    }
}

/* The early title shows once the menu is closed and the game thread has run. */
static void EarlyTitleAction(int arg) {
    EarlyTitleRequest(arg);
    MenuClose();
}

/* The demos start once the menu is closed. */
static void DemoAction(int arg) {
    DemoRequest(arg);
    MenuClose();
}

static const char *demoNames[DEMO_COUNT];
static const char *const resetNames[] = { "load the level", "instant" };

static const char *const warpLivesNames[] = { "as is", "0", "1", "2", "3", "4", "5", "6", "7", "8", "9" };
static const char *const warpPowerNames[NUM_PICKUPS + 2] = {
    "none",      "Rubber ball", "Stun",       "Death",         "Frog",      "Suction",
    "Hercules",  "Speed up",    "Rotor blades", "Bowling ball", "Power ball", "Ball bearing",
    "Beach ball", "Snow ball",  "Crystal ball", "Boomerang ball", "Vanish ball",
};

static const MenuItem warpItems[] = {
    CHOICE("Level", &run.warpLevel, levelNames, WARP_LEVELS),
    CHOICE("Difficulty (from the title)", &run.warpDifficulty, difficultyNames, 3),
    QUICK("Go", ACTION_WARP),
    QUICK("Reset level", ACTION_RESET),
    QUICK("Last checkpoint", ACTION_CHECKPOINT),
    NUMBER("Checkpoint", &run.checkpoint, 0, 40),
    INFO("Checkpoints", CheckpointText),
    QUICK("Go to checkpoint", ACTION_GOTO_CHECKPOINT),
    WARN_ACTION("Early title menu", EarlyTitleAction, EARLYTITLE_OPEN, "Early title. May crash. Edits your save"),
    ACTION("Back to the normal title", EarlyTitleAction, EARLYTITLE_BACK),
    WARN_CHOICE("Reset", &settings.instantReset, resetNames, 2, "Instant: stops 0.25 s at a level start"),
    CHOICE("Demo", &demo.choice, demoNames, DEMO_COUNT),
    WARN_ACTION("Watch demo", DemoAction, DEMO_ONE, "A, B or Start leaves. Cheats go off"),
    WARN_ACTION("Watch all demos", DemoAction, DEMO_ALL, "A, B or Start stops. Cheats go off"),
    CHOICE("Lives", &run.warpLives, warpLivesNames, 11),
    NUMBER("Garibs", &run.warpGaribs, 0, 99),
    CHOICE("Power-up", &run.warpPower, warpPowerNames, NUM_PICKUPS + 2),
};
static MENU(warpMenu, "Warp", warpItems);

static const char *const slowNames[] = { "off", "1/2", "1/3", "1/4" };

static void FrameAdvanceText(char *buf, int size, int arg) {
    (void)arg;
    OnOff(buf, size, run.frameAdvance);
}

static const MenuItem timeItems[] = {
    STATE("Frame advance", Request, FrameAdvanceText, ACTION_FRAME_ADVANCE),
    QUICK("Frame step", ACTION_FRAME_STEP),
    CHOICE("Slow motion", &run.slowMotion, slowNames, 4),
};
static MENU(timeMenu, "Frame advance", timeItems);

static void RecordingText(char *buf, int size, int arg) {
    (void)arg;
    if (recording.recording) {
        Format(buf, size, "recording %u", recording.count);
    } else if (recording.playing) {
        Format(buf, size, "playing %u/%u", recording.frame, recording.count);
    } else {
        Format(buf, size, "%u frames", recording.count);
    }
}

static const char *const slotNames[STATE_USER_SLOTS] = { "1", "2", "3", "4", "5", "6", "7", "8", "9", "10" };

/* The slot choice changed: the game thread's view of "a state is saved" follows. */
static void SlotChanged(int arg) {
    (void)arg;
    stateSaved = StateSlotUsed(stateSlot);
}

static void ClearSlotAction(int arg) {
    StateClear(arg == 0 ? stateSlot : STATE_SLOTS);
}

/* The card items ask the game thread. A save and a load close the menu: the game stops for seconds. */
static void CardAction(int arg) {
    CardRequest(arg);
    if (arg == CARD_REQ_SAVE || arg == CARD_REQ_LOAD || arg == CARD_REQ_SAVE_REC || arg == CARD_REQ_LOAD_REC) {
        MenuClose();
    }
}

static const MenuItem cardItems[] = {
    INFO("SD card", CardStatusText),
    ACTION("Check card", CardAction, CARD_REQ_CHECK),
    NUMBER("Card slot", &cardSlot, 1, CARD_SLOTS),
    INFO("Card file", CardFileText),
    WARN_ACTION("Save slot to card", CardAction, CARD_REQ_SAVE, "Writes the card file: stops the game for seconds"),
    WARN_ACTION("Load from card", CardAction, CARD_REQ_LOAD, "Stops the game for seconds"),
    WARN_ACTION("Delete on card", CardAction, CARD_REQ_DELETE, "Deletes the card file"),
    WARN_ACTION("Save recording to card", CardAction, CARD_REQ_SAVE_REC, "Writes RECORD.GRC: stops the game for seconds"),
    WARN_ACTION("Load recording from card", CardAction, CARD_REQ_LOAD_REC, "Loads its state: stops the game for seconds"),
};
static MENU(cardMenu, "SD card", cardItems);

static const MenuItem stateItems[] = {
    ITEM("Slot", ITEM_CHOICE, 0, STATE_USER_SLOTS - 1, &stateSlot, NULL, SlotChanged, slotNames, NULL, 0, -1),
    QUICK_HINT("Save state", ACTION_SAVE_STATE, "Stops the game 0.4 s (1.5 s at first)"),
    QUICK_HINT("Load state", ACTION_LOAD_STATE, "Stops the game 0.4 to 0.5 s"),
    QUICK_HINT("Record input", ACTION_RECORD, "Saves a state first: stops the game"),
    QUICK_HINT("Play recording", ACTION_PLAY, "Loads the state: stops the game 0.4 s"),
    INFO("Recording", RecordingText),
    INFO("This slot", StateSlotText),
    INFO("Pool", StatePoolText),
    ACTION("Clear this slot", ClearSlotAction, 0),
    WARN_ACTION("Clear all slots", ClearSlotAction, 1, "Forgets every saved state"),
    INFO("Level start state", StateAutoText),
    SUBMENU("SD card", cardMenu),
};
static MENU(stateMenu, "Savestate", stateItems);

static const MenuItem bindItems[] = {
    BIND("Reset level", ACTION_RESET),
    BIND("Warp", ACTION_WARP),
    BIND("Last checkpoint", ACTION_CHECKPOINT),
    BIND("Go to checkpoint", ACTION_GOTO_CHECKPOINT),
    BIND("Frame advance", ACTION_FRAME_ADVANCE),
    BIND("Frame step", ACTION_FRAME_STEP),
    BIND("Slow motion", ACTION_SLOW_MOTION),
    BIND("Hide HUD", ACTION_HUD),
    BIND("Save state", ACTION_SAVE_STATE),
    BIND("Load state", ACTION_LOAD_STATE),
    BIND("Record input", ACTION_RECORD),
    BIND("Play recording", ACTION_PLAY),
};
static MENU(bindMenu, "Bindings", bindItems);

/* The game's cheats (cheat.c), toggled with its own ActivateCheat. */
static const char *const cheatNames[] = {
    "Locate garibs", "Call ball",      "Checkpoints",  "Infinite lives", "Power ball",    "Control ball",
    "Infinite energy", "Enemy ball",   "Low gravity",  "Big ball",       "Fish eye",      "Camera rotate R",
    "Camera rotate L", "Mad garibs",   "Death spell",  "Frog spell",     "Hercules",      "Speed up",
    "Boomerang ball", "Vanish ball",   "Froggy",       "Secret cheat",   "All cheats off", "Level select",
    "Open portals",  "Open levels",
};

static void CheatText(char *buf, int size, int arg) {
    /* Cheats 11, 12 and 22 act once and have no bit. */
    if (arg == 11 || arg == 12 || arg == 22) {
        buf[0] = '\0';
    } else {
        OnOff(buf, size, cheatActivated & (1 << arg));
    }
}

static void ToggleCheat(int arg) {
    run.cheat = arg + 1;
}

#define CHEAT(n) STATE(cheatNames[n], ToggleCheat, CheatText, n)
static const MenuItem cheatItems[] = {
    CHEAT(0),  CHEAT(1),  CHEAT(2),  CHEAT(3),  CHEAT(4),  CHEAT(5),  CHEAT(6),  CHEAT(7),  CHEAT(8),
    CHEAT(9),  CHEAT(10), CHEAT(11), CHEAT(12), CHEAT(13), CHEAT(14), CHEAT(15), CHEAT(16), CHEAT(17),
    CHEAT(18), CHEAT(19), CHEAT(20), CHEAT(21), CHEAT(22), CHEAT(23), CHEAT(24), CHEAT(25),
};
static MENU(cheatMenu, "Cheats", cheatItems);

static void MuteText(char *buf, int size, int arg) {
    OnOff(buf, size, arg == 0 ? run.musicMuted : run.sfxMuted);
}

static const char *const pickupNames[NUM_PICKUPS + 1] = {
    "Rubber ball", "Stun",       "Death",       "Frog",      "Suction",   "Hercules",
    "Speed up",    "Rotor blades", "Bowling ball", "Power ball", "Ball bearing", "Beach ball",
    "Snow ball",   "Crystal ball", "Boomerang ball", "Vanish ball",
};

static void GivePickup(int arg) {
    (void)arg;
    run.givePickup = 1;
}

static const MenuItem pickupItems[] = {
    CHOICE("Power-up", &run.pickup, pickupNames, NUM_PICKUPS + 1),
    ACTION("Give", GivePickup, 0),
};
static MENU(pickupMenu, "Power-ups", pickupItems);

/* The lowest byte of a big-endian s32. The values here stay below 256. */
#define LOW_BYTE(x) ((u8 *)&(x) + 3)

static void RngText(char *buf, int size, int arg) {
    (void)arg;
    if (run.rngFrozen) {
        Format(buf, size, "frozen %08X", run.rngValue);
    } else {
        Format(buf, size, "off");
    }
}

/* Freezes the seed at its value now. The game thread sets it back at the start of each game frame. */
static void FreezeRng(int arg) {
    (void)arg;
    run.rngValue = rseed;
    run.rngFrozen ^= 1;
}

/* Freezes the seed if it is not frozen, and lets the memory view change the frozen value, digit by digit. */
static void EditRng(int arg) {
    (void)arg;
    if (!run.rngFrozen) {
        run.rngValue = rseed;
        run.rngFrozen = 1;
    }
    MemViewEdit(&run.rngValue);
    MenuShowView(&memoryView);
}

/* One step of the seed, as one call of the game's Random, or the seed of a level's start. Both act on the next
 * game frame, and on a frozen seed too. */
static void RngAction(int arg) {
    if (arg == 0) {
        if (run.rngCalls < 255) {
            run.rngCalls++;
        }
    } else {
        /* The level start replaces any steps not yet made. */
        run.rngCalls = 0;
        run.rngReset = 1;
    }
}

static const char *const fogNames[] = { "as the level", "off" };
static const char *const chaosNames[] = { "off", "rare", "sometimes", "often" };
static const char *const noclipSpeedNames[] = { "2", "4", "8", "16", "32", "64" };
static const char *const freeCamNames[] = { "off", "2", "4", "8", "16", "32" };

static void SetActionAsk(int arg) {
    SetActionRequest(arg);
}

static const MenuItem setActionItems[] = {
    CHOICE("Glove action", &run.setGlove, setActionGloveNames, SETACTION_GLOVE_COUNT),
    WARN_ACTION("Set glove action", SetActionAsk, 0, "Modified game! May crash"),
    CHOICE("Ball state", &run.setBall, setActionBallNames, SETACTION_BALL_COUNT),
    WARN_ACTION("Set ball state", SetActionAsk, 1, "Modified game! May crash"),
};
static MENU(setActionMenu, "Set action", setActionItems);

/* Lives and health change the run: the level is not kept as a practice best time. */
static void PlayerEdited(int arg) {
    (void)arg;
    BestTimesTaint(BT_OTHER);
}

#define NUMBER_TAINT(label, ptr, lo, hi) ITEM(label, ITEM_NUMBER, lo, hi, ptr, NULL, PlayerEdited, NULL, NULL, 0, -1)

static const MenuItem trainerItems[] = {
    STATE("L mash timing", TrainersToggle, TrainersText, TRAINER_LMASH),
    STATE("MBZ-L timing", TrainersToggle, TrainersText, TRAINER_MBZL),
    STATE("Line keeper", TrainersToggle, TrainersText, TRAINER_LINE),
    WARN_STATE("Moon Pound aim", TrainersToggle, TrainersText, TRAINER_MOON, "Can add lag: up to 4 ms a game frame"),
    STATE("Boss windows", TrainersToggle, TrainersText, TRAINER_BOSS),
    WARN_STATE("Wall clip", TrainersToggle, TrainersText, TRAINER_CLIP, "Can add lag: up to 3 ms a game frame"),
};
static MENU(trainerMenu, "Trainers", trainerItems);

static const MenuItem rngItems[] = {
    STATE("Freeze random numbers", FreezeRng, RngText, 0),
    ACTION("Random numbers: one call", RngAction, 0),
    ACTION("Random numbers: level start", RngAction, 1),
    ACTION("Random numbers: set (frozen)", EditRng, 0),
};
static MENU(rngMenu, "Random numbers", rngItems);

static const MenuItem pad2Items[] = {
    TOGGLE("Controller 2: debug controls", &settings.debugPad),
    WARN("Controller 2: L marks all done", &settings.debugPadL, "Edits your save! Needs debug controls"),
};
static MENU(pad2Menu, "Controller 2", pad2Items);

static const char *const cameraSpeedNames[] = { "normal", "fast", "faster" };

static const MenuItem moveItems[] = {
    WARN("Moon jump", &run.moonJump, "Modified game! Hold L (port 2, or stick up + L)"),
    WARN("Noclip", &run.noclip, "Modified game! Stick, D-pad up/down, R fast"),
    CHOICE("Noclip speed", &run.noclipSpeed, noclipSpeedNames, 6),
    CHOICE("Free camera", &run.freeCam, freeCamNames, 6),
    WARN_CHOICE("Camera speed", &run.cameraSpeed, cameraSpeedNames, 3, "Modified game! The camera turns the stick"),
};
static MENU(moveMenu, "Movement and camera", moveItems);

static const MenuItem gameItems[] = {
    SUBMENU("Cheats", cheatMenu),
    SUBMENU("Power-ups", pickupMenu),
    NUMBER_TAINT("Lives", LOW_BYTE(player.lives), 0, 9),
    NUMBER_TAINT("Health", LOW_BYTE(player.health), 1, 3),
    SUBMENU("Random numbers", rngMenu),
    STATE("Mute music", ActionsToggleMute, MuteText, 0),
    STATE("Mute sound effects", ActionsToggleMute, MuteText, 1),
    SUBMENU("Controller 2", pad2Menu),
    CHOICE("Fog", &run.fogOff, fogNames, 2),
    SUBMENU("Movement and camera", moveMenu),
    SUBMENU("Set action", setActionMenu),
    WARN_CHOICE("Chaos inputs", &run.chaos, chaosNames, 4, "Modified game! Off while recording"),
    STATE("Ghost", GhostToggle, GhostText, 0),
    SUBMENU("Trainers", trainerMenu),
};
static MENU(gameMenu, "Game", gameItems);

static void SettingsStatusText(char *buf, int size, int arg) {
    static const char *const text[] = { "", "saved", "loaded", "no EEPROM", "none saved" };
    (void)arg;
    Format(buf, size, "%s", text[settingsStatus < ARRAY_COUNT(text) ? settingsStatus : 0]);
}

static void SettingsAction(int arg) {
    if (arg == 0) {
        SettingsRequestSave();
    } else if (arg == 1) {
        SettingsRequestLoad();
    } else {
        SettingsDefault();
    }
}

static const char *const hiscoreNames[] = { "practice bests", "game records" };

static void BestTimesAction(int arg) {
    (void)arg;
    BestTimesClear();
    HudMessage("practice best times cleared");
}

static const char *const opacityNames[] = { "", "clear", "25%", "50%", "75%", "85%", "solid" };
static const char *const acrossNames[] = { "", "left", "centre", "right" };
static const char *const downNames[] = { "", "top", "middle", "low" };
static const char *const trainerLogNames[] = { "", "off", "5", "6", "8", "10" };
static const char *const trainerHoldNames[] = { "", "off", "1 s", "2 s", "3 s" };

static const MenuItem settingsItems[] = {
    ACTION("Save settings", SettingsAction, 0),
    ACTION("Load settings", SettingsAction, 1),
    ACTION("Default settings", SettingsAction, 2),
    INFO("Status", SettingsStatusText),
    TOGGLE("Pause game in this menu", &settings.menuPauses),
    ITEM("Menu opacity", ITEM_CHOICE, 1, MENU_OPACITY_STEPS, &settings.menuOpacity, NULL, NULL, opacityNames, NULL, 0, -1),
    ITEM("Menu position across", ITEM_CHOICE, 1, 3, &settings.menuAcross, NULL, NULL, acrossNames, NULL, 0, -1),
    ITEM("Menu position down", ITEM_CHOICE, 1, 3, &settings.menuDown, NULL, NULL, downNames, NULL, 0, -1),
    ITEM("Trainer log length", ITEM_CHOICE, 1, TRAINER_LOG_STEPS, &settings.trainerLog, NULL, NULL, trainerLogNames, NULL, 0, -1),
    ITEM("Trainer result hold", ITEM_CHOICE, 1, TRAINER_HOLD_STEPS, &settings.trainerHold, NULL, NULL, trainerHoldNames, NULL, 0, -1),
    CHOICE("Hi Scores screen shows", &bestTimesState.showGame, hiscoreNames, 2),
    WARN_ACTION("Clear practice best times", BestTimesAction, 0, "Forgets every practice best time"),
    WARN_ACTION("Save best times to card", CardAction, CARD_REQ_SAVE_BESTS, "Writes BESTS.GRB on the SD card"),
    WARN_ACTION("Load best times from card", CardAction, CARD_REQ_LOAD_BESTS, "Replaces the practice best times"),
    WARN_VIEW("Save file", saveEditView, "Edits your real save!"),
    VIEW("Controller Pak", cpakView),
};
static MENU(settingsMenu, "Settings", settingsItems);

static const char *const ballNames[] = { "Easy", "Normal" };

static void VersionText(char *buf, int size, int arg) {
    (void)arg;
    Format(buf, size, "%s", PRACTICE_VERSION);
}

extern const View actorView;
extern const View stateView;
extern const View frameLogView;
extern const View spawnView;

static const char *const collViewNames[] = { "off", "see-through", "on surfaces" };
static const char *const collRadiusNames[COLL_RADIUSES] = { "whole level", "1000", "500", "250" };
static const char *const collColourNames[COLL_COLOURS] = { "by terrain", "by kind", "by object" };
static const char *const collBackNames[COLL_BACKS] = { "faint", "hide", "same" };

static void CollStatsText(char *buf, int size, int arg) {
    (void)arg;
    if (!run.collView) {
        buf[0] = '\0';
    } else if (collStats.state != COLL_CACHE_READY) {
        Format(buf, size, "building");
    } else {
        Format(buf, size, "%d%s", collStats.faces, collStats.full ? " (full)" : "");
    }
}

/* The face filter: A shows or hides one kind of face (a bit of Run.collFaces). */
static void CollFaceToggle(int bit) {
    run.collFaces ^= 1 << bit;
}

static void CollFaceText(char *buf, int size, int bit) {
    Format(buf, size, "%s", run.collFaces & (1 << bit) ? "hidden" : "shown");
}

static const char *const regionNames[REGION_COUNT] = { "Glove actor", "Ball actor", "Level's actors", "Memory view range" };
static const char *const rangeNames[] = { "16 bytes", "256 bytes", "4096 bytes", "65536 bytes" };

/* The game thread does these on its next game frame, after the menu closes. */
static void CorruptAction(int arg) {
    CorruptRequest(arg);
    MenuClose();
}

static void PokesClearAction(int arg) {
    (void)arg;
    PokesClearAll();
}

static void PokesText(char *buf, int size, int arg) {
    int used = 0;

    (void)arg;
    for (int i = 0; i < POKE_COUNT; i++) {
        used += corrupt.pokes[i].address != 0;
    }
    Format(buf, size, "%d set, %d running", used, corrupt.active);
}

static void FlipText(char *buf, int size, int arg) {
    (void)arg;
    if (corrupt.flips == 0) {
        Format(buf, size, "none yet");
    } else {
        Format(buf, size, "%08X bit %d (%u)", corrupt.lastAddress, corrupt.lastBit, corrupt.flips);
    }
}

static const MenuItem corruptItems[] = {
    WARN_VIEW("Pokes", pokeView, "Modified game! May crash. Memory view: Z marks"),
    WARN_ACTION("Apply once pokes", CorruptAction, CORRUPT_APPLY, "Modified game! May crash"),
    CHOICE("Flip region", &corrupt.region, regionNames, REGION_COUNT),
    CHOICE("Range size", &corrupt.rangeSize, rangeNames, 4),
    WARN_ACTION("Flip one random bit", CorruptAction, CORRUPT_FLIP, "Modified game! May crash"),
    INFO("Pokes", PokesText),
    INFO("Last flip", FlipText),
    ACTION("Clear all pokes", PokesClearAction, 0),
};
static MENU(corruptMenu, "Memory corruption", corruptItems);

static const MenuItem collFaceItems[] = {
    STATE("Floors", CollFaceToggle, CollFaceText, 0),
    STATE("Walls", CollFaceToggle, CollFaceText, 1),
    STATE("Ceilings", CollFaceToggle, CollFaceText, 2),
    STATE("Water", CollFaceToggle, CollFaceText, 3),
    STATE("Slippery", CollFaceToggle, CollFaceText, 4),
    STATE("Low gravity", CollFaceToggle, CollFaceText, 5),
    STATE("Landscape", CollFaceToggle, CollFaceText, 6),
    STATE("Platforms", CollFaceToggle, CollFaceText, 7),
};
static MENU(collFaceMenu, "Faces shown", collFaceItems);

static const MenuItem collItems[] = {
    CHOICE("Collision view", &run.collView, collViewNames, COLLVIEW_MODES),
    CHOICE("Radius", &run.collRadius, collRadiusNames, COLL_RADIUSES),
    CHOICE("Colours", &run.collColour, collColourNames, COLL_COLOURS),
    CHOICE("Back faces", &run.collBack, collBackNames, COLL_BACKS),
    SUBMENU("Faces shown", collFaceMenu),
    INFO("Faces drawn", CollStatsText),
    TOGGLE("Collision spheres", &run.collSpheres),
    TOGGLE("Enemy collision spheres", &run.collEnemies),
    TOGGLE("Open collision edges", &run.collEdges),
    WARN("Moon Pound holes", &run.collHoles, "Can add lag: up to 3 ms a game frame"),
};
static MENU(collMenu, "Collision view", collItems);

static const MenuItem inspectItems[] = {
    VIEW("Actors", actorView),
    VIEW("Memory", memoryView),
    VIEW("Glove and ball", stateView),
    VIEW("Frame log", frameLogView),
    TOGGLE("Frame log records", &run.frameLog),
    TOGGLE("Show the watch list", &settings.hud[HUD_WATCH].on),
    SUBMENU("Collision view", collMenu),
    WARN_VIEW("Spawn actor", spawnView, "Modified game! May crash. Edits your save"),
    WARN("Show missed taps", &run.missedTaps, "Timing changes! Needs the input display"),
    SUBMENU("Memory corruption", corruptMenu),
};
static MENU(inspectMenu, "Inspect", inspectItems);

static const MenuItem rootItems[] = {
    SUBMENU("Warp", warpMenu),
    SUBMENU("Savestate", stateMenu),
    SUBMENU("HUD", hudMenu),
    SUBMENU("Frame advance", timeMenu),
    SUBMENU("Game", gameMenu),
    SUBMENU("Inspect", inspectMenu),
    CHOICE("Ball controls", &settings.ballNormal, ballNames, 2),
    SUBMENU("Bindings", bindMenu),
    SUBMENU("Settings", settingsMenu),
    INFO("Version", VersionText),
};
MENU(rootMenu, "Glover Practice", rootItems);

void MenusInit(void) {
    for (int i = 0; i < SPLITS_MAX; i++) {
        Format(splitLabels[i], sizeof(splitLabels[i]), "%d", i + 1);
        splitItems[i] = (MenuItem)INFO(splitLabels[i], SplitText);
        splitItems[i].arg = i;
    }
    for (int i = 0; i < WARP_LEVELS; i++) {
        levelNames[i] = levelInfo[i].name;
    }
    for (int i = 0; i < DEMO_COUNT; i++) {
        demoNames[i] = levelInfo[demoLevels[i]].name;
    }
}
