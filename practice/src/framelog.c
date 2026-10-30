/* The frame log and its view (Inspect, Frame log): the last game frames of the glove, newest at the bottom. Up
 * and down scroll, Z clears, B goes back. */
#include "actions.h"
#include "fmt.h"
#include "framelog.h"
#include "game.h"
#include "hud.h"
#include "input.h"
#include "menu.h"
#include "safe.h"
#include "text.h"

#define VIEW_X 8
#define VIEW_Y 24
#define VIEW_W 304
#define ROW_H 10
#define ROWS 16

FrameLog frameLog;
volatile u8 frameLogReads;

static u8 scrollBack; /* entries above the newest */

void FrameLogClear(void) {
    frameLog.count = 0;
    scrollBack = 0;
}

static u16 Bits14(const u8 *flags) {
    u16 bits = 0;

    for (int b = 0; b < 14; b++) {
        if (flags[b]) {
            bits |= padBits[b];
        }
    }
    return bits;
}

void FrameLogTick(void) {
    const Pad *p = &pad[ActiveController];
    FrameLogEntry *e = &frameLog.entries[frameLog.count % FRAME_LOG_SIZE];

    e->frame = frameLog.count;
    e->held = Bits14(p->button);
    e->pressed = Bits14(p->pressed);
    e->latched = Bits14(p->latch);
    e->mode = SafePointer(hand.state) ? hand.state->mode : -1;
    e->actionTime = handControl.actionTime;
    e->actionLock = handControl.actionLock;
    e->noControl = handControl.noControlTimer;
    e->reads = frameLogReads;
    frameLogReads = 0;
    frameLog.count++;
}

static void LogOpen(void) {
    scrollBack = 0;
}

static int LogInput(u16 pressed, u16 held) {
    u32 kept = frameLog.count < FRAME_LOG_SIZE ? frameLog.count : FRAME_LOG_SIZE;
    (void)held;

    if (pressed & CONT_B) {
        return 0;
    }
    if (pressed & CONT_Z) {
        FrameLogClear();
    }
    if ((pressed & CONT_UP) && (u32)(scrollBack + ROWS) < kept) {
        scrollBack++;
    }
    if ((pressed & CONT_DOWN) && scrollBack > 0) {
        scrollBack--;
    }
    if (pressed & CONT_L) {
        run.frameLog ^= 1;
    }
    return 1;
}

/* The buttons in a fixed order, one column each: A B Z Start L R, the D-pad, the C buttons. */
static const struct {
    u16 bit;
    char c;
} columns[] = {
    { CONT_A, 'A' },  { CONT_B, 'B' },     { CONT_Z, 'Z' },     { CONT_START, 'S' }, { CONT_L, 'L' },
    { CONT_R, 'R' },  { CONT_UP, '^' },    { CONT_DOWN, 'v' },  { CONT_LEFT, '<' },  { CONT_RIGHT, '>' },
    { CONT_CUP, '^' }, { CONT_CDOWN, 'v' }, { CONT_CLEFT, '<' }, { CONT_CRIGHT, '>' },
};

/* The columns of BITS as letters, the others as spaces (which draw nothing). */
static void ButtonText(char *buf, u16 bits) {
    for (u32 i = 0; i < ARRAY_COUNT(columns); i++) {
        buf[i] = (bits & columns[i].bit) ? columns[i].c : ' ';
    }
    buf[ARRAY_COUNT(columns)] = '\0';
}

static void LogDraw(Gfx **dl) {
    u32 kept = frameLog.count < FRAME_LOG_SIZE ? frameLog.count : FRAME_LOG_SIZE;
    char buf[48];
    int bx = VIEW_X + 6 + 6 * 6;

    TextRect(dl, VIEW_X, VIEW_Y, VIEW_X + VIEW_W, VIEW_Y + 16 + (ROWS + 3) * ROW_H, MenuPanelColor(0));
    TextRect(dl, VIEW_X, VIEW_Y, VIEW_X + VIEW_W, VIEW_Y + 13, MenuPanelColor(1));
    Format(buf, sizeof(buf), "Frame log  %s", run.frameLog ? "recording" : "off");
    TextPrint(dl, VIEW_X + 6, VIEW_Y + 3, 1, 0xFFE070FF, buf);
    /* The column heads: the D-pad and then the C buttons as arrows. */
    TextPrint(dl, VIEW_X + 6, VIEW_Y + 16, 1, 0x8090A8FF, "frame");
    TextPrint(dl, bx, VIEW_Y + 16, 1, 0x8090A8FF, "ABZSLR^v<>^v<>");
    TextPrint(dl, bx + 15 * 6, VIEW_Y + 16, 1, 0x8090A8FF, "action      time");
    for (int r = 0; r < ROWS && (u32)r < kept; r++) {
        /* The newest entry at the bottom row, less the scroll. */
        u32 age = (u32)(kept < ROWS ? kept : ROWS) - 1 - r + scrollBack;
        if (age >= kept) {
            continue;
        }
        const FrameLogEntry *e = &frameLog.entries[(frameLog.count - 1 - age) % FRAME_LOG_SIZE];
        int y = VIEW_Y + 16 + (r + 1) * ROW_H;
        int x = VIEW_X + 6;
        u32 dim = e->noControl ? 0x707888FF : 0xFFFFFFFF;

        Format(buf, sizeof(buf), "%5u", e->frame);
        TextPrint(dl, x, y, 1, 0xB0B8C8FF, buf);
        /* Held buttons, then the presses in yellow and the latched ones in grey over them. */
        ButtonText(buf, e->held & ~e->latched);
        TextPrint(dl, bx, y, 1, dim, buf);
        ButtonText(buf, e->pressed);
        TextPrint(dl, bx, y, 1, 0xFFE070FF, buf);
        ButtonText(buf, e->latched);
        TextPrint(dl, bx, y, 1, 0x707888FF, buf);
        int n = Format(buf, sizeof(buf), "%-11s%3d", HandActionName(e->mode), e->actionTime);
        if (e->actionLock) {
            n += Format(buf + n, sizeof(buf) - n, " lock%d", e->actionLock);
        }
        if (e->noControl) {
            n += Format(buf + n, sizeof(buf) - n, " nc%d", e->noControl);
        }
        if (e->reads != 1) {
            Format(buf + n, sizeof(buf) - n, " r%d", e->reads);
        }
        TextPrint(dl, bx + 15 * 6, y, 1, dim, buf);
    }
    TextPrint(dl, VIEW_X + 6, VIEW_Y + 16 + (ROWS + 2) * ROW_H - 4, 1, 0x8090A8FF,
              "^v scroll  L on/off  Z clear  B back");
}

const View frameLogView = { LogOpen, LogInput, LogDraw };
