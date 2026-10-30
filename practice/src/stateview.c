/* The glove and ball view: the timers, counters and contacts that decide the glove's and the ball's next moves,
 * live. Left and right change the page, B goes back. The offsets are those of gloveworld's include/hand.h
 * (HandControl), include/ball.h (BallControl) and include/actor.h (ActorObject). */
#include "fmt.h"
#include "game.h"
#include "hud.h"
#include "menu.h"
#include "safe.h"
#include "text.h"

#define VIEW_X 16
#define VIEW_Y 28
#define VIEW_W 288
#define ROW_H 10
#define ROWS 16

enum FieldKind { S8, U8, S16, S32, F32 };

typedef struct Field {
    const char *label;
    u16 offset;
    u8 kind;
} Field;

static const Field gloveFields[] = {
    { "action time", 0x1A, S16 },   { "previous action", 0x18, S16 }, { "action lock", 0x08, S16 },
    { "action delay", 0x0A, S16 },  { "no control", 0x24, S16 },     { "gait", 0x1C, S8 },
    { "status", 0x1E, S16 },        { "idle time", 0x28, S16 },      { "walk time", 0x2E, S16 },
    { "fall time", 0x30, S16 },     { "ground time", 0x534, S32 },  { "wall push time", 0x4BC, S16 },
    { "double jumped", 0x34, S32 }, { "bounces", 0x2A, U8 },        { "release timer", 0x2B, U8 },
    { "join timer", 0x50C, S16 },   { "drop timer", 0x50A, S16 },   { "steep slide", 0x538, S16 },
};

static const Field ballFields[] = {
    { "state", 0x1E, S8 },        { "release timer", 0x16, S16 }, { "fall time", 0x18, S16 },
    { "return time", 0x1A, S16 }, { "lose timer", 0x00, S32 },     { "change time", 0x11C, S32 },
    { "hit flags", 0x10C, S32 },
};

static u8 statePage;

static void Open(void) {
}

static int Input(u16 pressed, u16 held) {
    (void)held;
    if (pressed & CONT_B) {
        return 0;
    }
    if (pressed & (CONT_LEFT | CONT_RIGHT)) {
        statePage ^= 1;
    }
    return 1;
}

static void Line(Gfx **dl, int col, int row, u32 color, const char *fmt, ...) {
    char buf[64];
    va_list args;

    va_start(args, fmt);
    FormatV(buf, sizeof(buf), fmt, args);
    va_end(args);
    TextPrint(dl, VIEW_X + 6 + col * (VIEW_W / 2), VIEW_Y + 16 + row * ROW_H, 1, color, buf);
}

static void DrawField(Gfx **dl, int col, int row, const u8 *base, const Field *f) {
    const u8 *p = base + f->offset;

    switch (f->kind) {
        case S8:
            Line(dl, col, row, 0xFFFFFFFF, "%-15s %d", f->label, *(const s8 *)p);
            break;
        case U8:
            Line(dl, col, row, 0xFFFFFFFF, "%-15s %d", f->label, *p);
            break;
        case S16:
            Line(dl, col, row, 0xFFFFFFFF, "%-15s %d", f->label, *(const s16 *)p);
            break;
        case S32:
            Line(dl, col, row, 0xFFFFFFFF, "%-15s %d", f->label, *(const s32 *)p);
            break;
        case F32:
            if (SafeFloat((const f32 *)p)) {
                Line(dl, col, row, 0xFFFFFFFF, "%-15s %.2f", f->label, (double)*(const f32 *)p);
            }
            break;
    }
}

/* The faces that an actor touched on the last game frame, by kind: floor, slope, wall or ceiling (as the
 * collision view colours them). */
static void DrawContacts(Gfx **dl, int row, const char *name, const Actor *a) {
    const ActorObject *o = a->obj;
    char buf[64];
    int n = 0;

    if (!SafePointer(o)) {
        return;
    }
    n += Format(buf + n, sizeof(buf) - n, "%s %s:", name, o->onGround ? "ground" : "air");
    for (int k = 0; k < 4; k++) {
        const CollFace *f = o->planes[k];
        const char *kind = "?";
        if (!SafePointer(f)) {
            continue;
        }
        if (SafeFloat(&f->normal[1])) {
            f32 y = f->normal[1];
            kind = y >= 0.75f ? "floor" : y >= 0.3f ? "slope" : y > -0.3f ? "wall" : "ceiling";
        }
        n += Format(buf + n, sizeof(buf) - n, " %s", kind);
    }
    Line(dl, 0, row, 0xFFE070FF, "%s", buf);
}

static void Draw(Gfx **dl) {
    TextRect(dl, VIEW_X, VIEW_Y, VIEW_X + VIEW_W, VIEW_Y + 20 + (ROWS + 1) * ROW_H, MenuPanelColor(0));
    TextRect(dl, VIEW_X, VIEW_Y, VIEW_X + VIEW_W, VIEW_Y + 13, MenuPanelColor(1));
    TextPrint(dl, VIEW_X + 6, VIEW_Y + 3, 1, 0xFFE070FF, statePage == 0 ? "Glove  1/2" : "Ball  2/2");
    if (statePage == 0) {
        int mode = SafePointer(hand.state) ? hand.state->mode : -1;
        Line(dl, 0, 0, 0xFFE070FF, "action %s (%d)", HandActionName(mode), mode);
        for (u32 i = 0; i < ARRAY_COUNT(gloveFields); i++) {
            DrawField(dl, i % 2, 1 + i / 2, (const u8 *)&handControl, &gloveFields[i]);
        }
        DrawContacts(dl, 2 + ARRAY_COUNT(gloveFields) / 2, "glove", &hand);
    } else {
        Line(dl, 0, 0, 0xFFE070FF, "type %d", ballControl.type);
        for (u32 i = 0; i < ARRAY_COUNT(ballFields); i++) {
            DrawField(dl, i % 2, 1 + i / 2, (const u8 *)&ballControl, &ballFields[i]);
        }
        DrawContacts(dl, 2 + ARRAY_COUNT(ballFields) / 2, "ball", &ball);
    }
    Line(dl, 0, ROWS, 0x8090A8FF, "</> page  B back");
}

const View stateView = { Open, Input, Draw };
