/* The quick action bindings. */
#include "actions.h"
#include "bindings.h"
#include "fmt.h"
#include "freecam.h"
#include "game.h"
#include "input.h"
#include "settings.h"

/* A stick counts as held in a direction past this value (the game's stick reaches about 80). */
#define STICK_FULL 60
#define CAPTURE_FRAMES 100

static s8 capturing = -1;
static u8 captureFrames;
static u8 captureStarted;
static u16 captureButtons;
static u8 captureWhere;

_Static_assert(ACTION_COUNT <= BIND_SLOTS, "every action needs a place in the settings");

static int Where(int action) {
    return (settings.bindWhere[action / 2] >> (action % 2 * 4)) & 0xF;
}

static void SetBinding(int action, u16 buttons, int where) {
    int shift = action % 2 * 4;

    settings.bindButtons[action] = buttons;
    settings.bindWhere[action / 2] = (settings.bindWhere[action / 2] & ~(0xF << shift)) | (where << shift);
}

static void Bind(int stick, u16 buttons, int action) {
    SetBinding(action, buttons, stick << BIND_STICK_SHIFT);
}

/* Candidates for the owner to try (docs/plan.md, "Input"): the stick held fully up or down, with the D-pad. */
void BindingsDefault(void) {
    for (int i = 0; i < BIND_SLOTS; i++) {
        SetBinding(i, 0, 0);
    }
    Bind(STICK_UP, CONT_UP, ACTION_RESET);
    Bind(STICK_UP, CONT_RIGHT, ACTION_CHECKPOINT);
    Bind(STICK_UP, CONT_DOWN, ACTION_WARP);
    Bind(STICK_UP, CONT_LEFT, ACTION_HUD);
    Bind(STICK_DOWN, CONT_UP, ACTION_FRAME_ADVANCE);
    Bind(STICK_DOWN, CONT_RIGHT, ACTION_FRAME_STEP);
    Bind(STICK_DOWN, CONT_LEFT, ACTION_SLOW_MOTION);
    Bind(STICK_LEFT, CONT_UP, ACTION_SAVE_STATE);
    Bind(STICK_LEFT, CONT_DOWN, ACTION_LOAD_STATE);
    Bind(STICK_LEFT, CONT_LEFT, ACTION_RECORD);
    Bind(STICK_LEFT, CONT_RIGHT, ACTION_PLAY);
}

static int StickDir(void) {
    if (input.stickY >= STICK_FULL) {
        return STICK_UP;
    }
    if (input.stickY <= -STICK_FULL) {
        return STICK_DOWN;
    }
    if (input.stickX <= -STICK_FULL) {
        return STICK_LEFT;
    }
    if (input.stickX >= STICK_FULL) {
        return STICK_RIGHT;
    }
    return STICK_NONE;
}

void BindingsCheck(void) {
    int stick = StickDir();

    for (int a = 0; a < ACTION_COUNT; a++) {
        u16 mask = settings.bindButtons[a];
        u8 where = Where(a);
        int port2 = where & BIND_PORT2;
        u16 held = port2 ? input.held2 : input.held;
        u16 pressed = port2 ? input.pressed2 : input.pressed;

        /* A binding that uses a button of the free camera does not fire while the camera is on. */
        if (port2 && FreeCamOn() && (mask & FREECAM_BUTTONS)) {
            continue;
        }
        if (mask == 0 || (held & mask) != mask || !(pressed & mask)) {
            continue;
        }
        if (!port2 && (where >> BIND_STICK_SHIFT) != STICK_NONE && (where >> BIND_STICK_SHIFT) != stick) {
            continue;
        }
        ActionRequest(a);
        if (!port2) {
            input.hidden |= mask;
        }
    }
}

int BindingsCapturing(void) {
    u16 held = input.held | input.held2;

    if (capturing < 0) {
        return 0;
    }
    if (++captureFrames > CAPTURE_FRAMES && !captureStarted) {
        capturing = -1;
        return 0;
    }
    if (!captureStarted) {
        /* Waits for a press after everything is let go. */
        if ((input.pressed | input.pressed2) && captureFrames > 2) {
            captureStarted = 1;
            captureButtons = 0;
            captureWhere = input.held ? StickDir() << BIND_STICK_SHIFT : BIND_PORT2;
        }
    }
    if (captureStarted) {
        captureButtons |= (captureWhere & BIND_PORT2) ? input.held2 : input.held;
        if (held == 0) {
            SetBinding(capturing, captureButtons, captureWhere);
            capturing = -1;
            InputHideHeld();
        }
    }
    input.hidden |= input.held;
    return 1;
}

void BindingCapture(int action) {
    capturing = action;
    captureFrames = 0;
    captureStarted = 0;
}

void BindingClear(int action) {
    SetBinding(action, 0, 0);
}

static const struct {
    u16 bit;
    char name[3];
} buttonNames[] = {
    { CONT_A, "A" },     { CONT_B, "B" },     { CONT_Z, "Z" },      { CONT_START, "S" },  { CONT_L, "L" },
    { CONT_R, "R" },     { CONT_UP, "D^" },   { CONT_DOWN, "Dv" },  { CONT_LEFT, "D<" }, { CONT_RIGHT, "D>" },
    { CONT_CUP, "C^" },  { CONT_CDOWN, "Cv" }, { CONT_CLEFT, "C<" }, { CONT_CRIGHT, "C>" },
};

void BindingText(int action, char *buf, int size) {
    static const char *const sticks[] = { "", "St^ ", "Stv ", "St< ", "St> " };
    u16 mask = settings.bindButtons[action];
    u8 where = Where(action);
    int n = 0;

    if (capturing == action) {
        Format(buf, size, captureStarted ? "..." : "press");
        return;
    }
    if (mask == 0) {
        Format(buf, size, "-");
        return;
    }
    n += Format(buf + n, size - n, "%s", (where & BIND_PORT2) ? "P2 " : sticks[(where >> BIND_STICK_SHIFT) % 5]);
    for (u32 i = 0; i < ARRAY_COUNT(buttonNames) && n < size - 1; i++) {
        if (mask & buttonNames[i].bit) {
            n += Format(buf + n, size - n, "%s%s", n > 0 && buf[n - 1] != ' ' ? "+" : "", buttonNames[i].name);
        }
    }
}
