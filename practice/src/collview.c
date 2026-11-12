/* The collision view: the game's collision faces, drawn over the frame as see-through triangles, and the
 * collision spheres of the glove, the ball and the enemies. It runs on the draw thread at the end of the frame's
 * display list (PracticePrintTimers) and only reads the game's memory.
 *
 * The faces come from a cache. Once a level is loaded, the cache is built from the collision mesh of each
 * landscape actor (collisionList) and each platform (platformList): its vertices and one display list for each
 * kind of face, in the mesh's own space. The game places each collision face as a corner of the mesh, scaled by
 * the actor's scale, turned by its rotation and moved to its position (InitPlanes, RecalculatePlatformInfo).
 * Each frame, the view draws each actor's lists with a matrix made the same way. A platform then moves without
 * any work for its faces. Actors that share a mesh, a scale and the kind of each face share its lists.
 *
 * The draw thread runs above the game thread and can stop it in the middle of a game frame, and some lists hold
 * freed or half-written data. So every pointer is checked before it is followed, every walk is bounded, and every
 * float is checked as bits before the FPU sees it: the game's threads trap on NaN and denormal operands. Game
 * floats are read with ReadFloat, which also flushes numbers too small to multiply safely. */
#include "actions.h"
#include "collview.h"
#include "freecam.h"
#include "game.h"
#include "moonaim.h"
#include "practice.h"
#include "rdp.h"
#include "safe.h"

/* The cache. A shape is one mesh as some actors use it; a record is one actor. */
#define MAX_SHAPES 128
#define MAX_RECORDS 160
#define MAX_STATIC 96 /* matrices of landscape actors, which never move */
#define CACHE_VTX 2688
#define CACHE_GFX 2048
#define MAX_MESH_FACES 1024
#define ACTOR_SLOTS 512 /* the hash table from an actor's address to its record */
/* The game's collision lists are walked at most this far. */
#define MAX_COLLISION_ACTORS 1024
#define MAX_PLATFORMS 256
/* A build uses at most this many CPU counts of a frame (1.5 ms at 46.875 MHz). A mesh that is started is
 * finished. */
#define BUILD_COUNTS 70312
/* A rebuild waits this many frames after the last one that drew the cache. A frame's list is sent at the start
 * of the next frame's build and has run by the end of the one after. */
#define CACHE_WAIT_FRAMES 3

/* The kinds of faces, by the normal's y. A floor is above 0.3 (the game lets such a face carry Glover,
 * collision.c, CheckCollisions). It is steep from 1 - 0.25 / handSlideScale down (CheckPlaneForSliding: Glover
 * slides on it). A ceiling is below -0.3. Each kind has its own list in a shape. */
enum Group { GROUP_FLAT, GROUP_STEEP, GROUP_WALL, GROUP_CEILING, GROUPS };
#define FLOOR_Y 0.3f
#define CEILING_Y -0.3f
#define NO_LIST 0xFFFF
#define EDGES_NOT_BUILT 0xFFFF

/* The vertex cache of F3DEX 1.21 holds 32 vertices. */
#define BATCH_VERTS 32
#define BATCH_TRIS 64

/* Each frame. */
#define MAX_MOVING 64  /* platforms drawn */
#define SCENE_SIZE 896
#define MAX_SPHERES 40 /* the glove's and the ball's first, then the enemies' */
#define SPHERE_VERTS 12
#define SPHERE_FACES 20
#define MAX_CONTACTS 8
#define EXTRA_SIZE 224 /* the longest list: set-up 23, spheres 92, touched faces 12, holes 80, end */
#define MAX_ENEMY_ACTORS 400
/* The enemy types of the game's ActorType: SNEEZY (5) to NME_FROG (58), without the unused 6. */
#define SNEEZY_TYPE 5
#define NME_FROG_TYPE 58

/* An open edge is a band this wide along the edge, inside its face. */
#define EDGE_WIDTH 3.0f
#define EDGE_RGBA 0xFF20A0FF /* hot pink */
/* The open edges of a mesh are found with a hash table of its edges. A mesh with more faces is left out. While a
 * shape is built, the table and the batch slot of each mesh vertex take the unused end of the cache's vertices. */
#define EDGE_MESH_FACES 512
/* The open edges leave this much of the cache for the faces of actors that come later. */
#define EDGE_RESERVE_VTX 192
#define EDGE_RESERVE_GFX 96
/* A marked hole is a post of this height and half width, violet and opaque, standing on the hole's centre. */
#define HOLE_HEIGHT 24.0f
#define HOLE_HALF_WIDTH 1.5f
#define HOLE_RGBA 0xB050FFFF
#define HOLE_VERTS 4
#define HOLE_BATCH 7

/* The alpha of faces: the front, and the back in "faint" (as a fraction of 255 of the front's). */
#define FACE_ALPHA 0x60
#define BACK_ENV 0x6B /* 0x60 * 0x6B / 255 is about 0x28 */
#define CONTACT_ALPHA 0xA0
#define ENEMY_RGBA 0xFF603000

/* Vertices that are made each frame (the touched faces, the holes) are in half units around Glover, as s16. */
#define SCALE 2.0f
#define MAX_COORD 32000.0f
/* In "on surfaces" mode the faces are pulled this far towards the camera in the viewport's Z units. The depth is
 * not linear: 2 units is about 13 world units at 300 units from the camera. */
#define Z_BIAS 2
/* A place in a matrix is 16.16 fixed point (guMtxF2L), which traps at 32768 or more. */
#define MAX_CENTRE 30000.0f
/* A coordinate of a mesh vertex is below this: no level has one near it. */
#define MAX_MESH 1000000.0f

/* Render modes: the game's translucent surface (force blend, full coverage, image read), and the same with Z
 * compare in the translucent Z mode. */
#define RENDER_XLU 0x00504240
#define RENDER_XLU_Z 0x00504A50

/* The viewport of the game's InitViewport: 320 by 240, the whole Z range. */
#define VP_X 640
#define VP_Y 480
#define VP_Z 511

CollStats collStats;

typedef struct Shape {
    const Mesh *mesh;
    u32 key;          /* the kinds of its faces and its scale */
    s16 numFaces;     /* the mesh's, as it was built */
    const void *verts;
    const void *faces;
    u16 list[GROUPS]; /* where each kind's list starts in cacheGfx, or NO_LIST */
    u16 count[GROUPS]; /* the faces of each kind */
    u16 edgeList;     /* where the list of its open edges starts in cacheGfx, or NO_LIST */
    u16 edges;        /* its open edges */
    u8 record;        /* the record of the first actor that uses it */
    s8 shift;         /* the vertices are the mesh's coordinates times 2^shift */
} Shape;

/* tests/collview.py and collview2.py read the cache. */
_Static_assert(sizeof(Shape) == 44 && __builtin_offsetof(Shape, list) == 20 && __builtin_offsetof(Shape, edgeList) == 36 &&
                   __builtin_offsetof(Shape, shift) == 41,
               "the tests know Shape's layout");

typedef struct Record {
    const Actor *actor;
    const Mesh *mesh;
    u8 shape;
    u8 terrain;
    u8 platform; /* 1: a platform, which moves; its matrix is made each frame */
    u8 colour;   /* its colour in the "one colour per object" mode */
    u8 bits;     /* the face filter's terrain bits (CollFaceBit) */
    u16 matrix;  /* a landscape actor's matrix in staticMtx */
    u32 special; /* the colour of its terrain row in the terrain mode, or 0 for colours by the slope */
} Record;

_Static_assert(sizeof(Record) == 20 && __builtin_offsetof(Record, matrix) == 14, "the tests know Record's layout");

static Vtx cacheVtx[CACHE_VTX] __attribute__((aligned(16)));
static Gfx cacheGfx[CACHE_GFX] __attribute__((aligned(16)));
static Mtx staticMtx[MAX_STATIC] __attribute__((aligned(16)));
static Shape shapes[MAX_SHAPES];
static Record records[MAX_RECORDS];
static u8 actorSlots[ACTOR_SLOTS]; /* a record's number plus 1, or 0 */
static int vtxUsed, gfxUsed, shapeCount, recordCount, staticCount;
static u8 cacheState;
static u8 cacheFull;
static u8 cacheLevel = 0xFF;
/* The open edges' lists come after the faces' lists of the shapes that existed when the switch was turned on.
 * They are built nearest to Glover first, until the cache is full. Turning the switch off and on again builds
 * them again around Glover's new place, if no faces were added after them. */
static int edgeVtxStart = -1, edgeGfxStart; /* where the first edge list starts, or -1 */
static u8 edgesFull;     /* 1: an edge list did not fit */
static u8 facesAfterEdges; /* 1: a shape's faces were built after the first edge list */
static u8 edgesWereOn;
static u8 edgeReset;     /* 1: the edge lists are to be dropped once no frame in flight draws them */
static u32 lastEdgesDrawn; /* practice.drawFrames of the last frame that drew an edge list */
static s32 cacheFrame;
static u32 lastDrawn;   /* practice.drawFrames of the last frame that drew the cache */
static u32 buildCounts; /* the counts of the build that runs */
static u16 builds;
static volatile u8 resetRequest;

/* What changes each frame: one set for each frame buffer. */
static Gfx scene[2][SCENE_SIZE] __attribute__((aligned(16)));
static Gfx extra[2][EXTRA_SIZE] __attribute__((aligned(16)));
static Mtx movingMtx[2][MAX_MOVING] __attribute__((aligned(16)));
static Mtx sphereMtx[2][MAX_SPHERES] __attribute__((aligned(16)));
static Vtx contactVtx[2][MAX_CONTACTS * 3] __attribute__((aligned(16)));
static Vtx holeVtx[2][HOLE_MAX * HOLE_VERTS] __attribute__((aligned(16)));
static Mtx gloverMtx[2] __attribute__((aligned(16)));
static Mtx projection[2] __attribute__((aligned(16)));
static Vp viewport[2] __attribute__((aligned(16)));

/* The build's work space for one mesh. */
static u8 faceGroup[MAX_MESH_FACES]; /* GROUPS: degenerate */
static u8 openBits[MAX_MESH_FACES];  /* bit j: the edge from corner j to corner j + 1 is open */

void CollViewReset(void) {
    resetRequest = 1;
}

static f32 Dot(const f32 *a, const f32 *b) {
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

static f32 Sqrt(f32 x) {
    return __builtin_sqrtf(x);
}

static void Cross(const f32 *a, const f32 *b, f32 *n) {
    n[0] = a[1] * b[2] - a[2] * b[1];
    n[1] = a[2] * b[0] - a[0] * b[2];
    n[2] = a[0] * b[1] - a[1] * b[0];
}

/* A difference of two game coordinates, with a tiny one taken as 0: products of up to four of them stay normal
 * numbers. */
static f32 Diff(f32 a, f32 b) {
    f32 d = a - b;
    return d < 1e-6f && d > -1e-6f ? 0.0f : d;
}

static int InRam(const void *p, u32 bytes, u32 align) {
    u32 a = (u32)p;

    return (a & (align - 1)) == 0 && a >= 0x80000000 && a < 0x80800000 && bytes <= 0x800000 &&
           a + bytes <= 0x80800000;
}

/* The mesh of an actor's model, or NULL. */
static const Mesh *ActorMesh(const Actor *a) {
    const ObjectInstance *inst = a->inst;

    if (!SafePointer(inst) || !SafePointer(inst->obj) || !SafePointer(inst->obj->mesh)) {
        return NULL;
    }
    return inst->obj->mesh;
}

/* The rotation matrix of QuaternionToMatrix (maths.c) for the actor's rotQuat. 0 if it is not a rotation: the
 * game divides by the squared size of the quaternion, and a size far from 1 is not one. */
static int ActorRotation(const Actor *a, f32 m[3][3]) {
    f32 q[4], s, xs, ys, zs, wx, wy, wz, xx, xy, xz, yy, yz, zz;

    for (int i = 0; i < 4; i++) {
        if (!ReadFloat(&a->rotQuat[i], &q[i])) {
            return 0;
        }
    }
    s = q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3];
    if (!(s > 0.25f && s < 4.0f)) {
        return 0;
    }
    s = 2.0f / s;
    xs = q[0] * s;
    ys = q[1] * s;
    zs = q[2] * s;
    wx = q[3] * xs;
    wy = q[3] * ys;
    wz = q[3] * zs;
    xx = q[0] * xs;
    xy = q[0] * ys;
    xz = q[0] * zs;
    yy = q[1] * ys;
    yz = q[1] * zs;
    zz = q[2] * zs;
    m[0][0] = 1.0f - (yy + zz);
    m[0][1] = xy + wz;
    m[0][2] = xz - wy;
    m[1][0] = xy - wz;
    m[1][1] = 1.0f - (xx + zz);
    m[1][2] = yz + wx;
    m[2][0] = xz + wy;
    m[2][1] = yz - wx;
    m[2][2] = 1.0f - (xx + yy);
    return 1;
}

static int ReadVec(const f32 *p, f32 *out) {
    return ReadFloat(&p[0], &out[0]) && ReadFloat(&p[1], &out[1]) && ReadFloat(&p[2], &out[2]);
}

/* The matrix that places a shape's vertices as the game places the mesh: each vertex scaled, turned, moved to the
 * actor's position. The shape's vertices are 2^shift times the mesh's. 0 if it cannot be made. */
static int PlaceMatrix(const Actor *a, int shift, Mtx *out) {
    f32 rot[3][3], scale[3], pos[3], mf[4][4];
    f32 k = shift >= 0 ? 1.0f / (1 << shift) : (f32)(1 << -shift);

    if (!ActorRotation(a, rot) || !ReadVec(&a->scale.x, scale) || !ReadVec(&a->pos.x, pos)) {
        return 0;
    }
    for (int i = 0; i < 3; i++) {
        f32 size = scale[i] * k;
        if (!(pos[i] < MAX_CENTRE && pos[i] > -MAX_CENTRE && size < 16384.0f && size > -16384.0f)) {
            return 0;
        }
        for (int j = 0; j < 3; j++) {
            mf[i][j] = size * rot[i][j];
        }
        mf[i][3] = 0.0f;
        mf[3][i] = pos[i];
    }
    mf[3][3] = 1.0f;
    guMtxF2L(mf, out);
    return 1;
}

/* ---- Building the cache ---------------------------------------------------------------------------------- */

static u32 ActorSlot(const Actor *a) {
    return ((u32)a >> 2) * 2654435761u >> (32 - 9);
}

/* The record of an actor, or NULL. */
static Record *FindRecord(const Actor *a) {
    u32 slot = ActorSlot(a);

    for (int n = 0; n < ACTOR_SLOTS && actorSlots[slot]; n++, slot = (slot + 1) & (ACTOR_SLOTS - 1)) {
        Record *r = &records[actorSlots[slot] - 1];
        if (r->actor == a) {
            return r;
        }
    }
    return NULL;
}

/* Puts record R in the table. */
static void PutRecord(Record *r) {
    u32 slot = ActorSlot(r->actor);

    for (int n = 0; n < ACTOR_SLOTS; n++, slot = (slot + 1) & (ACTOR_SLOTS - 1)) {
        if (!actorSlots[slot]) {
            actorSlots[slot] = r - records + 1;
            return;
        }
    }
}

/* Marks the open edges of the mesh in openBits: an edge is open when only one face uses it. Faces share an edge
 * when they share its two vertex numbers. EDGESLOTS is a table of 2^BITS slots, at least 4 for each face: each
 * holds an edge's two vertex numbers, and in the top bits how often it was seen. */
static void FindOpenEdges(const s16 (*f)[3], int nf, u32 *edgeSlots, int bits) {
    const u32 empty = 0xFFFFFFFF;
    const u32 mask = (1u << bits) - 1;

    for (u32 i = 0; i <= mask; i++) {
        edgeSlots[i] = empty;
    }
    /* Pass 0 counts each edge; pass 1 reads the counts. The low 30 bits hold the two vertex numbers (each below
     * 2^15), the top two bits the count less 1, at most 1. */
    for (int pass = 0; pass < 2; pass++) {
        for (int i = 0; i < nf; i++) {
            u8 open = 0;
            for (int j = 0; j < 3; j++) {
                int a = f[i][j], b = f[i][j == 2 ? 0 : j + 1];
                if (a == b || a < 0 || b < 0) {
                    continue;
                }
                u32 key = a < b ? (u32)a << 15 | (u32)b : (u32)b << 15 | (u32)a;
                u32 slot = (key * 2654435761u) >> (32 - bits);
                while (edgeSlots[slot] != empty && (edgeSlots[slot] & 0x3FFFFFFF) != key) {
                    slot = (slot + 1) & mask;
                }
                if (pass == 0) {
                    edgeSlots[slot] = edgeSlots[slot] == empty ? key : key | 0x40000000;
                } else if (!(edgeSlots[slot] & 0x40000000)) {
                    open |= 1 << j;
                }
            }
            if (pass == 1) {
                openBits[i] = open;
            }
        }
    }
}

/* The batch: up to 32 vertices for one vertex load, and the triangles that use them. A vertex of the mesh is
 * loaded once in a batch; the inner corners of the open edges' bands are not shared. */
typedef struct Batch {
    int verts;
    s16 pos[BATCH_VERTS][3];
    int tris;
    u8 tri[BATCH_TRIS][3];
} Batch;

/* The build's scratch for one shape, in the unused end of cacheVtx: for each mesh vertex, the batch that loaded
 * it (above bit 5) and its slot; for the open edges, also the table of FindOpenEdges below it. */
static u16 *vertSlot;
static u16 batchNumber;
static int vtxLimit; /* the scratch starts here */
static int gfxLimit; /* and the commands end here */

typedef struct Builder {
    const Vec3 *v;
    const s16 (*f)[3];
    int nv, nf;
    f32 scale[3];
    f32 mult; /* 2^shift */
    int ok;   /* 0 once something did not fit */
    int edgeCount;
} Builder;

/* A vertex of the mesh, read and checked. */
static int MeshVertex(const Builder *b, int i, f32 *out) {
    if (i < 0 || i >= b->nv || !ReadVec(&b->v[i].x, out)) {
        return 0;
    }
    for (int k = 0; k < 3; k++) {
        if (!(out[k] < MAX_MESH && out[k] > -MAX_MESH)) {
            return 0;
        }
    }
    return 1;
}

static void Put(Gfx g) {
    cacheGfx[gfxUsed++] = g;
}

/* Combiner inputs. */
#define CC_PRIM 3
#define CC_SHADE 4
#define CC_ENV 5
#define CC_ZERO 7

/* The combiner: colour (0 - 0) * 0 + C, alpha (AA - 0) * AC + AD, the same in both cycles. */
static Gfx Combine(u32 c, u32 aa, u32 ac, u32 ad) {
    return GfxCmd(Bits(CMD_SETCOMBINE, 24, 8) | Bits(15, 20, 4) | Bits(31, 15, 5) | Bits(aa, 12, 3) | Bits(ac, 9, 3) |
                      Bits(15, 5, 4) | Bits(31, 0, 5),
                  Bits(15, 28, 4) | Bits(15, 24, 4) | Bits(aa, 21, 3) | Bits(ac, 18, 3) | Bits(c, 15, 3) |
                      Bits(7, 12, 3) | Bits(ad, 9, 3) | Bits(c, 6, 3) | Bits(7, 3, 3) | Bits(ad, 0, 3));
}

/* The faces: the primitive colour, with its alpha times the environment's. The edges: the vertices' colour, with
 * their alpha times the environment's. The touched faces and the holes: the vertices' colour and alpha. */
#define FACE_COMBINE Combine(CC_PRIM, CC_PRIM, CC_ENV, CC_ZERO)
#define EDGE_COMBINE Combine(CC_SHADE, CC_SHADE, CC_ENV, CC_ZERO)
#define SHADE_COMBINE Combine(CC_SHADE, CC_ZERO, CC_ZERO, CC_SHADE)
/* The commands after the last batch of a list: an edge list ends with the faces' combiner. */
#define LIST_END_COMMANDS 3

/* Writes the batch's vertices and its commands to the cache. */
static void FlushBatch(Builder *b, Batch *t) {
    if (t->verts == 0) {
        return;
    }
    if (vtxUsed + t->verts > vtxLimit || gfxUsed + 1 + (t->tris + 1) / 2 + LIST_END_COMMANDS > gfxLimit) {
        b->ok = 0;
    }
    if (b->ok) {
        Vtx *v = &cacheVtx[vtxUsed];
        for (int i = 0; i < t->verts; i++) {
            v[i].x = t->pos[i][0];
            v[i].y = t->pos[i][1];
            v[i].z = t->pos[i][2];
            v[i].flag = 0;
            v[i].s = v[i].t = 0;
            /* Only the edges use the vertices' colour. The faces use the primitive colour. */
            v[i].r = (u8)(EDGE_RGBA >> 24);
            v[i].g = (u8)(EDGE_RGBA >> 16);
            v[i].b = (u8)(EDGE_RGBA >> 8);
            v[i].a = (u8)EDGE_RGBA;
        }
        vtxUsed += t->verts;
        Put(GfxVertices(v, t->verts));
        for (int i = 0; i < t->tris; i += 2) {
            const u8 *p = t->tri[i], *q = t->tri[i + 1];
            Put(i + 1 < t->tris ? GfxTriangles(p[0], p[1], p[2], q[0], q[1], q[2]) : GfxTriangle(p[0], p[1], p[2]));
        }
    }
    t->verts = t->tris = 0;
    batchNumber++;
}

/* The batch slot of mesh vertex I (or of a new inner corner, -1) at P, loaded if it is not yet. */
static int BatchVertex(Batch *t, int i, const f32 *p, f32 mult) {
    if (i >= 0) {
        if (vertSlot[i] >> 5 == batchNumber) {
            return vertSlot[i] & 31;
        }
        vertSlot[i] = batchNumber << 5 | t->verts;
    }
    for (int k = 0; k < 3; k++) {
        t->pos[t->verts][k] = (s16)(p[k] * mult);
    }
    return t->verts++;
}

static void AddTriangle(Batch *t, int a, int b, int c) {
    t->tri[t->tris][0] = a;
    t->tri[t->tris][1] = b;
    t->tri[t->tris][2] = c;
    t->tris++;
}

/* Reads face I's corners. 0 if one cannot be read. */
static int FaceCorners(const Builder *b, int i, f32 c[3][3]) {
    for (int j = 0; j < 3; j++) {
        if (!MeshVertex(b, b->f[i][j], c[j])) {
            return 0;
        }
    }
    return 1;
}

static void AddFace(Builder *b, Batch *t, int i) {
    const s16 *f = b->f[i];
    f32 c[3][3];
    int need = 0, slot[3];

    if (!FaceCorners(b, i, c)) {
        return;
    }
    for (int j = 0; j < 3; j++) {
        need += vertSlot[f[j]] >> 5 != batchNumber;
    }
    if (t->verts + need > BATCH_VERTS || t->tris + 1 > BATCH_TRIS) {
        FlushBatch(b, t);
    }
    for (int j = 0; j < 3; j++) {
        slot[j] = BatchVertex(t, f[j], c[j], b->mult);
    }
    AddTriangle(t, slot[0], slot[1], slot[2]);
}

/* Adds the bands of face I's open edges: each is a quad from the edge to the same edge moved EDGE_WIDTH into the
 * face. The inner corners are the face's corners pulled towards its incentre, which keeps every band the same
 * width. A band turns the same way as its face, so it is culled with it. */
static void AddEdges(Builder *b, Batch *t, int i) {
    const s16 *f = b->f[i];
    u8 open = openBits[i], corners = 0;
    f32 c[3][3], e[3][3], len[3], n[3], inner[3][3], perimeter, inradius, k;
    int need = 0, bands = 0, slot[3], in[3];

    if (!open || !FaceCorners(b, i, c)) {
        return;
    }
    /* The lengths are in world units: the mesh's corners scaled as the actor scales them. */
    for (int j = 0; j < 3; j++) {
        int j1 = j == 2 ? 0 : j + 1;
        for (int a = 0; a < 3; a++) {
            e[j][a] = Diff(c[j1][a] * b->scale[a], c[j][a] * b->scale[a]); /* the edge from corner j */
        }
        len[j] = Sqrt(Dot(e[j], e[j]));
        if (open & (1 << j)) {
            corners |= 1 << j | 1 << j1;
            bands += 2;
        }
    }
    Cross(e[0], e[1], n);
    perimeter = len[0] + len[1] + len[2];
    if (!(perimeter > 0.01f)) {
        return;
    }
    inradius = Sqrt(Dot(n, n)) / perimeter;
    k = inradius > EDGE_WIDTH ? 1.0f - EDGE_WIDTH / inradius : 0.0f;
    /* The incentre weighs each corner by the length of the edge across from it. */
    for (int a = 0; a < 3; a++) {
        f32 centre = (len[1] * c[0][a] + len[2] * c[1][a] + len[0] * c[2][a]) / perimeter;
        for (int j = 0; j < 3; j++) {
            inner[j][a] = centre + (c[j][a] - centre) * k;
        }
    }
    for (int j = 0; j < 3; j++) {
        if (corners & (1 << j)) {
            need += (vertSlot[f[j]] >> 5 != batchNumber) + 1;
        }
    }
    if (t->verts + need > BATCH_VERTS || t->tris + bands > BATCH_TRIS) {
        FlushBatch(b, t);
    }
    for (int j = 0; j < 3; j++) {
        if (corners & (1 << j)) {
            slot[j] = BatchVertex(t, f[j], c[j], b->mult);
            in[j] = BatchVertex(t, -1, inner[j], b->mult);
        }
    }
    for (int j = 0; j < 3; j++) {
        int j1 = j == 2 ? 0 : j + 1;
        if (open & (1 << j)) {
            AddTriangle(t, slot[j], slot[j1], in[j1]);
            AddTriangle(t, slot[j], in[j1], in[j]);
            b->edgeCount++;
        }
    }
}

/* Sets up the scratch for a build, with EXTRA more vertices of room below it. 0 if there is no room. */
static int BeginBuild(Builder *b, int extra) {
    int slotVtx = (b->nv * 2 + sizeof(Vtx) - 1) / sizeof(Vtx);

    vtxLimit = CACHE_VTX - slotVtx;
    if (vtxUsed + slotVtx + extra > CACHE_VTX) {
        return 0;
    }
    vertSlot = (u16 *)&cacheVtx[vtxLimit];
    gfxLimit = CACHE_GFX;
    for (int i = 0; i < b->nv; i++) {
        vertSlot[i] = 0;
    }
    batchNumber = 1;
    b->ok = 1;
    b->edgeCount = 0;
    return 1;
}

/* The group of face I as the actor places it: B's scale scales it and ROT turns it. FLAT_Y is the normal's y
 * above which a floor is flat. GROUPS for a face that cannot be drawn. */
static int FaceGroup(const Builder *b, const f32 rot[3][3], f32 flatY, int i) {
    f32 c[3][3], p[3][3], e1[3], e2[3], n[3], len2;

    if (!FaceCorners(b, i, c)) {
        return GROUPS;
    }
    for (int j = 0; j < 3; j++) {
        for (int k = 0; k < 3; k++) {
            p[j][k] = rot[0][k] * c[j][0] * b->scale[0] + rot[1][k] * c[j][1] * b->scale[1] +
                      rot[2][k] * c[j][2] * b->scale[2];
        }
    }
    for (int k = 0; k < 3; k++) {
        e1[k] = Diff(p[1][k], p[0][k]);
        e2[k] = Diff(p[2][k], p[0][k]);
    }
    Cross(e1, e2, n);
    len2 = Dot(n, n);
    if (!(len2 > 1e-8f)) {
        return GROUPS;
    }
    f32 y = n[1] / Sqrt(len2);
    return y > flatY ? GROUP_FLAT : y > FLOOR_Y ? GROUP_STEEP : y >= CEILING_Y ? GROUP_WALL : GROUP_CEILING;
}

static Batch batch;
static Builder builder;

/* The build that runs: one mesh at a time, a few faces at a time, over as many frames as it needs. It finds the
 * group of each face, then builds the shape's lists one group after another (or finds a shape to share), or it
 * builds a shape's open edges. */
enum JobStep { JOB_NONE, JOB_GROUPS, JOB_FACES, JOB_EDGES };
#define JOB_FACES_A_STEP 16 /* faces between two looks at the time */
/* Record.shape while its shape is built, and for an actor that cannot be drawn. */
#define SHAPE_BUILDING 0xFE
#define NO_SHAPE 0xFF

static struct Job {
    u8 step;
    u8 group;
    int face;
    int count; /* faces of the group so far */
    int vtxStart, gfxStart;
    Record *record;
    Shape *shape;
    u32 key;
    f32 rot[3][3];
    f32 flatY;
    int shift;
} job;

/* Drops what the job wrote to the cache. */
static void JobUndo(void) {
    vtxUsed = job.vtxStart;
    gfxUsed = job.gfxStart;
}

/* Ends the job of a record with shape SHAPE, or with none: -1 when the cache is full, -2 when the actor cannot
 * be drawn. */
static void EndRecord(int shape) {
    Record *r = job.record;

    job.step = JOB_NONE;
    r->shape = NO_SHAPE;
    if (shape == -1) {
        cacheFull = 1;
    }
    if (shape < 0) {
        return;
    }
    if (!r->platform) {
        if (staticCount >= MAX_STATIC) {
            cacheFull = 1;
            return;
        }
        if (!PlaceMatrix(r->actor, shapes[shape].shift, &staticMtx[staticCount])) {
            return;
        }
        osWritebackDCache(&staticMtx[staticCount], sizeof(Mtx));
        r->matrix = staticCount++;
    }
    r->shape = shape;
}

/* The groups are known: shares a shape with the same mesh, scale and groups, or starts a new one. */
static void StartShape(void) {
    Builder *b = &builder;
    const Mesh *m = job.record->mesh;
    f32 most = 0.0f;

    for (int k = 0; k < 3; k++) {
        union {
            f32 f;
            u32 u;
        } bits = { b->scale[k] };
        job.key = job.key * 31 + bits.u;
    }
    for (int i = 0; i < shapeCount; i++) {
        const Shape *s = &shapes[i];
        if (s->mesh == m && s->key == job.key && s->numFaces == b->nf && s->verts == b->v && s->faces == b->f) {
            EndRecord(i);
            return;
        }
    }
    if (shapeCount >= MAX_SHAPES) {
        EndRecord(-1);
        return;
    }
    for (int i = 0; i < b->nv; i++) {
        f32 v[3];
        if (MeshVertex(b, i, v)) {
            for (int k = 0; k < 3; k++) {
                f32 a = v[k] < 0.0f ? -v[k] : v[k];
                most = a > most ? a : most;
            }
        }
    }
    /* The most precision for which every vertex fits an s16. */
    for (job.shift = 6; job.shift > -6 && most * (job.shift >= 0 ? (f32)(1 << job.shift) : 1.0f / (1 << -job.shift)) > 30000.0f;
         job.shift--) {
    }
    b->mult = job.shift >= 0 ? (f32)(1 << job.shift) : 1.0f / (1 << -job.shift);
    job.vtxStart = vtxUsed;
    job.gfxStart = gfxUsed;
    if (!BeginBuild(b, 0)) {
        EndRecord(-1);
        return;
    }
    job.step = JOB_FACES;
    job.group = 0;
    job.face = job.count = 0;
    batch.verts = batch.tris = 0;
    shapes[shapeCount].list[0] = gfxUsed;
}

/* The last group is built: the shape is complete. */
static void EndShape(void) {
    Builder *b = &builder;
    Shape *s = &shapes[shapeCount];

    s->mesh = job.record->mesh;
    s->key = job.key;
    s->numFaces = b->nf;
    s->verts = b->v;
    s->faces = b->f;
    s->edgeList = NO_LIST;
    s->edges = EDGES_NOT_BUILT;
    s->record = job.record - records;
    s->shift = job.shift;
    facesAfterEdges |= edgeVtxStart >= 0;
    osWritebackDCache(&cacheVtx[job.vtxStart], (vtxUsed - job.vtxStart) * sizeof(Vtx));
    osWritebackDCache(&cacheGfx[job.gfxStart], (gfxUsed - job.gfxStart) * sizeof(Gfx));
    EndRecord(shapeCount++);
}

/* Builds the next faces of the shape's list of the job's group. */
static void FacesStep(void) {
    Builder *b = &builder;
    Shape *s = &shapes[shapeCount];

    for (int n = 0; n < JOB_FACES_A_STEP && job.face < b->nf; n++, job.face++) {
        if (faceGroup[job.face] == job.group) {
            AddFace(b, &batch, job.face);
            job.count++;
        }
    }
    if (job.face < b->nf) {
        return;
    }
    FlushBatch(b, &batch);
    if (job.count && b->ok) {
        Put(GfxEndDisplayList());
    }
    if (!b->ok) {
        JobUndo();
        EndRecord(-1);
        return;
    }
    s->list[job.group] = job.count ? s->list[job.group] : NO_LIST;
    s->count[job.group] = job.count;
    job.face = job.count = 0;
    if (++job.group == GROUPS) {
        EndShape();
    } else {
        s->list[job.group] = gfxUsed;
    }
}

/* Builds the bands of the next faces' open edges. */
static void EdgesStep(void) {
    Builder *b = &builder;
    Shape *s = job.shape;

    for (int n = 0; n < JOB_FACES_A_STEP && job.face < b->nf && b->ok; n++, job.face++) {
        AddEdges(b, &batch, job.face);
    }
    if (b->ok && job.face < b->nf) {
        return;
    }
    FlushBatch(b, &batch);
    job.step = JOB_NONE;
    if (!b->ok || b->edgeCount == 0) {
        JobUndo();
        edgesFull |= !b->ok;
        return;
    }
    Put(GfxSync(CMD_PIPESYNC));
    Put(FACE_COMBINE);
    Put(GfxEndDisplayList());
    s->edgeList = job.gfxStart;
    s->edges = b->edgeCount;
    if (edgeVtxStart < 0) {
        edgeVtxStart = job.vtxStart;
        edgeGfxStart = job.gfxStart;
    }
    osWritebackDCache(&cacheVtx[job.vtxStart], (vtxUsed - job.vtxStart) * sizeof(Vtx));
    osWritebackDCache(&cacheGfx[job.gfxStart], (gfxUsed - job.gfxStart) * sizeof(Gfx));
}

/* Runs the job for at most COUNTS from START (osGetCount). */
static void RunJob(u32 start, u32 counts) {
    Builder *b = &builder;

    while (job.step != JOB_NONE && osGetCount() - start < counts) {
        if (job.step == JOB_GROUPS) {
            for (int n = 0; n < JOB_FACES_A_STEP && job.face < b->nf; n++, job.face++) {
                int g = FaceGroup(b, job.rot, job.flatY, job.face);
                faceGroup[job.face] = g;
                job.key = job.key * 31 + g + 1;
            }
            if (job.face == b->nf) {
                StartShape();
            }
        } else if (job.step == JOB_FACES) {
            FacesStep();
        } else {
            EdgesStep();
        }
    }
}

/* Starts the list of shape S's open edges, which is drawn after its faces. */
static void StartEdges(Shape *s) {
    Builder *b = &builder;
    const Mesh *m = s->mesh;
    const Actor *a = records[s->record].actor;
    int nf = s->numFaces;

    s->edges = 0;
    /* The scale of the first actor that uses the shape gives the bands' width. */
    if (nf > EDGE_MESH_FACES || !SafePointer(m) || m->numFaces != nf || (const void *)m->verts != s->verts ||
        (const void *)m->faces != s->faces || !SafePointer(a) || !ReadVec(&a->scale.x, b->scale)) {
        return;
    }
    b->v = m->verts;
    b->f = m->faces;
    b->nv = m->numVerts;
    b->nf = nf;
    b->mult = s->shift >= 0 ? (f32)(1 << s->shift) : 1.0f / (1 << -s->shift);
    if (b->nv < 3 || !InRam(b->v, b->nv * sizeof(Vec3), 4) || !InRam(b->f, nf * sizeof(b->f[0]), 2)) {
        return;
    }
    int bits = 6;
    while ((1 << bits) < nf * 4) {
        bits++;
    }
    int tableVtx = (4 << bits) / sizeof(Vtx);
    job.vtxStart = vtxUsed;
    job.gfxStart = gfxUsed;
    /* The edges leave room for the faces of actors that come later. */
    if (!BeginBuild(b, tableVtx + EDGE_RESERVE_VTX) || gfxUsed + 2 + EDGE_RESERVE_GFX > CACHE_GFX) {
        edgesFull = 1;
        return;
    }
    vtxLimit -= EDGE_RESERVE_VTX;
    gfxLimit -= EDGE_RESERVE_GFX;
    FindOpenEdges(b->f, nf, (u32 *)&cacheVtx[vtxLimit - tableVtx], bits);
    Put(GfxSync(CMD_PIPESYNC));
    Put(EDGE_COMBINE);
    batch.verts = batch.tris = 0;
    job.step = JOB_EDGES;
    job.shape = s;
    job.face = 0;
}

/* The terrain bits of the face filter for terrain row T, and its colour in the terrain mode (0: by the slope). */
static u8 TerrainBits(u8 t, u32 *colour) {
    f32 slip, gravity;
    u8 bits = 0;

    *colour = 0;
    if (t == TERRAIN_WATER) {
        *colour = 0x30C0F0; /* cyan */
        bits |= COLL_WATER;
    }
    if (t < TERRAIN_COUNT && ReadFloat(&terrain[t].slip, &slip) && ReadFloat(&terrain[t].gravityScale, &gravity)) {
        if (slip > 1.0f) {
            bits |= COLL_SLIPPERY;
        }
        if (gravity < 1.0f) {
            bits |= COLL_LOW_GRAVITY;
        }
        if (*colour == 0) {
            *colour = slip > 1.0f      ? 0xE0F0FF /* slippery: pale blue */
                      : gravity < 1.0f ? 0xFF9020 /* low gravity: orange */
                      : slip < 1.0f    ? 0xD040D0 /* sticky: magenta */
                                       : 0;
        }
    }
    return bits;
}

/* The normal's y above which a floor of terrain row T is flat: Glover does not slide on it. */
static f32 FlatY(u8 t) {
    f32 slide, gravity;

    if (t < TERRAIN_COUNT && ReadFloat(&terrain[t].handSlideScale, &slide) &&
        ReadFloat(&terrain[t].gravityScale, &gravity) && gravity >= 1.0f && slide > 0.25f) {
        f32 y = 1.0f - 0.25f / slide;
        return y > FLOOR_Y ? y : FLOOR_Y;
    }
    /* No sliding on low gravity (CheckPlaneForSliding). */
    return FLOOR_Y;
}

static void CacheClear(void) {
    vtxUsed = gfxUsed = shapeCount = recordCount = staticCount = 0;
    cacheFull = edgesFull = facesAfterEdges = edgeReset = 0;
    edgeVtxStart = -1;
    job.step = JOB_NONE;
    for (int i = 0; i < ACTOR_SLOTS; i++) {
        actorSlots[i] = 0;
    }
}

/* Adds a record for actor A (PLATFORM: one of platformList), or brings its record R up to date when its mesh
 * changed, and starts the job of its shape. Returns NULL when the records are full. */
static Record *AddRecord(Record *r, const Actor *a, int platform) {
    Builder *b = &builder;
    const Mesh *m = ActorMesh(a);
    const CollFace *faces = a->faces;

    if (r == NULL) {
        if (recordCount >= MAX_RECORDS) {
            cacheFull = 1;
            return NULL;
        }
        r = &records[recordCount];
        r->actor = a;
        r->colour = recordCount;
        recordCount++;
        PutRecord(r);
    }
    r->mesh = m;
    r->shape = NO_SHAPE;
    r->platform = platform;
    r->terrain = SafePointer(faces) ? faces->terrain : 0;
    r->bits = TerrainBits(r->terrain, &r->special);
    if (m == NULL) {
        return r;
    }
    b->v = m->verts;
    b->f = m->faces;
    b->nv = m->numVerts;
    b->nf = m->numFaces;
    if (b->nf < 1 || b->nf > MAX_MESH_FACES || b->nv < 3 || !InRam(b->v, b->nv * sizeof(Vec3), 4) ||
        !InRam(b->f, b->nf * sizeof(b->f[0]), 2) || !ReadVec(&a->scale.x, b->scale) || !ActorRotation(a, job.rot)) {
        return r;
    }
    r->shape = SHAPE_BUILDING;
    job.step = JOB_GROUPS;
    job.record = r;
    job.face = 0;
    job.key = 0;
    job.flatY = FlatY(r->terrain);
    return r;
}

/* ---- Each frame ------------------------------------------------------------------------------------------ */

typedef struct Frame {
    int b;            /* the frame buffer's set */
    u32 start;        /* osGetCount at the start */
    f32 c[3];         /* Glover */
    f32 cam[3];       /* the camera, for the holes' posts */
    f32 radius;       /* 0: the whole level */
    Gfx *g, *end;     /* the frame's scene list */
    int moving;       /* platform matrices used */
    int pending;      /* actors that waited for the next frame's build time */
    u32 buildCounts;  /* the build's time on this frame */
    u32 sphereColour; /* the last colour set for a sphere */
    CollStats stats;
} Frame;

static const s16 radii[COLL_RADIUSES] = { 0, 1000, 500, 250 };

/* The colours of the "one colour per object" mode. */
static const u32 objectColours[] = { 0xFF5050, 0x50E050, 0x5080FF, 0xFFD040, 0xE050E0, 0x40E0E0,
                                     0xFF9030, 0xA070FF, 0xA0FF60, 0xFF70B0, 0x70B0FF, 0xE0E0E0 };

static u32 GroupColour(const Record *r, int g) {
    static const u32 slope[GROUPS] = { 0x40E040, 0xC0E030, 0x4070FF, 0xFF4040 };
    static const u32 kind[GROUPS] = { 0x40E040, 0x40E040, 0x4070FF, 0xFF4040 };
    if (run.collColour == COLL_COLOUR_OBJECT) {
        return objectColours[r->colour % ARRAY_COUNT(objectColours)];
    }
    if (run.collColour == COLL_COLOUR_KIND) {
        return kind[g];
    }
    return r->special ? r->special : slope[g];
}

/* Looks up actor A's record, starting its build if no build runs and there is build time left, and adds its
 * lists to the scene. */
static void VisitActor(Frame *fr, const Actor *a, int platform) {
    Record *r = FindRecord(a);
    const Mesh *m = ActorMesh(a);
    const Shape *s;

    if (r == NULL || r->mesh != m ||
        (r->shape < shapeCount && m != NULL &&
         (shapes[r->shape].numFaces != m->numFaces || shapes[r->shape].verts != m->verts ||
          shapes[r->shape].faces != m->faces))) {
        if (cacheFull) {
            return;
        }
        if (job.step != JOB_NONE || fr->buildCounts >= BUILD_COUNTS) {
            fr->pending++;
            return;
        }
        u32 t0 = osGetCount();
        r = AddRecord(r, a, platform);
        RunJob(t0, BUILD_COUNTS - fr->buildCounts);
        fr->buildCounts += osGetCount() - t0;
        if (r == NULL) {
            return;
        }
    }
    if (r->shape == SHAPE_BUILDING) {
        fr->pending++;
        return;
    }
    if (r->shape == NO_SHAPE) {
        fr->stats.bad++;
        return;
    }
    if (cacheState != COLL_CACHE_READY) {
        return;
    }
    s = &shapes[r->shape];
    u8 skip = run.collFaces;
    if ((skip & (platform ? COLL_PLATFORMS : COLL_LANDSCAPE)) || (skip & r->bits)) {
        return;
    }
    if (fr->radius > 0.0f) {
        f32 p[3], radius, d[3];
        if (!ReadVec(&a->pos.x, p) || !ReadFloat(&a->radius, &radius)) {
            fr->stats.bad++;
            return;
        }
        for (int k = 0; k < 3; k++) {
            d[k] = p[k] - fr->c[k];
        }
        f32 reach = fr->radius + (radius > 0.0f ? radius : 0.0f);
        if (Dot(d, d) > reach * reach) {
            return;
        }
    }
    /* A matrix, then for each kind a colour (when it changes) and the list. */
    if (fr->g + 2 + GROUPS * 2 >= fr->end) {
        fr->stats.full |= 4;
        return;
    }
    const Mtx *mtx;
    if (platform) {
        if (fr->moving >= MAX_MOVING) {
            fr->stats.full |= 4;
            return;
        }
        Mtx *mm = &movingMtx[fr->b][fr->moving];
        if (!PlaceMatrix(a, s->shift, mm)) {
            fr->stats.bad++;
            return;
        }
        fr->moving++;
        mtx = mm;
    } else {
        mtx = &staticMtx[r->matrix];
    }
    Gfx *start = fr->g;
    u32 last = 0xFFFFFFFF;
    *fr->g++ = GfxMatrix(mtx, MTX_LOAD);
    for (int g = 0; g < GROUPS; g++) {
        if (s->list[g] == NO_LIST || (skip & (g <= GROUP_STEEP ? COLL_FLOORS : g == GROUP_WALL ? COLL_WALLS
                                                                                              : COLL_CEILINGS))) {
            continue;
        }
        u32 colour = GroupColour(r, g);
        if (colour != last) {
            /* No pipe sync: the RDP takes the primitive colour with each primitive. */
            *fr->g++ = GfxSetPrimColor(colour << 8 | FACE_ALPHA);
            last = colour;
        }
        *fr->g++ = GfxDisplayList(&cacheGfx[s->list[g]]);
        fr->stats.faces += s->count[g];
    }
    if (last == 0xFFFFFFFF) {
        fr->g = start; /* every kind of its faces is left out */
        return;
    }
    if (run.collEdges && s->edgeList != NO_LIST && !edgeReset) {
        lastEdgesDrawn = practice.drawFrames;
        *fr->g++ = GfxDisplayList(&cacheGfx[s->edgeList]);
        fr->stats.edges += s->edges;
    }
    fr->stats.actors++;
}

/* Builds the open edges of the shapes nearest to Glover first, in the build's time left on this frame. */
static void BuildEdgesNear(Frame *fr) {
    if (!edgesWereOn && edgesFull && !facesAfterEdges && edgeVtxStart >= 0) {
        edgeReset = 1;
    }
    if (edgeReset) {
        if (job.step != JOB_NONE || practice.drawFrames - lastEdgesDrawn < CACHE_WAIT_FRAMES) {
            return;
        }
        vtxUsed = edgeVtxStart;
        gfxUsed = edgeGfxStart;
        edgeVtxStart = -1;
        for (int i = 0; i < shapeCount; i++) {
            shapes[i].edgeList = NO_LIST;
            shapes[i].edges = EDGES_NOT_BUILT;
        }
        edgesFull = edgeReset = 0;
    }
    u32 t0 = osGetCount();
    while (!edgesFull && job.step == JOB_NONE && fr->buildCounts + (osGetCount() - t0) < BUILD_COUNTS) {
        f32 best = 0.0f;
        int pick = -1;
        for (int i = 0; i < recordCount; i++) {
            const Record *r = &records[i];
            f32 p[3], d[3];
            if (r->shape >= shapeCount || shapes[r->shape].edges != EDGES_NOT_BUILT || !SafePointer(r->actor) ||
                !ReadVec(&r->actor->pos.x, p)) {
                continue;
            }
            for (int k = 0; k < 3; k++) {
                d[k] = Diff(p[k], fr->c[k]);
            }
            f32 dd = Dot(d, d);
            if (pick < 0 || dd < best) {
                best = dd;
                pick = r->shape;
            }
        }
        if (pick < 0) {
            break;
        }
        StartEdges(&shapes[pick]);
        RunJob(t0, BUILD_COUNTS - fr->buildCounts);
    }
    fr->buildCounts += osGetCount() - t0;
}

/* ---- The glove's, the ball's and the enemies' spheres ---------------------------------------------------- */

/* An icosahedron of unit size, its 12 corners in thousandths and its 20 faces. */
static const s16 icoCorners[SPHERE_VERTS][3] = {
    { -526, 851, 0 }, { 526, 851, 0 },  { -526, -851, 0 }, { 526, -851, 0 }, { 0, -526, 851 }, { 0, 526, 851 },
    { 0, -526, -851 }, { 0, 526, -851 }, { 851, 0, -526 }, { 851, 0, 526 }, { -851, 0, -526 }, { -851, 0, 526 },
};
static const u8 icoFaces[SPHERE_FACES][3] = {
    { 0, 11, 5 }, { 0, 5, 1 },  { 0, 1, 7 },  { 0, 7, 10 }, { 0, 10, 11 }, { 1, 5, 9 }, { 5, 11, 4 },
    { 11, 10, 2 }, { 10, 7, 6 }, { 7, 1, 8 }, { 3, 9, 4 },  { 3, 4, 2 },   { 3, 2, 6 }, { 3, 6, 8 },
    { 3, 8, 9 },  { 4, 9, 5 },  { 2, 4, 11 }, { 6, 2, 10 }, { 8, 6, 7 },  { 9, 8, 1 },
};
static Vtx icoVtx[SPHERE_VERTS] __attribute__((aligned(16)));
static Gfx icoList[1 + SPHERE_FACES / 2 + 1] __attribute__((aligned(16)));

static void MakeIcosahedron(void) {
    Gfx *g = icoList;

    for (int i = 0; i < SPHERE_VERTS; i++) {
        icoVtx[i] = (Vtx){ icoCorners[i][0], icoCorners[i][1], icoCorners[i][2], 0, 0, 0, 0xFF, 0xFF, 0xFF, 0xFF };
    }
    *g++ = GfxVertices(icoVtx, SPHERE_VERTS);
    for (int t = 0; t < SPHERE_FACES; t += 2) {
        *g++ = GfxTriangles(icoFaces[t][0], icoFaces[t][1], icoFaces[t][2], icoFaces[t + 1][0], icoFaces[t + 1][1],
                            icoFaces[t + 1][2]);
    }
    *g++ = GfxEndDisplayList();
    osWritebackDCache(icoVtx, sizeof(icoVtx));
    osWritebackDCache(icoList, sizeof(icoList));
}

/* q turns v: v + 2w(q x v) + 2 q x (q x v), with q = (x, y, z) a unit quaternion. */
static void Rotate(const f32 *q, const f32 *v, f32 *out) {
    f32 t[3] = { 2.0f * (q[1] * v[2] - q[2] * v[1]), 2.0f * (q[2] * v[0] - q[0] * v[2]),
                 2.0f * (q[0] * v[1] - q[1] * v[0]) };

    out[0] = v[0] + q[3] * t[0] + (q[1] * t[2] - q[2] * t[1]);
    out[1] = v[1] + q[3] * t[1] + (q[2] * t[0] - q[0] * t[2]);
    out[2] = v[2] + q[3] * t[2] + (q[0] * t[1] - q[1] * t[0]);
}

/* The collision spheres of an actor, placed as CalcCollisionSpheres (collision.c) places them, each as an
 * icosahedron with its own matrix. G is the extra list. Returns the number drawn. The game turns a point with
 * GetRotationFromQuaternion, which takes acos of a value that can stray from range and give NaN; this copy turns
 * by the unit quaternion directly and leaves out a sphere whose actor has a quaternion far from unit size. */
static int AddSpheres(Frame *fr, Gfx **gp, const Actor *a, u32 rgb) {
    const ActorObject *o = a->obj;
    f32 q[4], pos[3];
    int added = 0;
    Gfx *g = *gp;

    if (!SafePointer(o) || !ReadVec(&a->pos.x, pos)) {
        return 0;
    }
    for (int i = 0; i < 4; i++) {
        if (!ReadFloat(&a->rotQuat[i], &q[i])) {
            return 0;
        }
    }
    f32 qq = q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3];
    int turns = qq > 0.8f && qq < 1.25f;
    for (int k = 0; k < o->numSpheres && k < 8 && fr->stats.spheres < MAX_SPHERES; k++) {
        const CollSphere *sp = &o->spheres[k];
        f32 c[3], sc[3], radius, mf[4][4];

        if (!ReadVec(sp->pos, sc) || !ReadFloat(&sp->radius, &radius) || !(radius > 0.0f && radius <= 1000.0f)) {
            continue;
        }
        if (sp->flags & 0x20) {
            c[0] = pos[0];
            c[1] = pos[1];
            c[2] = pos[2];
        } else if (sp->flags & 0x10) {
            c[0] = sc[0];
            c[1] = sc[1];
            c[2] = sc[2];
        } else if (turns) {
            Rotate(q, sc, c);
            c[0] += pos[0];
            c[1] += pos[1];
            c[2] += pos[2];
        } else {
            continue;
        }
        if (!(c[0] < MAX_CENTRE && c[0] > -MAX_CENTRE && c[1] < MAX_CENTRE && c[1] > -MAX_CENTRE &&
              c[2] < MAX_CENTRE && c[2] > -MAX_CENTRE)) {
            continue;
        }
        /* The corners lie a little outside the sphere. The faces are near its surface. */
        f32 size = radius * 1.12f * 0.001f;
        for (int i = 0; i < 4; i++) {
            for (int j = 0; j < 4; j++) {
                mf[i][j] = i == j ? size : 0.0f;
            }
            if (i < 3) {
                mf[3][i] = c[i];
            }
        }
        mf[3][3] = 1.0f;
        Mtx *m = &sphereMtx[fr->b][fr->stats.spheres];
        u32 colour = rgb << 8 | ((sp->flags & 4) ? 0x30 : FACE_ALPHA);
        guMtxF2L(mf, m);
        *g++ = GfxMatrix(m, MTX_LOAD);
        if (colour != fr->sphereColour) {
            *g++ = GfxSetPrimColor(colour);
            fr->sphereColour = colour;
        }
        *g++ = GfxDisplayList(icoList);
        fr->stats.spheres++;
        added++;
    }
    *gp = g;
    return added;
}

/* The spheres of every enemy in reach: an actor of an enemy type that collides with other actors (flag 8, as
 * CheckActorCollisions tests). The reach is twice the radius, as the spheres matter well before contact. */
static void AddEnemySpheres(Frame *fr, Gfx **g) {
    Actor *a = actorList.head.next;

    for (int i = 0; i < MAX_ENEMY_ACTORS && SafePointer(a) && a != &actorList.head && fr->stats.spheres < MAX_SPHERES;
         i++, a = a->next) {
        f32 d[3], p[3], radius;

        if (a->type < SNEEZY_TYPE || a->type > NME_FROG_TYPE || a->type == 6 || !(a->flags & 8)) {
            continue;
        }
        if (!ReadVec(&a->pos.x, p) || !ReadFloat(&a->radius, &radius)) {
            continue;
        }
        if (fr->radius > 0.0f) {
            for (int k = 0; k < 3; k++) {
                d[k] = p[k] - fr->c[k];
            }
            f32 reach = 2.0f * fr->radius + (radius < 0.0f ? 0.0f : radius);
            if (Dot(d, d) > reach * reach) {
                continue;
            }
        }
        fr->stats.enemies += AddSpheres(fr, g, a, ENEMY_RGBA >> 8);
    }
}

/* ---- The touched faces and the holes, made each frame around Glover -------------------------------------- */

static void PutVertex(Vtx *v, const f32 *p, const f32 *c, u32 rgba) {
    v->x = (s16)((p[0] - c[0]) * SCALE);
    v->y = (s16)((p[1] - c[1]) * SCALE);
    v->z = (s16)((p[2] - c[2]) * SCALE);
    v->flag = 0;
    v->s = v->t = 0;
    v->r = rgba >> 24;
    v->g = rgba >> 16;
    v->b = rgba >> 8;
    v->a = rgba;
}

static int NearGlover(const Frame *fr, const f32 *p) {
    for (int i = 0; i < 3; i++) {
        f32 q = (p[i] - fr->c[i]) * SCALE;
        if (q > MAX_COORD || q < -MAX_COORD) {
            return 0;
        }
    }
    return 1;
}

/* The faces that the glove and the ball touched on the last game frame, white, as the game has them. */
static int AddContacts(Frame *fr, Vtx *out) {
    const void *seen[MAX_CONTACTS];
    int n = 0;

    for (int a = 0; a < 2; a++) {
        const ActorObject *o = (a == 0 ? &hand : &ball)->obj;
        for (int k = 0; SafePointer(o) && k < 4 && n < MAX_CONTACTS; k++) {
            const CollFace *f = o->planes[k];
            f32 p[3][3];
            int dup = 0, ok = 1;

            if (!SafePointer(f) || !SafePointer((const u8 *)f + sizeof(CollFace) - 4)) {
                continue;
            }
            for (int i = 0; i < n; i++) {
                dup |= seen[i] == f;
            }
            for (int j = 0; j < 3; j++) {
                ok &= ReadVec(f->corner[j], p[j]) && NearGlover(fr, p[j]);
            }
            if (dup || !ok) {
                continue;
            }
            for (int j = 0; j < 3; j++) {
                PutVertex(&out[n * 3 + j], p[j], fr->c, 0xFFFFFF00 | CONTACT_ALPHA);
            }
            seen[n++] = f;
        }
    }
    return n;
}

/* A thin quad from P0 to P1, turned to face the camera, written to OUT. 0 if a corner is out of range or the line
 * is seen end-on (a dot). */
static int MakeBar(Vtx *out, const Frame *fr, const f32 *p0, const f32 *p1, f32 half, u32 rgba) {
    f32 d[3], v[3], side[3];

    if (!NearGlover(fr, p0) || !NearGlover(fr, p1)) {
        return 0;
    }
    for (int i = 0; i < 3; i++) {
        d[i] = p1[i] - p0[i];
        v[i] = fr->cam[i] - (p0[i] + p1[i]) * 0.5f;
    }
    Cross(d, v, side);
    /* An edge seen end-on, or a zero-length edge, is a dot. */
    if ((side[0] < 0.0f ? -side[0] : side[0]) + (side[1] < 0.0f ? -side[1] : side[1]) +
            (side[2] < 0.0f ? -side[2] : side[2]) <
        1.0f) {
        return 0;
    }
    f32 k = half / Sqrt(Dot(side, side));
    f32 c[4][3];
    for (int i = 0; i < 3; i++) {
        side[i] *= k;
        c[0][i] = p0[i] + side[i];
        c[1][i] = p0[i] - side[i];
        c[2][i] = p1[i] + side[i];
        c[3][i] = p1[i] - side[i];
    }
    for (int i = 0; i < HOLE_VERTS; i++) {
        PutVertex(out + i, c[i], fr->c, rgba);
    }
    return 1;
}

/* The marked holes (moonaim.h): a violet post on each. The game thread publishes the list; the copy is kept only if
 * no publish began meanwhile. Returns the number of posts written. */
static int AddHoles(const Frame *fr, Vtx *out) {
    static HoleView local;
    int stable = 0, n = 0;

    for (int tries = 0; tries < 4 && !stable; tries++) {
        u32 seq = holeSeq;

        local = holeViews[seq & 1];
        __asm__ volatile("" ::: "memory");
        stable = holeBegin - seq <= 1;
    }
    if (!stable) {
        return 0;
    }
    for (int i = 0; i < local.count && i < HOLE_MAX; i++) {
        f32 p0[3], p1[3];

        if (!ReadVec(local.p[i], p0)) {
            continue;
        }
        p1[0] = p0[0];
        p1[1] = p0[1] + HOLE_HEIGHT;
        p1[2] = p0[2];
        if (MakeBar(out + n * HOLE_VERTS, fr, p0, p1, HOLE_HALF_WIDTH, HOLE_RGBA)) {
            n++;
        }
    }
    return n;
}

/* ---- The frame ------------------------------------------------------------------------------------------- */

/* Starts a new cache when the level's memory changed. The old one is written over only once no frame that
 * draws it is still in flight. */
static void CacheGameFrame(void) {
    u32 frames = practice.drawFrames;

    if (resetRequest || gameInfo.level != cacheLevel || frameCount < cacheFrame) {
        resetRequest = 0;
        cacheLevel = gameInfo.level;
        cacheState = COLL_CACHE_WAIT;
    }
    cacheFrame = frameCount;
    if (cacheState == COLL_CACHE_WAIT && frames - lastDrawn >= CACHE_WAIT_FRAMES) {
        CacheClear();
        cacheState = COLL_CACHE_BUILD;
        buildCounts = 0;
        builds++;
    }
}

void CollViewDraw(void) {
    static u8 icoMade;
    int b = draw_buffer & 1;
    Frame fr;
    u16 perspNorm;
    f32 mf[4][4];

    fr.start = osGetCount();
    for (u32 i = 0; i < sizeof(fr.stats); i++) {
        ((u8 *)&fr.stats)[i] = 0;
    }
    /* Only on frames of a level in which the game drew the world, so its camera matrix is this frame's. */
    if (gameInfo.mode != GAME_MODE_LEVEL || gameInfo.level >= LEVEL_PRESENT || !gfxIsDrawing ||
        onlyDrawBackdrops || (gameInfo.fade >= 276 && gameInfo.fade2 >= 276) || !ReadVec(&hand.pos.x, fr.c) ||
        !ReadVec(&actualCamPos[b].x, fr.cam) || fieldOfView <= 0 || nearPlaneDist <= 0 ||
        farPlaneDist <= nearPlaneDist || !(fr.c[0] < MAX_CENTRE && fr.c[0] > -MAX_CENTRE) ||
        !(fr.c[1] < MAX_CENTRE && fr.c[1] > -MAX_CENTRE) || !(fr.c[2] < MAX_CENTRE && fr.c[2] > -MAX_CENTRE)) {
        fr.stats.state = cacheState;
        collStats = fr.stats;
        return;
    }
    if (!icoMade) {
        MakeIcosahedron();
        icoMade = 1;
    }
    CacheGameFrame();
    FreeCamEye(fr.cam);
    fr.b = b;
    fr.radius = radii[run.collRadius % COLL_RADIUSES];
    fr.g = scene[b];
    fr.end = scene[b] + SCENE_SIZE;
    fr.moving = fr.pending = 0;
    fr.buildCounts = 0;
    fr.sphereColour = 0;

    /* Every actor with collision faces: the platforms, then the landscape. Each frame finds the actors again in
     * the game's lists. */
    if (cacheState != COLL_CACHE_WAIT) {
        /* The build that runs goes on first: a mesh is built over as many frames as it needs. */
        RunJob(fr.start, BUILD_COUNTS);
        fr.buildCounts = osGetCount() - fr.start;
        Platform *p = platformList.head.next;
        for (int i = 0; i < MAX_PLATFORMS && SafePointer(p) && p != &platformList.head; i++, p = p->next) {
            if (!(p->flags & 0x84) && SafePointer(p->actor.faces)) {
                VisitActor(&fr, &p->actor, 1);
            }
        }
        Actor *a = collisionList.head.collNext;
        for (int i = 0; i < MAX_COLLISION_ACTORS && SafePointer(a) && a != &collisionList.head;
             i++, a = a->collNext) {
            VisitActor(&fr, a, 0);
        }
        if (cacheState == COLL_CACHE_BUILD) {
            buildCounts += fr.buildCounts;
            if (fr.pending == 0 && job.step == JOB_NONE) {
                cacheState = COLL_CACHE_READY;
            }
        } else if (run.collEdges) {
            /* The open edges come after the faces: the faces of every actor fit first. */
            BuildEdgesNear(&fr);
        }
        edgesWereOn = run.collEdges;
    }
    int drawScene = fr.g != scene[b];
    if (drawScene) {
        *fr.g++ = GfxEndDisplayList();
        osWritebackDCache(scene[b], (fr.g - scene[b]) * sizeof(Gfx));
        osWritebackDCache(movingMtx[b], fr.moving * sizeof(Mtx));
        lastDrawn = practice.drawFrames;
    }

    /* The projection: the game's for this frame, then its camera. */
    guPerspective(&projection[b], &perspNorm, fieldOfView, 4.0f / 3.0f, nearPlaneDist, farPlaneDist, 1.0f);
    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 4; j++) {
            mf[i][j] = 0.0f;
        }
    }
    /* The matrix of the vertices made each frame: half units around Glover. */
    mf[0][0] = mf[1][1] = mf[2][2] = 1.0f / SCALE;
    mf[3][0] = fr.c[0];
    mf[3][1] = fr.c[1];
    mf[3][2] = fr.c[2];
    mf[3][3] = 1.0f;
    guMtxF2L(mf, &gloverMtx[b]);
    int onSurfaces = run.collView == COLLVIEW_SURFACES;
    viewport[b].scale[0] = viewport[b].trans[0] = VP_X;
    viewport[b].scale[1] = viewport[b].trans[1] = VP_Y;
    viewport[b].scale[2] = VP_Z;
    viewport[b].trans[2] = VP_Z - (onSurfaces ? Z_BIAS : 0);
    viewport[b].scale[3] = viewport[b].trans[3] = 0;

    /* The overlay runs in the frame's last task, which starts from the microcode's first state: everything is
     * set again. The render mode is the game's translucent surface; on surfaces, also with Z compare and no Z
     * update. */
    Gfx *g = extra[b];
    *g++ = GfxSync(CMD_PIPESYNC);
    *g++ = GfxCmd(CMD_SETSCISSOR << 24, (320 * 4) << 12 | (240 * 4));
    if (onSurfaces) {
        *g++ = GfxCmd(CMD_SETZIMG << 24, PhysAddr(zbuffer));
    }
    *g++ = GfxViewport(&viewport[b]);
    *g++ = GfxMatrix(&projection[b], MTX_PROJECTION | MTX_LOAD);
    *g++ = GfxMatrix(dynamicp + DYNAMIC_CAMERA_VIEW, MTX_PROJECTION);
    *g++ = GfxPerspNormalize(perspNorm);
    *g++ = GfxCmd(CMD_TEXTURE << 24, 0);
    *g++ = GfxCmd(CMD_CLEARGEOMETRYMODE << 24, GEOM_ALL);
    *g++ = GfxCmd(CMD_SETGEOMETRYMODE << 24, GEOM_SHADE | GEOM_SHADING_SMOOTH | (onSurfaces ? GEOM_ZBUFFER : 0));
    *g++ = GfxSetOtherMode(CMD_SETOTHERMODE_H, 4, 20, 0xCF0);
    *g++ = GfxSetOtherMode(CMD_SETOTHERMODE_L, 0, 32, onSurfaces ? RENDER_XLU_Z : RENDER_XLU);
    *g++ = FACE_COMBINE;
    *g++ = GfxSetEnvColor(0xFFFFFFFF);
    if (drawScene) {
        /* The faces whose solid side faces the camera, then (faint) the others. The solid side is the one that
         * the normal points to, and the game's normal is the cross product of the edges from corner 0: the RSP
         * sees it as the front. */
        if (run.collBack != COLL_BACK_SAME) {
            *g++ = GfxCmd(CMD_SETGEOMETRYMODE << 24, GEOM_CULL_BACK);
        }
        *g++ = GfxDisplayList(scene[b]);
        if (run.collBack == COLL_BACK_FAINT) {
            *g++ = GfxCmd(CMD_CLEARGEOMETRYMODE << 24, GEOM_CULL_BACK);
            *g++ = GfxCmd(CMD_SETGEOMETRYMODE << 24, GEOM_CULL_FRONT);
            *g++ = GfxSync(CMD_PIPESYNC);
            *g++ = GfxSetEnvColor(0xFFFFFF00 | BACK_ENV);
            *g++ = GfxDisplayList(scene[b]);
            *g++ = GfxSync(CMD_PIPESYNC);
            *g++ = GfxSetEnvColor(0xFFFFFFFF);
        }
        *g++ = GfxCmd(CMD_CLEARGEOMETRYMODE << 24, GEOM_CULL_BACK | GEOM_CULL_FRONT);
    }
    /* The spheres. */
    if (run.collSpheres) {
        AddSpheres(&fr, &g, &hand, 0xFFFFFF);
        AddSpheres(&fr, &g, &ball, 0xFFE040);
    }
    if (run.collEnemies) {
        AddEnemySpheres(&fr, &g);
    }
    /* The touched faces and the holes: vertices around Glover, with their own colours. */
    int contacts = AddContacts(&fr, contactVtx[b]);
    int holes = run.collHoles ? AddHoles(&fr, holeVtx[b]) : 0;
    if (contacts || holes) {
        *g++ = GfxSync(CMD_PIPESYNC);
        *g++ = SHADE_COMBINE;
        *g++ = GfxMatrix(&gloverMtx[b], MTX_LOAD);
    }
    if (contacts) {
        *g++ = GfxVertices(contactVtx[b], contacts * 3);
        for (int k = 0; k < contacts; k++) {
            *g++ = GfxTriangle(k * 3, k * 3 + 1, k * 3 + 2);
        }
    }
    for (int e = 0; e < holes; e += HOLE_BATCH) {
        int n = holes - e < HOLE_BATCH ? holes - e : HOLE_BATCH;
        *g++ = GfxVertices(holeVtx[b] + e * HOLE_VERTS, n * HOLE_VERTS);
        for (int k = 0; k < n; k++) {
            *g++ = GfxTriangles(k * 4, k * 4 + 1, k * 4 + 2, k * 4 + 2, k * 4 + 1, k * 4 + 3);
        }
    }
    *g++ = GfxEndDisplayList();

    fr.stats.contacts = contacts;
    fr.stats.holes = holes;
    fr.stats.state = cacheState;
    fr.stats.full |= cacheFull | edgesFull << 1;
    fr.stats.shapes = shapeCount;
    fr.stats.records = recordCount;
    fr.stats.vertices = vtxUsed;
    fr.stats.commands = gfxUsed;
    fr.stats.builds = builds;
    fr.stats.buildCounts = buildCounts;

    osWritebackDCache(extra[b], (g - extra[b]) * sizeof(Gfx));
    osWritebackDCache(sphereMtx[b], fr.stats.spheres * sizeof(Mtx));
    osWritebackDCache(contactVtx[b], contacts * 3 * sizeof(Vtx));
    osWritebackDCache(holeVtx[b], holes * HOLE_VERTS * sizeof(Vtx));
    osWritebackDCache(&gloverMtx[b], sizeof(Mtx));
    osWritebackDCache(&projection[b], sizeof(Mtx));
    osWritebackDCache(&viewport[b], sizeof(Vp));
    *glistp++ = GfxDisplayList(extra[b]);
    fr.stats.counts = osGetCount() - fr.start;
    collStats = fr.stats;
}

