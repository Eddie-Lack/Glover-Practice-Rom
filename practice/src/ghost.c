/* The ghost of a recorded run (ghost.h): a see-through glove and ball, drawn at the places they had on the same
 * game frame of a recording.
 *
 * The game thread keeps the places while a recording is made. The draw thread draws them at the end of the frame's
 * display list (PracticePrintTimers), as the collision view draws: its own small list, called from the game's list
 * by one command. It reads the game's memory, and every game float is checked as bits first (safe.h), because the
 * game's threads trap on NaN and denormal operands. */
#include "actions.h"
#include "fmt.h"
#include "game.h"
#include "ghost.h"
#include "rdp.h"
#include "recording.h"
#include "safe.h"
#include "savestate.h"

_Static_assert(sizeof(GhostFrame) == 16, "a ghost frame is 16 bytes");
_Static_assert(GHOST_SLOT_FRAMES * sizeof(GhostFrame) == GHOST_SLOT_BYTES, "a slot holds GHOST_SLOT_FRAMES frames");
_Static_assert(GHOST_BUFFER + GHOST_SLOTS * GHOST_SLOT_BYTES <= RECORDING_BUFFER, "the ghosts must not overlap the recording");
_Static_assert(__builtin_offsetof(Ghost, writing) == 0x30 && __builtin_offsetof(Ghost, draws) == 0x40 &&
                   __builtin_offsetof(Ghost, drawnHand) == 0x48 && __builtin_offsetof(Ghost, list) == 0x58 &&
                   __builtin_offsetof(Ghost, drawFrame) == 0x60 &&
                   __builtin_offsetof(Ghost, liveHand) == 0x64 && __builtin_offsetof(Ghost, liveBall) == 0x70 &&
                   __builtin_offsetof(Ghost, lastFrame) == 0x7C,
               "tests/ghost.py has the offsets");

Ghost ghost = { .writing = -1 };

static GhostFrame *Frames(int slot) {
    return (GhostFrame *)(GHOST_BUFFER + slot * GHOST_SLOT_BYTES);
}

/* ---- Keeping the places ----------------------------------------------------------------------------------- */

/* A game float as a whole number of units, or 0 when it is unreadable or beyond what 16 bits hold. */
static int Unit(const f32 *p, s16 *out) {
    f32 v;

    if (!ReadFloat(p, &v) || !(v < 32000.0f && v > -32000.0f)) {
        return 0;
    }
    *out = (s16)(v < 0.0f ? v - 0.5f : v + 0.5f);
    return 1;
}

static int Place(const Actor *a, s16 *out) {
    return SafePointer(a) && Unit(&a->pos.x, &out[0]) && Unit(&a->pos.y, &out[1]) && Unit(&a->pos.z, &out[2]);
}

static void Sample(GhostSlot *s) {
    s32 index = frameCount - s->start;
    GhostFrame *f;
    f32 yaw;

    if (index < 0 || index >= GHOST_SLOT_FRAMES || gameInfo.level != s->level) {
        return;
    }
    f = Frames(s - ghost.slots);
    /* Frames that were not sampled (the count is the next one to keep) read as empty. */
    for (s32 i = s->count; i < index; i++) {
        f[i].flags = 0;
    }
    f += index;
    f->flags = 0;
    if (Place(&hand, f->hand)) {
        f->flags |= GHOST_HAND;
    }
    if (ball.active && Place(&ball, f->ball)) {
        f->flags |= GHOST_BALL;
    }
    if (ReadFloat(&hand.rot.y, &yaw) && yaw < 100.0f && yaw > -100.0f) {
        f->yaw = (u16)(s32)(yaw * (65536.0f / 6.2831853f));
        f->flags |= GHOST_YAW;
    }
    if (index + 1 > s->count) {
        s->count = index + 1;
    }
    ghost.stored++;
}

/* Ends the recording's ghost: it replaces the older ghost of its level when it kept something. */
static void Finish(void) {
    /* The game thread and the menu's thread can both end a recording. */
    u32 mask = __osDisableInt();
    int w = ghost.writing;
    GhostSlot *s;

    ghost.writing = -1;
    __osRestoreInt(mask);
    if (w < 0) {
        return;
    }
    s = &ghost.slots[w];
    if (s->count == 0) {
        return;
    }
    for (int i = 0; i < GHOST_SLOTS; i++) {
        if (i != w && ghost.slots[i].used && ghost.slots[i].level == s->level) {
            ghost.slots[i].used = 0;
        }
    }
    s->stamp = ++ghost.clock;
    s->used = 1;
}

void GhostRecordStart(void) {
    int pick = -1;
    GhostSlot *s;

    Finish();
    /* A free slot, else the oldest ghost. The level's older ghost stays until the new one has a frame. */
    for (int i = 0; i < GHOST_SLOTS; i++) {
        if (!ghost.slots[i].used) {
            pick = i;
            break;
        }
    }
    if (pick < 0) {
        pick = 0;
        for (int i = 1; i < GHOST_SLOTS; i++) {
            if (ghost.slots[i].stamp < ghost.slots[pick].stamp) {
                pick = i;
            }
        }
    }
    s = &ghost.slots[pick];
    s->used = 0;
    s->count = 0;
    s->level = gameInfo.level;
    s->start = frameCount;
    ghost.writing = pick;
    ghost.lastFrame2 = frameCount2;
    ghost.lastFrame = frameCount;
    ghost.loads = stateLoads;
    Sample(s);
}

void GhostGameFrame(void) {
    if (ghost.writing < 0) {
        return;
    }
    if (!recording.recording) {
        Finish();
        return;
    }
    /* The recording goes on with the input, but its places are only valid while the game frames count up in the
     * same level without a state load. */
    if (frameCount < ghost.lastFrame || gameInfo.level != ghost.slots[ghost.writing].level ||
        stateLoads != ghost.loads) {
        Finish();
        return;
    }
    if (frameCount2 != ghost.lastFrame2) {
        ghost.lastFrame2 = frameCount2;
        ghost.lastFrame = frameCount;
        Sample(&ghost.slots[ghost.writing]);
    }
}

void GhostClear(void) {
    u32 mask = __osDisableInt();

    ghost.writing = -1;
    for (int i = 0; i < GHOST_SLOTS; i++) {
        ghost.slots[i].used = 0;
    }
    __osRestoreInt(mask);
}

/* The slot of the finished ghost of a level, or -1. */
static int Find(int level) {
    for (int i = 0; i < GHOST_SLOTS; i++) {
        if (ghost.slots[i].used && ghost.slots[i].level == level) {
            return i;
        }
    }
    return -1;
}

/* ---- The menu ---------------------------------------------------------------------------------------------- */

void GhostToggle(int arg) {
    (void)arg;
    run.ghost ^= 1;
}

void GhostText(char *buf, int size, int arg) {
    (void)arg;
    if (!run.ghost) {
        Format(buf, size, "off");
        return;
    }
    if (ghost.writing >= 0 && !recording.recording) {
        Finish(); /* a recording that stopped while the game waited */
    }
    int slot = Find(gameInfo.level);

    if (slot >= 0) {
        Format(buf, size, "on (%d frames)", ghost.slots[slot].count);
    } else if (ghost.writing >= 0) {
        Format(buf, size, "on (recording)");
    } else {
        Format(buf, size, "on (none for this level)");
    }
}

/* ---- Drawing ----------------------------------------------------------------------------------------------- */

#define SPHERE_VERTS 12
#define SPHERE_FACES 20
#define NOSE_VERTS 5
#define HAND_VERTS (SPHERE_VERTS + NOSE_VERTS)
#define LIST_SIZE 64
/* The commands of the longest list GhostDraw writes: the setup, the glove (a matrix, its vertices, 10 commands of 2
 * triangles of the sphere and 3 of the nose), the ball (a matrix, its vertices and 10 commands) and the end. */
#define SETUP_COMMANDS 13
#define HAND_COMMANDS (2 + SPHERE_FACES / 2 + 3)
#define BALL_COMMANDS (2 + SPHERE_FACES / 2)
_Static_assert(SETUP_COMMANDS + HAND_COMMANDS + BALL_COMMANDS + 1 <= LIST_SIZE, "the ghost's list must fit in lists[]");
/* The model's vertices are in eighths of a unit around the place. */
#define SCALE 8.0f
#define HAND_RADIUS 12.0f
#define BALL_RADIUS 20.0f
#define HAND_RGB 0xFFE040
#define BALL_RGB 0xFF50C8
#define NOSE_RGB 0xFFFFFF
#define ALPHA 0x80

#define VP_X 640
#define VP_Y 480
#define VP_Z 511

static Vtx handVerts[2][HAND_VERTS] __attribute__((aligned(16)));
static Vtx ballVerts[2][SPHERE_VERTS] __attribute__((aligned(16)));
static Gfx lists[2][LIST_SIZE] __attribute__((aligned(16)));
static Mtx projection[2] __attribute__((aligned(16)));
static Mtx modelView[2][2] __attribute__((aligned(16)));
static Vp viewport[2] __attribute__((aligned(16)));

/* An icosahedron of unit size: its 12 corners in thousandths and its 20 faces. */
static const s16 icoCorners[SPHERE_VERTS][3] = {
    { -526, 851, 0 }, { 526, 851, 0 },  { -526, -851, 0 }, { 526, -851, 0 }, { 0, -526, 851 }, { 0, 526, 851 },
    { 0, -526, -851 }, { 0, 526, -851 }, { 851, 0, -526 }, { 851, 0, 526 }, { -851, 0, -526 }, { -851, 0, 526 },
};
static const u8 icoFaces[SPHERE_FACES][3] = {
    { 0, 11, 5 }, { 0, 5, 1 },  { 0, 1, 7 },  { 0, 7, 10 }, { 0, 10, 11 }, { 1, 5, 9 }, { 5, 11, 4 },
    { 11, 10, 2 }, { 10, 7, 6 }, { 7, 1, 8 }, { 3, 9, 4 },  { 3, 4, 2 },   { 3, 2, 6 }, { 3, 6, 8 },
    { 3, 8, 9 },  { 4, 9, 5 },  { 2, 4, 11 }, { 6, 2, 10 }, { 8, 6, 7 },  { 9, 8, 1 },
};

static void PutVertex(Vtx *v, f32 x, f32 y, f32 z, u32 rgb, u32 alpha) {
    v->x = (s16)(x * SCALE);
    v->y = (s16)(y * SCALE);
    v->z = (s16)(z * SCALE);
    v->flag = 0;
    v->s = v->t = 0;
    v->r = rgb >> 16;
    v->g = rgb >> 8;
    v->b = rgb;
    v->a = alpha;
}

/* The colour of a sphere's corner: lighter at the top. */
static u32 Shade(u32 rgb, int cornerY) {
    int k = 140 + (cornerY + 851) * 115 / 1702; /* 140 to 255 */

    return ((((rgb >> 16) & 0xFF) * k / 255) << 16) | ((((rgb >> 8) & 0xFF) * k / 255) << 8) | ((rgb & 0xFF) * k / 255);
}

static void Sphere(Vtx *v, f32 r, u32 rgb) {
    for (int i = 0; i < SPHERE_VERTS; i++) {
        PutVertex(&v[i], icoCorners[i][0] * r * 0.001f, icoCorners[i][1] * r * 0.001f, icoCorners[i][2] * r * 0.001f,
                  Shade(rgb, icoCorners[i][1]), ALPHA);
    }
}

/* The size of an actor's first collision sphere, or a default. */
static f32 Radius(const Actor *a, f32 fallback) {
    f32 r;

    if (SafePointer(a->obj) && a->obj->numSpheres > 0 && ReadFloat(&a->obj->spheres[0].radius, &r) && r >= 4.0f &&
        r <= 100.0f) {
        return r;
    }
    return fallback;
}

static void Translate(Mtx *m, const s16 *pos) {
    f32 mf[4][4];

    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 4; j++) {
            mf[i][j] = 0.0f;
        }
    }
    mf[0][0] = mf[1][1] = mf[2][2] = 1.0f / SCALE;
    mf[3][0] = pos[0];
    mf[3][1] = pos[1];
    mf[3][2] = pos[2];
    mf[3][3] = 1.0f;
    guMtxF2L(mf, m);
}

void GhostDraw(void) {
    int b = draw_buffer & 1;
    Gfx *g = lists[b];
    u16 perspNorm;
    int slot;
    s32 index;
    const GhostFrame *f;
    int hasHand, hasBall;

    ghost.drawFrame = frameCount;
    /* Only on frames of a level in which the game drew the world: its camera matrix is then this frame's. */
    if (gameInfo.mode != GAME_MODE_LEVEL || gameInfo.level >= LEVEL_PRESENT || !gfxIsDrawing || onlyDrawBackdrops ||
        (gameInfo.fade >= 276 && gameInfo.fade2 >= 276) || fieldOfView <= 0 || nearPlaneDist <= 0 ||
        farPlaneDist <= nearPlaneDist) {
        return;
    }
    slot = Find(gameInfo.level);
    if (slot < 0) {
        return;
    }
    index = frameCount - ghost.slots[slot].start;
    if (index < 0 || index >= ghost.slots[slot].count) {
        return;
    }
    f = &Frames(slot)[index];
    hasHand = (f->flags & GHOST_HAND) != 0;
    hasBall = (f->flags & GHOST_BALL) != 0;
    if (!hasHand && !hasBall) {
        return;
    }

    guPerspective(&projection[b], &perspNorm, fieldOfView, 4.0f / 3.0f, nearPlaneDist, farPlaneDist, 1.0f);
    viewport[b].scale[0] = viewport[b].trans[0] = VP_X;
    viewport[b].scale[1] = viewport[b].trans[1] = VP_Y;
    viewport[b].scale[2] = viewport[b].trans[2] = VP_Z;
    viewport[b].scale[3] = viewport[b].trans[3] = 0;

    /* The overlay runs in the frame's last task, which starts from the microcode's first state: everything is set
     * again, as the collision view does (docs/build.md). The surface is see-through and ignores the Z buffer. */
    *g++ = GfxSync(CMD_PIPESYNC);
    *g++ = GfxCmd(CMD_SETSCISSOR << 24, (320 * 4) << 12 | (240 * 4));
    *g++ = GfxViewport(&viewport[b]);
    *g++ = GfxMatrix(&projection[b], MTX_PROJECTION | MTX_LOAD);
    *g++ = GfxMatrix(dynamicp + DYNAMIC_CAMERA_VIEW, MTX_PROJECTION);
    *g++ = GfxPerspNormalize(perspNorm);
    *g++ = GfxCmd(CMD_TEXTURE << 24, 0);
    *g++ = GfxCmd(CMD_CLEARGEOMETRYMODE << 24, GEOM_ALL);
    *g++ = GfxCmd(CMD_SETGEOMETRYMODE << 24, GEOM_SHADE | GEOM_SHADING_SMOOTH);
    *g++ = GfxSync(CMD_PIPESYNC);
    *g++ = GfxSetOtherMode(CMD_SETOTHERMODE_H, 4, 20, 0xCF0);
    *g++ = GfxSetOtherMode(CMD_SETOTHERMODE_L, 3, 29, 0x00504240);
    *g++ = GfxCmd(0xFCFFFFFF, 0xFFFE793C);

    if (hasHand) {
        f32 r = Radius(&hand, HAND_RADIUS);
        f32 a = (f->flags & GHOST_YAW) ? f->yaw * (6.2831853f / 65536.0f) : 0.0f;
        f32 fx = __sinf(a), fz = __cosf(a);
        f32 rx = fz, rz = -fx;
        Vtx *v = handVerts[b];
        /* A nose: a pyramid on the glove's heading (forward is (sin, cos) of rot.y). */
        static const s8 corner[4][2] = { { -1, -1 }, { 1, -1 }, { 1, 1 }, { -1, 1 } };

        Sphere(v, r, HAND_RGB);
        PutVertex(&v[SPHERE_VERTS], fx * r * 1.9f, 0.0f, fz * r * 1.9f, NOSE_RGB, ALPHA);
        for (int i = 0; i < 4; i++) {
            f32 side = corner[i][0] * r * 0.4f, up = corner[i][1] * r * 0.4f;

            PutVertex(&v[SPHERE_VERTS + 1 + i], fx * r * 0.8f + rx * side, up, fz * r * 0.8f + rz * side, NOSE_RGB, ALPHA);
        }
        Translate(&modelView[b][0], f->hand);
        *g++ = GfxMatrix(&modelView[b][0], MTX_LOAD);
        *g++ = GfxVertices(v, HAND_VERTS);
        for (int t = 0; t < SPHERE_FACES; t += 2) {
            *g++ = GfxTriangles(icoFaces[t][0], icoFaces[t][1], icoFaces[t][2], icoFaces[t + 1][0], icoFaces[t + 1][1],
                                icoFaces[t + 1][2]);
        }
        /* The tip is vertex 12 and the base corners 13 to 16. */
        *g++ = GfxTriangles(12, 13, 14, 12, 14, 15);
        *g++ = GfxTriangles(12, 15, 16, 12, 16, 13);
        *g++ = GfxTriangles(13, 15, 14, 13, 16, 15);
        osWritebackDCache(v, HAND_VERTS * sizeof(Vtx));
        osWritebackDCache(&modelView[b][0], sizeof(Mtx));
    }
    if (hasBall) {
        Vtx *v = ballVerts[b];

        Sphere(v, Radius(&ball, BALL_RADIUS), BALL_RGB);
        Translate(&modelView[b][1], f->ball);
        *g++ = GfxMatrix(&modelView[b][1], MTX_LOAD);
        *g++ = GfxVertices(v, SPHERE_VERTS);
        for (int t = 0; t < SPHERE_FACES; t += 2) {
            *g++ = GfxTriangles(icoFaces[t][0], icoFaces[t][1], icoFaces[t][2], icoFaces[t + 1][0], icoFaces[t + 1][1],
                                icoFaces[t + 1][2]);
        }
        osWritebackDCache(v, SPHERE_VERTS * sizeof(Vtx));
        osWritebackDCache(&modelView[b][1], sizeof(Mtx));
    }
    *g++ = GfxEndDisplayList();

    osWritebackDCache(lists[b], (g - lists[b]) * sizeof(Gfx));
    osWritebackDCache(&projection[b], sizeof(Mtx));
    osWritebackDCache(&viewport[b], sizeof(Vp));
    *glistp++ = GfxDisplayList(lists[b]);

    /* The live places, copied as bits: the draw thread's view of the game, which the ghost is in step with. */
    for (int i = 0; i < 3; i++) {
        ((volatile u32 *)ghost.liveHand)[i] = ((const volatile u32 *)&hand.pos.x)[i];
        ((volatile u32 *)ghost.liveBall)[i] = ((const volatile u32 *)&ball.pos.x)[i];
    }
    ghost.drawIndex = index;
    for (int i = 0; i < 3; i++) {
        ghost.drawnHand[i] = hasHand ? f->hand[i] : 0;
        ghost.drawnBall[i] = hasBall ? f->ball[i] : 0;
    }
    ghost.drawnYaw = f->yaw;
    ghost.drawnFlags = f->flags;
    ghost.drawnSlot = slot;
    ghost.list = lists[b];
    ghost.commands = g - lists[b];
    ghost.draws++;
}
