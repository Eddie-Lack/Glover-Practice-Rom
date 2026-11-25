/* The practice input layer. The controller thread reads the controllers, then calls PracticeReadController in
 * place of the game's ReadController. It updates the practice view of the controllers, runs the menu and the
 * bindings, and takes practice inputs out of what the game sees. With no practice input, the game's data is
 * untouched. */
#include "actions.h"
#include "bindings.h"
#include "chaos.h"
#include "cpak.h"
#include "demo.h"
#include "framelog.h"
#include "freecam.h"
#include "game.h"
#include "hud.h"
#include "input.h"
#include "menu.h"
#include "missedtaps.h"
#include "noclip.h"
#include "paddisplay.h"
#include "perf.h"
#include "practice.h"
#include "recording.h"
#include "saveedit.h"
#include "settings.h"

Input input;
Press presses[PRESS_HISTORY];
static s32 chordStart; /* frameCount2 when a C button was first held (Frame) */
/* The stick counts as let go inside this range. */
#define STICK_CENTRE 16

/* The game's buttonFlag order: the bit of each pad[].button[] index. */
const u16 padBits[14] = {
    CONT_A, CONT_B, CONT_Z, CONT_START, CONT_L, CONT_R, CONT_LEFT, CONT_RIGHT, CONT_UP, CONT_DOWN,
    CONT_CLEFT, CONT_CRIGHT, CONT_CUP, CONT_CDOWN,
};

void InputHideHeld(void) {
    input.hidden |= input.held;
}

static void Update(void) {
    u16 held = controllerdata[0].button;
    u16 held2 = controllerdata[1].button;

    input.pressed = held & ~input.held;
    input.held = held;
    input.stickX = controllerdata[0].stickX;
    input.stickY = controllerdata[0].stickY;
    input.pressed2 = held2 & ~input.held2;
    input.held2 = held2;
    input.hidden &= held;
}

/* The last presses of controller 1 that the game sees, newest first: the in-game timer's frame of the press (the
 * timer's "f") and how long it is held. Nothing is noted while the menu is open, and the menu's chord of the four C
 * buttons is taken out again: the history keeps the presses from before the menu. */
static void History(void) {
    /* The history, and as many older presses: the C presses of a chord push presses out of presses[] for a while. */
    static Press log[PRESS_HISTORY * 2];
    const int count = PRESS_HISTORY * 2;

    if ((input.held & CONT_C_ALL) == CONT_C_ALL) {
        /* The chord. The speedrun never holds all four C buttons, so the C presses that are still held are its
         * part. */
        int n = 0;
        for (int i = 0; i < count; i++) {
            if (!(log[i].holding && (log[i].bit & CONT_C_ALL))) {
                log[n++] = log[i];
            }
        }
        for (; n < count; n++) {
            log[n].bit = 0;
        }
    }
    if (menuOpen || (input.held & CONT_C_ALL) == CONT_C_ALL) {
        for (int i = 0; i < count; i++) {
            log[i].holding = 0;
        }
    } else {
        for (int i = 0; i < count; i++) {
            if (log[i].bit != 0 && (input.held & log[i].bit) && log[i].holding) {
                log[i].frames++;
            } else {
                log[i].holding = 0;
            }
        }
        for (int b = 0; b < 14; b++) {
            if (input.pressed & padBits[b]) {
                for (int i = count - 1; i > 0; i--) {
                    log[i] = log[i - 1];
                }
                log[0].bit = padBits[b];
                log[0].frame = player.levelFrames;
                log[0].frames = 1;
                log[0].holding = 1;
            }
        }
    }
    for (int i = 0; i < PRESS_HISTORY; i++) {
        presses[i] = log[i];
    }
}

/* The game's debug controls read pad[4], which the game never fills (docs/game.md in gloveworld). This fills it
 * from controller 2: held buttons, presses and the stick. L is left out unless the Game menu's L switch is on: in
 * ProcessInputPersonal it marks every level and garib of the save slot as done, and the game may save that to the
 * player's real save. */
static void DebugPad(void) {
    Pad *p = &pad[4];
    u16 held = FreeCamOn() ? input.held2 & ~FREECAM_BUTTONS : input.held2;

    /* The moon jump button is L on controller 2. */
    if (!settings.debugPadL || run.moonJump) {
        held &= ~CONT_L;
    }

    for (int b = 0; b < 14; b++) {
        p->prevButton[b] = p->button[b];
        p->button[b] = (held & padBits[b]) != 0;
        p->pressed[b] = p->button[b] && !p->prevButton[b];
        p->holdTime[b] = p->button[b] == p->prevButton[b] ? p->holdTime[b] + 1 : 0;
    }
    p->stickX = FreeCamOn() ? 0 : controllerdata[1].stickX;
    p->stickY = FreeCamOn() ? 0 : controllerdata[1].stickY;
}

static void Frame(u8 cntlr, void (*read)(u8)) {
    u32 t0 = osGetCount();

    if (run.frameLog) {
        frameLogReads++;
    } else if (frameLogReads) {
        /* The first entry after the log is turned on counts from then. */
        frameLogReads = 0;
    }
    Update();
    if (run.missedTaps | missedTaps.active) {
        MissedTapsRead(controllerdata[0].button);
    }
    History();
    /* The game frame on which a C button was first held after none was: the menu's chord may have let the game see
     * C presses before its last button came (ActionsChordRepair). */
    if ((input.held & CONT_C_ALL) && !((input.held & ~input.pressed) & CONT_C_ALL)) {
        chordStart = frameCount2;
    }
    if (BindingsCapturing()) {
        /* A new binding is being chosen: nothing reaches the menu or the game. */
    } else if ((input.held & CONT_C_ALL) == CONT_C_ALL && (input.pressed & CONT_C_ALL)) {
        /* All four C buttons open and close the menu. The speedrun never holds them together. */
        if (menuOpen) {
            /* A Controller Pak operation that runs needs the menu: a load applies its settings there. */
            if (!cpak.working) {
                MenuClose();
            }
        } else {
            if ((input.held & ~input.pressed) & CONT_C_ALL) {
                /* Some C buttons came a read early and the game acted on them. */
                ActionsChordRepair(chordStart);
            }
            MenuOpen();
        }
        InputHideHeld();
    } else if (menuOpen) {
        MenuInput(input.pressed, input.held);
        if (!menuOpen) {
            InputHideHeld();
        }
    } else if (!NoclipFlying()) {
        BindingsCheck();
    }
    if (menuOpen) {
        controllerdata[cntlr].button = 0;
        controllerdata[cntlr].stickX = 0;
        controllerdata[cntlr].stickY = 0;
    } else {
        if (run.moonJump | run.noclip) {
            NoclipHide(cntlr);
        }
        controllerdata[cntlr].button &= ~input.hidden;
        if (input.stickHidden) {
            /* A stick binding fired: the game does not walk on it until the stick is let go. */
            if (input.stickX > -STICK_CENTRE && input.stickX < STICK_CENTRE && input.stickY > -STICK_CENTRE &&
                input.stickY < STICK_CENTRE) {
                input.stickHidden = 0;
            } else {
                controllerdata[cntlr].stickX = 0;
                controllerdata[cntlr].stickY = 0;
            }
        }
        if (run.chaos) {
            ChaosInput(cntlr);
        }
    }
    if (demo.all && (controllerdata[cntlr].button & (CONT_A | CONT_B | CONT_START))) {
        /* The game leaves a demo on these; Play all stops with it. */
        demo.all = 0;
    }
    RecordingFrame(cntlr);
    u32 t1 = osGetCount();
    read(cntlr);
    u32 t2 = osGetCount();
    if (run.freeCam | freeCam.active) {
        FreeCamRead();
    }
    if (settings.debugPad) {
        DebugPad();
    }
    if (run.padDisplay == 2) {
        PadDisplayRead(input.held2, controllerdata[1].stickX, controllerdata[1].stickY);
    }
    SettingsService();
    SaveEditService();
    CPakService();
    PerfAdd(PERF_CTRL, t1 - t0 + (osGetCount() - t2));
}

void PracticeReadController(u8 cntlr) {
    Frame(cntlr, ReadController);
}

/* During a demo the game plays a recording instead. The player's own buttons still open the menu; while it is open
 * the demo sees no buttons, which would end it. */
void PracticeReadControllerFromData(u8 cntlr) {
    Frame(cntlr, DemoRead);
}
