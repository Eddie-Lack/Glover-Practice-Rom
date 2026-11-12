/* Hiding objects (hide.h).
 *
 * Several of the game's drawing functions also change the game's state: they step animations, place collision
 * spheres, age footprints and call Random. A hidden kind's function is therefore always called. Afterwards the
 * commands that it wrote to glistp that draw something (vertices, triangles, rectangles, nested display lists) become
 * no-ops. Every other command stays: the RDP ends up in the state that the game expects, and the game's own copies
 * of the render state (transparentSurf, xluSurf, the lighting bit) stay true. */
#include "actions.h"
#include "fmt.h"
#include "game.h"
#include "hide.h"
#include "rdp.h"
#include "safe.h"

/* The parts of the game's structures that this file reads. */

/* GameSprite (overlay.h): anim.def.frames at 0x24. */
typedef struct HideSprite {
    /* 0x00 */ PAD(00)[0x24];
    /* 0x24 */ void *frames;
} HideSprite;

/* Object (actor.h): the model's tree and the sprites that it places. */
typedef struct HideObject {
    /* 0x00 */ PAD(00)[0x28];
    /* 0x28 */ s32 numSprites;
    /* 0x2C */ struct {
        void *tex;
        HideSprite *sprite;
        PAD(08)[0x10];
    } *sprites; /* ObjectSprite, 0x18 bytes */
    /* 0x30 */ PAD(30)[0x04];
    /* 0x34 */ struct HideObject *child;
    /* 0x38 */ struct HideObject *sibling;
} HideObject;

/* Bullet (bullet.h): the list's links, its actor, its two sprites. */
typedef struct HideBullet {
    /* 0x000 */ struct HideBullet *next;
    /* 0x004 */ struct HideBullet *prev;
    /* 0x008 */ Actor actor;
    /* 0x0EC */ u8 sprites[2][0x38];
} HideBullet;

_Static_assert(__builtin_offsetof(HideBullet, sprites) == 0xEC, "Bullet has the game's layout");

#define DEBRIS_COUNT 120
#define DEBRIS_SIZE 0x120 /* Debris (debris.h): an actor's fields, then its sprite at 0xE8 */
#define GARIB_ANIMS 4     /* garibAnimation[0..3]: the four kinds of garib (collect.c, hand.c) */
#define SPRITE_ANIM_SIZE 0x10

/* Actor types (enums.h). */
#define TYPE_BALL 2
#define TYPE_BULLET 4
#define TYPE_FIRST_ENEMY 5
#define TYPE_PLATFORM 59
#define TYPE_LANDSCAPE 60
#define TYPE_COLLECTABLE 61
#define TYPE_WATER 62
#define TYPE_CAMEO 63
#define TYPE_ORBITER 1000 /* CreateOrbiter: the pick-up's type plus 1000 */

extern u8 debris[DEBRIS_COUNT * DEBRIS_SIZE];
extern Actor extraBall[3];
extern Actor plaster[4];
extern Actor fadeOverlay;
extern Actor starOverlay;
extern Actor *currentDrawActor;
extern u8 noClip; /* 1 while CreateObjects draws the foreground objects (title and menu screens) */
extern struct {
    HideBullet head;
    s32 count;
} bulletList;

void TransformAndDrawObject(void *obj, f32 time, s16 doKeys, s16 arg3);
void PrintSprite(void *sprite);
void PrintBackdrops(void);
void PrintStats(void);
void ProcessShadows(void);
void DrawFootprints(void);
void UpdateLightning(void);
void DrawBlurs(void);

HideStats hideStats;

/* F3DEX 1.x commands that draw and that Strip removes, besides those in rdp.h. */
#define CMD_SPNOOP 0x00
#define CMD_BRANCH_Z 0xB0
#define CMD_LINE3D 0xB5 /* also gSP1Quadrangle */
#define CMD_SPRITE2D_DRAW 0xBD
#define CMD_TEXRECTFLIP 0xE5

/* The sprites of the models of the actors hidden on this frame. A sprite is drawn after its actor, except a sprite
 * of the opaque pass whose actor is drawn in the translucent pass: that one shows. */
#define HIDDEN_SPRITES 32
static HideSprite *hiddenSprites[HIDDEN_SPRITES];
static u8 hiddenCount;

/* The first call of each frame starts the frame's notes. dispFrameCount counts the frames that the draw thread
 * builds (main.c). */
static void NewFrame(void) {
    HideStats *st = &hideStats;

    if ((u32)dispFrameCount == st->frame) {
        return;
    }
    st->frame = dispFrameCount;
    st->last[0] = st->actors;
    st->last[1] = st->sprites;
    st->last[2] = st->commands;
    st->lastCounts = st->counts;
    st->actors = st->sprites = st->commands = 0;
    st->counts = 0;
    hiddenCount = 0;
}

/* Turns the commands in [from, to) that draw into no-ops. LISTS: also the calls of other display lists (and
 * leaves their state commands out as well). Returns the number of commands changed. */
static int Strip(Gfx *from, Gfx *to, int lists) {
    int n = 0;

    for (Gfx *g = from; g < to; g++) {
        u32 op = g->w0 >> 24;
        int drop = 0;

        switch (op) {
            case CMD_VTX:
            case CMD_TRI1:
            case CMD_TRI2:
            case CMD_LINE3D:
            case CMD_FILLRECT:
            case CMD_SPRITE2D_DRAW:
                drop = 1;
                break;
            case CMD_DL:
            case CMD_BRANCH_Z:
                drop = lists;
                break;
            case CMD_TEXRECT:
            case CMD_TEXRECTFLIP:
                /* The rectangle's texture coordinates follow in two half commands. */
                for (int i = 0; i < 2 && g + 1 < to; i++) {
                    u32 next = g[1].w0 >> 24;
                    if (next != CMD_RDPHALF_1 && next != CMD_RDPHALF_2) {
                        break;
                    }
                    g->w0 = CMD_SPNOOP << 24;
                    g->w1 = 0;
                    g++;
                    n++;
                }
                drop = 1;
                break;
        }
        if (drop) {
            g->w0 = CMD_SPNOOP << 24;
            g->w1 = 0;
            n++;
        }
    }
    hideStats.commands += n;
    return n;
}

static int Inside(const void *p, const void *base, u32 size) {
    return (u32)p - (u32)base < size;
}

/* The kind of an actor that DrawActor draws, or -1 for one that is never hidden. */
static int ActorKind(const Actor *a) {
    if (a == &hand) {
        return HIDE_GLOVE;
    }
    if (a == &ball || Inside(a, extraBall, sizeof(extraBall)) || Inside(a, plaster, sizeof(plaster))) {
        return HIDE_BALL;
    }
    if (Inside(a, debris, sizeof(debris))) {
        return HIDE_DEBRIS;
    }
    /* PrintFadeOverlay draws the fade and the star transition with DrawActor. */
    if (a == &fadeOverlay || a == &starOverlay || noClip) {
        return -1;
    }
    switch (a->type) {
        case TYPE_LANDSCAPE:
            return (a->flags & 4) ? HIDE_SCENERY : HIDE_LANDSCAPE;
        case TYPE_CAMEO:
            return HIDE_SCENERY;
        case TYPE_WATER:
            return HIDE_WATER;
        case TYPE_PLATFORM:
            return HIDE_PLATFORMS;
        case TYPE_COLLECTABLE:
            return HIDE_COLLECT;
        case TYPE_BULLET:
        case TYPE_BALL: /* a shot of the ball kind (bullet.c): the ball and its copies are found above */
            return HIDE_ENEMIES;
    }
    if (a->type >= TYPE_FIRST_ENEMY && a->type < TYPE_PLATFORM) {
        return HIDE_ENEMIES;
    }
    if (a->type >= TYPE_ORBITER) {
        return HIDE_COLLECT;
    }
    return -1;
}

/* Notes the sprites of a hidden actor's model: the tree's nodes in order, at most 64. */
static void NoteSprites(const HideObject *obj) {
    const HideObject *stack[16];
    int depth = 0;
    int nodes = 0;

    while (obj != NULL && nodes++ < 64) {
        if (!SafePointer(obj)) {
            return;
        }
        s32 n = obj->numSprites;
        if (n > 0 && n <= 16 && SafePointer(obj->sprites)) {
            for (int i = 0; i < n && hiddenCount < HIDDEN_SPRITES; i++) {
                HideSprite *s = obj->sprites[i].sprite;
                if (s != NULL) {
                    hiddenSprites[hiddenCount++] = s;
                    hideStats.objectSprites++;
                }
            }
        }
        if (obj->sibling != NULL && depth < (int)ARRAY_COUNT(stack)) {
            stack[depth++] = obj->sibling;
        }
        obj = obj->child;
        if (obj == NULL && depth > 0) {
            obj = stack[--depth];
        }
    }
}

/* DrawActor calls this in place of TransformAndDrawObject: every 3D object of a frame comes through here. */
void PracticeTransformAndDrawObject(void *obj, f32 time, s16 doKeys, s16 arg3) {
    Gfx *from = glistp;
    int kind;

    TransformAndDrawObject(obj, time, doKeys, arg3);
    if (!run.hide) {
        return;
    }
    NewFrame();
    u32 t = osGetCount();
    kind = ActorKind(currentDrawActor);
    if (kind >= 0 && (run.hide & HIDE_BIT(kind))) {
        Strip(from, glistp, 1);
        hideStats.actors++;
        NoteSprites((const HideObject *)obj);
    }
    hideStats.counts += osGetCount() - t;
}

static int SpriteHidden(const HideSprite *s) {
    u16 hide = run.hide;

    if (Inside(s, debris, sizeof(debris))) {
        return (hide & HIDE_BIT(HIDE_DEBRIS)) != 0;
    }
    if (hide & HIDE_BIT(HIDE_COLLECT)) {
        /* A garib's sprite runs one of the garib animations; so does the garib that the Locate cheat shows. */
        for (int i = 0; i < GARIB_ANIMS; i++) {
            if (s->frames == *(void **)&garibAnimation[i * SPRITE_ANIM_SIZE]) {
                return 1;
            }
        }
    }
    if (hide & HIDE_BIT(HIDE_ENEMIES)) {
        const HideBullet *b = bulletList.head.next;
        for (int i = 0; i < 64 && b != &bulletList.head && SafePointer(b); i++, b = b->next) {
            if (Inside(s, b->sprites, sizeof(b->sprites))) {
                return 1;
            }
        }
    }
    for (int i = 0; i < hiddenCount; i++) {
        if (hiddenSprites[i] == s) {
            return 1;
        }
    }
    return 0;
}

/* PrintSpritesOpaque and PrintSpritesTranslucent call this in place of PrintSprite. */
void PracticePrintSprite(void *sprite) {
    Gfx *from = glistp;

    PrintSprite(sprite);
    if (!run.hide || glistp == from) {
        return;
    }
    NewFrame();
    u32 t = osGetCount();
    if (SpriteHidden(sprite)) {
        Strip(from, glistp, 0);
        hideStats.sprites++;
    }
    hideStats.counts += osGetCount() - t;
}

/* Each of these runs the game's function and keeps only its state commands while its kind is hidden. The blurs'
 * own display list (blurdl) sets state only. */
static void CallAndStrip(void (*fn)(void), int kind) {
    Gfx *from = glistp;

    fn();
    if (run.hide & HIDE_BIT(kind)) {
        NewFrame();
        Strip(from, glistp, 0);
    }
}

void PracticePrintBackdrops(void) {
    CallAndStrip(PrintBackdrops, HIDE_SKY);
}

void PracticeProcessShadows(void) {
    CallAndStrip(ProcessShadows, HIDE_SHADOWS);
}

void PracticeDrawFootprints(void) {
    CallAndStrip(DrawFootprints, HIDE_FOOTPRINTS);
}

void PracticeUpdateLightning(void) {
    CallAndStrip(UpdateLightning, HIDE_LIGHTNING);
}

void PracticeDrawBlurs(void) {
    CallAndStrip(DrawBlurs, HIDE_TRAILS);
}

/* PracticePrintStats calls this in place of PrintStats. PrintStats calls Random and steps the counters, so it always
 * runs. While the HUD is hidden, its commands are dropped: it sets up no state of its own, and ShowText, which
 * follows it, starts from spInit. */
void HidePrintStats(void) {
    Gfx *from = glistp;

    PrintStats();
    if (run.hide & HIDE_BIT(HIDE_HUD)) {
        NewFrame();
        hideStats.commands += glistp - from;
        glistp = from;
    }
}

void HideToggle(int arg) {
    run.hide ^= HIDE_BIT(arg);
}

void HideText(char *buf, int size, int arg) {
    Format(buf, size, "%s", (run.hide & HIDE_BIT(arg)) ? "hidden" : "shown");
}
