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
#include "practice.h"
#include "recording.h"
#include "saveedit.h"
#include "settings.h"

Input input;
Press presses[PRESS_HISTORY];

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

/* The last presses of controller 1, newest first: the game frame of the press and how long it is held. */
static void History(void) {
    for (int i = 0; i < PRESS_HISTORY; i++) {
        if (presses[i].bit != 0 && (input.held & presses[i].bit) && presses[i].holding) {
            presses[i].frames++;
        } else {
            presses[i].holding = 0;
        }
    }
    for (int b = 0; b < 14; b++) {
        if (input.pressed & padBits[b]) {
            for (int i = PRESS_HISTORY - 1; i > 0; i--) {
                presses[i] = presses[i - 1];
            }
            presses[0].bit = padBits[b];
            presses[0].frame = frameCount;
            presses[0].frames = 1;
            presses[0].holding = 1;
        }
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
        if (run.chaos) {
            ChaosInput(cntlr);
        }
    }
    if (demo.all && (controllerdata[cntlr].button & (CONT_A | CONT_B | CONT_START))) {
        /* The game leaves a demo on these; Play all stops with it. */
        demo.all = 0;
    }
    RecordingFrame(cntlr);
    read(cntlr);
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
}

void PracticeReadController(u8 cntlr) {
    Frame(cntlr, ReadController);
}

/* During a demo the game plays a recording instead. The player's own buttons still open the menu; while it is open
 * the demo sees no buttons, which would end it. */
void PracticeReadControllerFromData(u8 cntlr) {
    Frame(cntlr, DemoRead);
}
