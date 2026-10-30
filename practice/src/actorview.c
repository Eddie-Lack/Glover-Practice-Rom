/* The actor list: every actor of the level, and a page with one actor's details. Up and down choose, A opens the
 * details, B goes back. Left and right on the details page step through the actors, A moves Glover there, and Z
 * brings the actor to Glover (spawn.h). Start on the details page edits the actor: up and down choose a field
 * (position, speed, rotation, the state's hits, damage and mode), left and right change it by a step, C up and
 * down change the step. */
#include "actions.h"
#include "besttimes.h"
#include "fmt.h"
#include "game.h"
#include "menu.h"
#include "safe.h"
#include "spawn.h"
#include "text.h"

#define VIEW_X 16
#define VIEW_Y 28
#define VIEW_W 288
#define ROWS 15
#define ROW_H 10
#define MAX_ACTORS 400

static const char *const typeNames[] = {
    "camera", "hand", "ball", "debris", "bullet", "sneezy", "?6", "bovva", "cannonball", "samtex", "mallet",
    "generalwu", "lionfish", "chester", "keg", "reggie", "swish", "knight", "robes", "fumble", "mike", "raptor",
    "crumpet", "tracey", "yoofow", "opec", "cymon", "sucker", "bugle", "denis", "chuck", "hubchicken", "frankie",
    "kloset", "flannel", "selwyn whale", "selwyn crab", "selwyn fish", "robot", "evil robot", "spank",
    "baby spank", "evil glove", "dibber", "fly", "snail", "bigfish", "littlefish", "ladybird", "weevil",
    "caterpillar", "butterfly", "spider", "bat", "frog", "dragfly", "?56", "?57", "nme frog", "platform",
    "landscape", "collectable", "water", "cameo",
};

/* The edit fields: nine floats at these offsets in the actor (position, speed, rotation), then the state's hits,
 * damage and mode. */
#define FIELDS 12
#define FIRST_STATE_FIELD 9
static const u8 floatOffsets[FIRST_STATE_FIELD] = { 0x34, 0x38, 0x3C, 0x58, 0x5C, 0x60, 0x78, 0x7C, 0x80 };
static const char *const fieldNames[FIELDS] = { "pos x", "pos y", "pos z", "vel x", "vel y", "vel z",
                                                 "rot x", "rot y", "rot z", "hits", "damage", "mode" };
static const f32 steps[] = { 0.01f, 0.1f, 1.0f, 10.0f, 100.0f };

static int selected;
static int top;
static u8 details;
static u8 editing;
static u8 field;
static u8 stepIndex = 2;

/* The edit field as text: a float safe for the FPU, or an integer of the state. */
static void FieldText(const Actor *a, char *buf, int size) {
    if (field < FIRST_STATE_FIELD) {
        const f32 *p = (const f32 *)((const u8 *)a + floatOffsets[field]);
        Format(buf, size, SafeFloat(p) ? "%.2f" : "?", (double)*p);
    } else if (SafePointer(a->state)) {
        Format(buf, size, "%d", field == 9 ? a->state->hits : field == 10 ? a->state->damage : a->state->mode);
    } else {
        Format(buf, size, "no state");
    }
}

/* Changes the edit field by one step, up (DIR 1) or down. The game thread is waiting while the menu is open. */
static void FieldChange(Actor *a, int dir) {
    f32 step = steps[stepIndex] * dir;
    int whole = step < 0 ? (int)(step - 0.5f) : (int)(step + 0.5f);

    if (whole == 0) {
        whole = dir;
    }
    if (field < FIRST_STATE_FIELD) {
        f32 *p = (f32 *)((u8 *)a + floatOffsets[field]);
        *p = SafeFloat(p) ? *p + step : 0.0f;
        if (!SafeFloat(p)) {
            *p = 0.0f;
        }
    } else if (SafePointer(a->state)) {
        if (field == 9) {
            a->state->hits += whole;
        } else if (field == 10) {
            a->state->damage += whole;
        } else {
            a->state->mode += whole;
        }
    } else {
        return;
    }
    ActorsModified();
    BestTimesTaint(BT_ACTORS);
}

static void Open(void) {
    selected = 0;
    top = 0;
    details = 0;
    editing = 0;
}

/* Actor number i of the list, or NULL. The list can change while the game runs with the menu open, so the walk
 * stops at MAX_ACTORS. */
static Actor *ActorAt(int i) {
    Actor *a = actorList.head.next;

    for (int n = 0; SafePointer(a) && a != &actorList.head && n < MAX_ACTORS; n++, a = a->next) {
        if (n == i) {
            return a;
        }
    }
    return NULL;
}

static int ActorCount(void) {
    int n = 0;

    for (Actor *a = actorList.head.next; SafePointer(a) && a != &actorList.head && n < MAX_ACTORS; a = a->next) {
        n++;
    }
    return n;
}

const char *ActorTypeName(int type) {
    return type >= 0 && type < (int)ARRAY_COUNT(typeNames) ? typeNames[type] : "?";
}

static const char *TypeName(const Actor *a) {
    return ActorTypeName(a->type);
}

/* The model's name, which fills 8 bytes with no terminating zero when it is that long. */
static void ObjectName(const Actor *a, char *buf) {
    int i = 0;

    if (SafePointer(a->inst) && SafePointer(a->inst->obj)) {
        for (; i < 8 && a->inst->obj->name[i] != '\0'; i++) {
            buf[i] = a->inst->obj->name[i];
        }
    }
    buf[i] = '\0';
}

static int Input(u16 pressed, u16 held) {
    int count = ActorCount();
    (void)held;

    if (pressed & CONT_B) {
        if (details) {
            details = editing = 0;
            return 1;
        }
        return 0;
    }
    if (details && (pressed & CONT_START) && SafePointer(ActorAt(selected))) {
        editing ^= 1;
    }
    if (details && editing) {
        Actor *a = ActorAt(selected);

        if ((pressed & CONT_UP) && field > 0) {
            field--;
        }
        if ((pressed & CONT_DOWN) && field < FIELDS - 1) {
            field++;
        }
        if ((pressed & CONT_CUP) && stepIndex < ARRAY_COUNT(steps) - 1) {
            stepIndex++;
        }
        if ((pressed & CONT_CDOWN) && stepIndex > 0) {
            stepIndex--;
        }
        if (a != NULL && (pressed & (CONT_LEFT | CONT_RIGHT))) {
            FieldChange(a, pressed & CONT_RIGHT ? 1 : -1);
        }
        return 1;
    }
    if ((pressed & CONT_A) && details) {
        Actor *a = ActorAt(selected);
        if (a != NULL && SafeFloats(&a->pos.x, 3)) {
            run.teleportPos = a->pos;
            run.teleportPos.y += 50.0f;
            run.teleport = 1;
            MenuClose();
            return 1;
        }
    }
    if ((pressed & CONT_Z) && details) {
        Actor *a = ActorAt(selected);
        if (ActorMovable(a)) {
            run.moveActor = a;
            MenuClose();
            return 1;
        }
    }
    if (pressed & CONT_A) {
        details = 1;
    }
    if (pressed & (CONT_UP | CONT_LEFT)) {
        selected--;
    }
    if (pressed & (CONT_DOWN | CONT_RIGHT)) {
        selected++;
    }
    if (count > 0) {
        selected = (selected + count) % count;
    } else {
        selected = 0;
    }
    if (selected < top) {
        top = selected;
    }
    if (selected >= top + ROWS) {
        top = selected - ROWS + 1;
    }
    return 1;
}

static void Line(Gfx **dl, int row, u32 color, const char *fmt, ...) {
    char buf[64];
    va_list args;

    va_start(args, fmt);
    FormatV(buf, sizeof(buf), fmt, args);
    va_end(args);
    TextPrint(dl, VIEW_X + 6, VIEW_Y + 16 + row * ROW_H, 1, color, buf);
}

static void DrawDetails(Gfx **dl, const Actor *a) {
    char name[9];

    ObjectName(a, name);
    Line(dl, 0, 0xFFFFFFFF, "%s (type %d)  model %s", TypeName(a), a->type, name);
    Line(dl, 1, 0xB0B8C8FF, "at %08X", (u32)a);
    /* An actor's floats are read only when they are safe for the FPU (safe.h). */
    if (SafeFloats(&a->pos.x, 3)) {
        Line(dl, 2, 0xFFFFFFFF, "pos %9.2f %9.2f %9.2f", (double)a->pos.x, (double)a->pos.y, (double)a->pos.z);
    }
    if (SafeFloats(&a->vel.x, 3)) {
        Line(dl, 3, 0xFFFFFFFF, "vel %9.3f %9.3f %9.3f", (double)a->vel.x, (double)a->vel.y, (double)a->vel.z);
    }
    if (SafeFloats(&a->rot.x, 3)) {
        Line(dl, 4, 0xFFFFFFFF, "rot %9.3f %9.3f %9.3f", (double)a->rot.x, (double)a->rot.y, (double)a->rot.z);
    }
    if (SafeFloat(&a->radius)) {
        Line(dl, 5, 0xFFFFFFFF, "radius %.2f  flags %08X", (double)a->radius, a->flags);
    } else {
        Line(dl, 5, 0xFFFFFFFF, "flags %08X", a->flags);
    }
    Line(dl, 6, 0xFFFFFFFF, "active %d  life %d  alpha %d", a->active, a->lifeTimer, a->alpha);
    if (SafePointer(a->state)) {
        Line(dl, 7, 0xFFFFFFFF, "state: hits %d  damage %d  mode %d (was %d)", a->state->hits, a->state->damage,
             a->state->mode, a->state->prevMode);
    }
    if (SafePointer(a->link)) {
        Line(dl, 8, 0xFFFFFFFF, "linked to %s at %08X", TypeName(a->link), (u32)a->link);
    }
}

static void Draw(Gfx **dl) {
    int count = ActorCount();
    char title[48];
    char name[9];
    Actor *a;

    TextRect(dl, VIEW_X, VIEW_Y, VIEW_X + VIEW_W, VIEW_Y + 20 + ROWS * ROW_H + 14, MenuPanelColor(0));
    TextRect(dl, VIEW_X, VIEW_Y, VIEW_X + VIEW_W, VIEW_Y + 13, MenuPanelColor(1));
    Format(title, sizeof(title), "Actors  %d/%d", count > 0 ? selected + 1 : 0, count);
    TextPrint(dl, VIEW_X + 6, VIEW_Y + 3, 1, 0xFFE070FF, title);
    if (details) {
        a = ActorAt(selected);
        if (a != NULL) {
            DrawDetails(dl, a);
        }
        if (editing && a != NULL) {
            char value[16];

            FieldText(a, value, sizeof(value));
            Line(dl, 10, 0xFFE070FF, "edit %-7s %-10s step %.2f", fieldNames[field], value, (double)steps[stepIndex]);
            Line(dl, ROWS - 1, 0xFF9060FF, "Modified game! May crash");
            Line(dl, ROWS, 0x8090A8FF, "^v field  </> change  C^v step  Start done");
            return;
        }
        if (ActorMovable(a)) {
            Line(dl, ROWS - 1, 0xFF9060FF, "Z brings it to Glover: modified game");
        }
        Line(dl, ROWS, 0x8090A8FF, "</> other  A go  Z bring  Start edit  B back");
        return;
    }
    a = ActorAt(top);
    for (int r = 0; r < ROWS && SafePointer(a) && a != &actorList.head; r++, a = a->next) {
        ObjectName(a, name);
        if (SafeFloats(&a->pos.x, 3)) {
            Line(dl, r, top + r == selected ? 0xFFE070FF : 0xFFFFFFFF, "%3d %-12s %-8s %6.0f %6.0f %6.0f",
                 top + r + 1, TypeName(a), name, (double)a->pos.x, (double)a->pos.y, (double)a->pos.z);
        } else {
            Line(dl, r, top + r == selected ? 0xFFE070FF : 0xFFFFFFFF, "%3d %-12s %-8s", top + r + 1, TypeName(a),
                 name);
        }
    }
    Line(dl, ROWS, 0x8090A8FF, "A details  B back");
}

const View actorView = { Open, Input, Draw };
