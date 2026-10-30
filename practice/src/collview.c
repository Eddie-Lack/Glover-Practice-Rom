/* The collision view: the game's collision faces near Glover, drawn over the frame as see-through triangles
 * coloured by kind, and the collision spheres of the glove and the ball. Three more parts have their own switches:
 * the spheres of the enemies, the faces of platforms worked out again from their mesh, and the open edges (edges
 * that belong to one face only). The draw thread builds it at the end of the frame's display list
 * (PracticePrintTimers). It only reads the game's memory.
 *
 * The draw thread runs above the game thread and can stop it in the middle of a game frame, and some lists hold
 * freed or half-written data. So every pointer is checked before it is followed, every walk is bounded, and every
 * float is checked as bits before the FPU sees it: the game's threads trap on NaN and denormal operands. The
 * maths that is a copy of the game's (turning a point by a quaternion, placing a platform's mesh) reads each game
 * float with ReadFloat, which also flushes numbers too small to multiply safely. */
#include "actions.h"
#include "collview.h"
#include "freecam.h"
#include "game.h"
#include "moonaim.h"
#include "rdp.h"
#include "safe.h"

#define MAX_FACES 320
#define MAX_SPHERES 40 /* the glove's and the ball's first, then the enemies' */
#define SPHERE_VERTS 12
#define SPHERE_FACES 20
#define BATCH 10 /* faces a vertex load: 30 vertices, as the game loads them */
/* An open edge is a thin quad of 4 vertices and 2 triangles, loaded EDGE_BATCH edges at a time. */
#define MAX_EDGES 128
#define EDGE_VERTS 4
#define EDGE_BATCH 7
#define EDGE_BASE (MAX_FACES * 3 + MAX_SPHERES * SPHERE_VERTS) /* the edges' vertices follow the faces' and spheres' */
#define HOLE_BASE (EDGE_BASE + MAX_EDGES * EDGE_VERTS) /* then one post (a quad) for each marked hole */
#define VTX_COUNT (HOLE_BASE + HOLE_MAX * EDGE_VERTS)
#define HEADER_COMMANDS 16
#define DL_SIZE                                                                                                    \
    (HEADER_COMMANDS + (MAX_FACES / BATCH) * (1 + BATCH) + MAX_SPHERES * (1 + SPHERE_FACES / 2) +                  \
     ((MAX_EDGES + EDGE_BATCH - 1) / EDGE_BATCH) * (1 + EDGE_BATCH) +                                              \
     ((HOLE_MAX + EDGE_BATCH - 1) / EDGE_BATCH) * (1 + EDGE_BATCH) + 1)

#define MAX_COLLISION_ACTORS 1024
#define MAX_PLATFORMS 256
#define MAX_MESH_FACES 8192
/* The scan stops after 3 ms of the CPU counter (46.875 MHz). */
#define SCAN_COUNTS 140625
/* Vertices are in half units around Glover, as s16. */
#define SCALE 2.0f
#define MAX_COORD 32000.0f
/* In "on surfaces" mode the faces are pulled this far towards the camera in the viewport's Z units. The depth is
 * not linear: 2 units is about 13 world units at 300 units from the camera. */
#define Z_BIAS 2
/* Glover's place goes into a 16.16 fixed-point matrix (guMtxF2L), which traps at 32768 or more. */
#define MAX_CENTRE 30000.0f
/* The scan's radius shrinks to this when the faces fill the buffer. */
#define MIN_RADIUS 100.0f
#define MAX_CONTACTS 8
#define MAX_ENEMY_ACTORS 400
/* The enemy types of the game's ActorType: SNEEZY (5) to NME_FROG (58), without the unused 6. */
#define SNEEZY_TYPE 5
#define NME_FROG_TYPE 58
/* An open edge is drawn this many units wide on each side of its line. */
#define EDGE_HALF_WIDTH 1.5f
#define EDGE_RGBA 0xFF20A0FF /* hot pink, opaque */
/* A marked hole is a post of this height and half width, violet and opaque, standing on the hole's centre. */
#define HOLE_HEIGHT 24.0f
#define HOLE_HALF_WIDTH 1.5f
#define HOLE_RGBA 0xB050FFFF
#define ENEMY_RGBA 0xFF603000
/* The open edges of a mesh are worked out once and kept. A mesh with more faces than this is left out. */
#define EDGE_MESH_FACES 700
#define EDGE_CACHE 96
#define EDGE_POOL 16384 /* faces in all kept meshes */
#define EDGE_SLOTS 4096
#define EDGE_BUILDS_PER_FRAME 2
/* A platform's faces are not kept up to date by the game with these flags (RecalculatePlatformInfo). */
#define PLATFORM_NO_UPDATE 0x20084
/* A coordinate of a world-space corner that is worked out here stays below 2^20. */
#define MAX_WORLD 1000000.0f

/* The viewport of the game's InitViewport: 320 by 240, the whole Z range. */
#define VP_X 640
#define VP_Y 480
#define VP_Z 511

CollStats collStats;

static Vtx vertices[2][VTX_COUNT] __attribute__((aligned(16)));
static Gfx lists[2][DL_SIZE] __attribute__((aligned(16)));
static Mtx modelView[2] __attribute__((aligned(16)));
static Mtx projection[2] __attribute__((aligned(16)));
static Vp viewport[2] __attribute__((aligned(16)));

static const s16 radii[] = { 500, 250, 1000 };
/* The radius in use: the menu's, or less while the faces near Glover fill the buffer. The first faces found win,
 * so a smaller radius keeps the nearest. */
static f32 radius;
static u8 lastRadiusChoice = 0xFF;

/* The scan's work: the centre (Glover), the radius, the camera, and the output. */
typedef struct Scan {
    f32 c[3];
    f32 cam[3];
    f32 r;
    Vtx *v;
    int faces;
    int spheres;
    const void *contacts[MAX_CONTACTS]; /* CollFace pointers, drawn white; the scan skips them */
    int contactCount;
    Vtx *ev;           /* the next open edge's vertices */
    int edges;
    int edgesFull;
    int edgesSkipped;
    int fresh;         /* platforms worked out again */
    int enemies;       /* enemy spheres */
    int full;
    u32 start;
    int steps; /* faces looked at, for the time check */
    int stop;
} Scan;

static f32 Dot(const f32 *a, const f32 *b) {
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

static f32 Sqrt(f32 x) {
    return __builtin_sqrtf(x);
}

/* Faces and actors left out on this frame: too far from Glover for the vertex format, or unreadable. */
static u16 scanRange;
static u16 scanBad;

static int TimeUp(Scan *s) {
    if (s->stop) {
        return 1;
    }
    if (++s->steps % 64 == 0 && osGetCount() - s->start > SCAN_COUNTS) {
        s->stop = 1;
    }
    return s->stop;
}

/* RGBA for a face: its terrain first, then its slope. */
static u32 FaceColour(u8 t, f32 ny) {
    if (t == TERRAIN_WATER) {
        return 0x30C0F000; /* cyan */
    }
    if (t < TERRAIN_COUNT && SafeFloat(&terrain[t].slip) && SafeFloat(&terrain[t].gravityScale)) {
        if (terrain[t].slip > 1.0f) {
            return 0xE0F0FF00; /* slippery: pale blue */
        }
        if (terrain[t].gravityScale < 1.0f) {
            return 0xFF9020 << 8; /* low gravity: orange */
        }
        if (terrain[t].slip < 1.0f) {
            return 0xD040D000; /* sticky: magenta */
        }
    }
    if (ny >= 0.75f) {
        return 0x40E04000; /* floor: green */
    }
    if (ny >= 0.3f) {
        return 0xC0E03000; /* slope: yellow-green */
    }
    if (ny > -0.3f) {
        return 0x4070FF00; /* wall: blue */
    }
    return 0xFF404000; /* ceiling: red */
}

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

static int InRam(const void *p, u32 bytes, u32 align) {
    u32 a = (u32)p;

    return (a & (align - 1)) == 0 && a >= 0x80000000 && a < 0x80800000 && bytes <= 0x800000 &&
           a + bytes <= 0x80800000;
}

/* The sum of the sizes of the three parts of a vector: 0 only for a zero vector. A test before a square, which
 * could underflow for a tiny vector. */
static f32 AbsSum(const f32 *v) {
    return (v[0] < 0.0f ? -v[0] : v[0]) + (v[1] < 0.0f ? -v[1] : v[1]) + (v[2] < 0.0f ? -v[2] : v[2]);
}

static void Cross(const f32 *a, const f32 *b, f32 *n) {
    n[0] = a[1] * b[2] - a[2] * b[1];
    n[1] = a[2] * b[0] - a[0] * b[2];
    n[2] = a[0] * b[1] - a[1] * b[0];
}

/* ---- Open edges ------------------------------------------------------------------------------------------ */

/* The open edges of each mesh, kept: the mesh's shape never changes, also when a platform moves. Bit j of a
 * face's byte is set when the edge from corner j to corner j + 1 belongs to no other face. Faces share an edge
 * when they share its two vertex numbers (the meshes of Atlantis 1 have no vertex twice). */
typedef struct EdgeEntry {
    const Mesh *mesh; /* NULL for an empty entry */
    u32 check; /* tells a mesh from another one that took its place */
    u16 offset;
    u16 size;
} EdgeEntry;

/* A new mesh takes the next entry in turn and the next bytes of the pool, which wraps round. It drops the
 * meshes whose entry or bytes it takes. */
static EdgeEntry edgeCache[EDGE_CACHE];
static int edgeNext;      /* the entry the next mesh takes */
static int edgePoolWrite; /* where the next mesh's bytes start */
static int edgeBuilds;    /* meshes built on this frame */
static u8 edgePool[EDGE_POOL];
static u32 edgeKeys[EDGE_SLOTS];
static u8 edgeSeen[EDGE_SLOTS];
static u8 edgeLevel;
static s32 edgeFrame;
static u8 edgesWereOn;

static void EdgeReset(void) {
    for (int i = 0; i < EDGE_CACHE; i++) {
        edgeCache[i].mesh = NULL;
    }
    edgeNext = 0;
    edgePoolWrite = 0;
}

/* The game thread asks; the draw thread, which uses the cache, drops it at the start of its next scan. */
static volatile u8 edgeResetRequest;

void CollViewEdgesReset(void) {
    edgeResetRequest = 1;
}

/* Faces 0 and 1, and about 30 more spread over the mesh, with the face count and the vertex count. */
static u32 MeshCheck(const Mesh *m, int nf) {
    u32 c = (u32)m->faces ^ ((u32)nf << 16) ^ (u16)m->numVerts;
    int step = nf / 30 > 1 ? nf / 30 : 1;

    for (int i = 0; i < nf; i += i < 2 ? 1 : step) {
        for (int j = 0; j < 3; j++) {
            c = c * 31 + (u16)m->faces[i][j];
        }
    }
    return c;
}

/* Counts every edge of the mesh, then marks the ones seen once. The table has at least 4 slots for each face.
 * 0 if a vertex number is out of range. */
static int BuildOpenBits(const Mesh *m, int nf, int nv, u8 *out) {
    const s16(*f)[3] = m->faces;
    int bits = 6;

    while ((1 << bits) < nf * 4) {
        bits++;
    }
    u32 mask = (1u << bits) - 1;
    for (u32 i = 0; i <= mask; i++) {
        edgeSeen[i] = 0;
    }
    for (int i = 0; i < nf; i++) {
        for (int j = 0; j < 3; j++) {
            int a = f[i][j], b = f[i][(j + 1) % 3];
            if (a < 0 || a >= nv || b < 0 || b >= nv) {
                return 0;
            }
            if (a == b) {
                continue;
            }
            u32 key = a < b ? (u32)a << 16 | (u32)b : (u32)b << 16 | (u32)a;
            u32 slot = (key * 2654435761u) >> (32 - bits);
            while (edgeSeen[slot] && edgeKeys[slot] != key) {
                slot = (slot + 1) & mask;
            }
            edgeKeys[slot] = key;
            if (edgeSeen[slot] < 255) {
                edgeSeen[slot]++;
            }
        }
    }
    for (int i = 0; i < nf; i++) {
        u8 open = 0;
        for (int j = 0; j < 3; j++) {
            int a = f[i][j], b = f[i][(j + 1) % 3];
            if (a == b) {
                continue;
            }
            u32 key = a < b ? (u32)a << 16 | (u32)b : (u32)b << 16 | (u32)a;
            u32 slot = (key * 2654435761u) >> (32 - bits);
            while (edgeKeys[slot] != key) {
                slot = (slot + 1) & mask;
            }
            if (edgeSeen[slot] == 1) {
                open |= 1 << j;
            }
        }
        out[i] = open;
    }
    return 1;
}

/* The open-edge bits of a mesh, one byte a face, or NULL: too many faces, unreadable, or not built yet (a few
 * meshes are built on each frame, and none once the scan has used half of its time). */
static const u8 *OpenBits(Scan *s, const Mesh *m) {
    int nf = m->numFaces, nv = m->numVerts;
    u32 check;
    EdgeEntry *e;

    if (nf < 1 || nf > EDGE_MESH_FACES || nv < 3 || !InRam(m->faces, nf * sizeof(m->faces[0]), 2)) {
        s->edgesSkipped++;
        return NULL;
    }
    check = MeshCheck(m, nf);
    for (int i = 0; i < EDGE_CACHE; i++) {
        if (edgeCache[i].mesh == m) {
            if (edgeCache[i].check == check) {
                return edgePool + edgeCache[i].offset;
            }
            edgeCache[i].mesh = NULL; /* another mesh took the place of the kept one */
            break;
        }
    }
    if (edgeBuilds >= EDGE_BUILDS_PER_FRAME || osGetCount() - s->start > SCAN_COUNTS / 2) {
        return NULL;
    }
    edgeBuilds++;
    if (edgePoolWrite + nf > EDGE_POOL) {
        edgePoolWrite = 0;
    }
    if (!BuildOpenBits(m, nf, nv, edgePool + edgePoolWrite)) {
        s->edgesSkipped++;
        return NULL;
    }
    /* The build counts against the scan's time. */
    if (osGetCount() - s->start > SCAN_COUNTS) {
        s->stop = 1;
    }
    for (int i = 0; i < EDGE_CACHE; i++) {
        if (edgeCache[i].mesh != NULL && edgeCache[i].offset < edgePoolWrite + nf &&
            edgePoolWrite < edgeCache[i].offset + edgeCache[i].size) {
            edgeCache[i].mesh = NULL; /* its bytes were written over */
        }
    }
    e = &edgeCache[edgeNext];
    edgeNext = (edgeNext + 1) % EDGE_CACHE;
    e->mesh = m;
    e->check = check;
    e->offset = edgePoolWrite;
    e->size = nf;
    edgePoolWrite += nf;
    return edgePool + e->offset;
}

/* A thin quad from P0 to P1, turned to face the camera, written to OUT. 0 if a corner is out of range or the line is seen
 * end-on (a dot). */
static int MakeBar(Vtx *out, const Scan *s, const f32 *p0, const f32 *p1, f32 half, u32 rgba) {
    f32 d[3], v[3], side[3], q[3];

    for (int i = 0; i < 3; i++) {
        d[i] = p1[i] - p0[i];
        v[i] = s->cam[i] - (p0[i] + p1[i]) * 0.5f;
        q[i] = (p0[i] - s->c[i]) * SCALE;
        if (q[i] > MAX_COORD || q[i] < -MAX_COORD) {
            return 0;
        }
        q[i] = (p1[i] - s->c[i]) * SCALE;
        if (q[i] > MAX_COORD || q[i] < -MAX_COORD) {
            return 0;
        }
    }
    Cross(d, v, side);
    /* An edge seen end-on, or a zero-length edge, is a dot. */
    if (AbsSum(side) < 1.0f) {
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
    for (int i = 0; i < EDGE_VERTS; i++) {
        PutVertex(out + i, c[i], s->c, rgba);
    }
    return 1;
}

/* A thin quad along an edge, turned to face the camera. */
static void AddEdge(Scan *s, const f32 *p0, const f32 *p1) {
    if (s->edges >= MAX_EDGES) {
        s->edgesFull = 1;
        return;
    }
    if (MakeBar(s->ev, s, p0, p1, EDGE_HALF_WIDTH, EDGE_RGBA)) {
        s->ev += EDGE_VERTS;
        s->edges++;
    }
}

/* The marked holes (moonaim.h): a violet post on each. The game thread publishes the list; the copy is kept only if no
 * publish began meanwhile. Returns the number of posts written. */
static int AddHoles(Scan *s, Vtx *out) {
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

        if (!(ReadFloat(&local.p[i][0], &p0[0]) && ReadFloat(&local.p[i][1], &p0[1]) &&
              ReadFloat(&local.p[i][2], &p0[2]))) {
            continue;
        }
        p1[0] = p0[0];
        p1[1] = p0[1] + HOLE_HEIGHT;
        p1[2] = p0[2];
        if (MakeBar(out + n * EDGE_VERTS, s, p0, p1, HOLE_HALF_WIDTH, HOLE_RGBA)) {
            n++;
        }
    }
    return n;
}

static void AddOpenEdges(Scan *s, const f32 (*corner)[3], u8 bits) {
    for (int j = 0; j < 3 && bits; j++) {
        if (bits & (1 << j)) {
            AddEdge(s, corner[j], corner[(j + 1) % 3]);
        }
    }
}

/* ---- Faces ----------------------------------------------------------------------------------------------- */

/* Adds a triangle if it is near Glover and can be drawn. CONTACT faces are the ones Glover or the ball touched.
 * ID names the game's face. A face drawn as a contact is not drawn again. OPEN holds the triangle's open
 * edges. Returns 1 if the triangle was added, 2 if it was drawn already as a contact, else 0. */
static int AddTri(Scan *s, const f32 *normal, const f32 (*corner)[3], u8 terrainRow, int contact, const void *id,
                  u8 open) {
    const f32 *v0 = corner[0], *v1 = corner[1], *v2 = corner[2];
    f32 d[3], m[3], e1[3], e2[3], n[3];
    f32 r2 = 0.0f;
    u32 rgba;

    if (s->faces >= MAX_FACES) {
        s->full = 1;
        return 0;
    }
    for (int i = 0; i < s->contactCount && !contact; i++) {
        if (s->contacts[i] == id) {
            AddOpenEdges(s, corner, open);
            return 2;
        }
    }
    /* The plane is near: the distance from Glover to it along its normal. */
    for (int i = 0; i < 3; i++) {
        d[i] = s->c[i] - v0[i];
    }
    if (!contact) {
        f32 dist = Dot(normal, d);
        if (dist > s->r || dist < -s->r) {
            return 0;
        }
        /* The triangle is near: its centre, less its size. */
        for (int i = 0; i < 3; i++) {
            m[i] = (v0[i] + v1[i] + v2[i]) * (1.0f / 3.0f);
        }
        for (int k = 0; k < 3; k++) {
            const f32 *p = corner[k];
            f32 q = (p[0] - m[0]) * (p[0] - m[0]) + (p[1] - m[1]) * (p[1] - m[1]) + (p[2] - m[2]) * (p[2] - m[2]);
            r2 = q > r2 ? q : r2;
        }
        for (int i = 0; i < 3; i++) {
            m[i] -= s->c[i];
        }
        f32 reach = s->r + Sqrt(r2);
        if (Dot(m, m) > reach * reach) {
            return 0;
        }
    }
    /* Not a degenerate triangle. */
    for (int i = 0; i < 3; i++) {
        e1[i] = v1[i] - v0[i];
        e2[i] = v2[i] - v0[i];
    }
    Cross(e1, e2, n);
    if (Dot(n, n) <= 0.001f) {
        return 0;
    }
    /* Every corner fits an s16 around Glover. */
    for (int k = 0; k < 3; k++) {
        for (int i = 0; i < 3; i++) {
            f32 q = (corner[k][i] - s->c[i]) * SCALE;
            if (q > MAX_COORD || q < -MAX_COORD) {
                scanRange++;
                return 0;
            }
        }
    }
    if (contact && s->contactCount < MAX_CONTACTS) {
        s->contacts[s->contactCount++] = id;
    }
    rgba = contact ? 0xFFFFFF00 : FaceColour(terrainRow, normal[1]);
    /* The side that the normal points to is the solid side; seen from behind, a face is fainter. */
    for (int i = 0; i < 3; i++) {
        d[i] = s->cam[i] - v0[i];
    }
    rgba |= contact ? 0xA0 : Dot(normal, d) > 0.0f ? 0x60 : 0x28;
    for (int k = 0; k < 3; k++) {
        PutVertex(s->v++, corner[k], s->c, rgba);
    }
    s->faces++;
    AddOpenEdges(s, corner, open);
    return 1;
}

static void AddFace(Scan *s, const CollFace *f, int contact, u8 open) {
    if (!SafeFloats(f->normal, 3) || !SafeFloats(&f->corner[0][0], 9)) {
        return;
    }
    AddTri(s, f->normal, (const f32(*)[3])f->corner, f->terrain, contact, f, open);
}

/* A platform's mesh placed as RecalculatePlatformInfo places it: each corner scaled, turned by the platform's
 * rotation and moved to its position. The game does this only when something comes near the platform. */
typedef struct PlaceMesh {
    const Vec3 *verts;
    const s16 (*faces)[3];
    int numVerts;
    f32 scale[3];
    f32 pos[3];
    f32 m[3][3]; /* the rotation matrix of QuaternionToMatrix (maths.c) */
} PlaceMesh;

static int BeginPlace(PlaceMesh *pm, const Actor *a, const Mesh *mesh) {
    f32 q[4], s, xs, ys, zs, wx, wy, wz, xx, xy, xz, yy, yz, zz;

    pm->verts = mesh->verts;
    pm->faces = mesh->faces;
    pm->numVerts = mesh->numVerts;
    if (pm->numVerts < 1 || !InRam(pm->verts, pm->numVerts * sizeof(Vec3), 4) ||
        !InRam(pm->faces, mesh->numFaces * sizeof(pm->faces[0]), 2)) {
        return 0;
    }
    for (int i = 0; i < 3; i++) {
        if (!ReadFloat(&(&a->scale.x)[i], &pm->scale[i]) || !ReadFloat(&(&a->pos.x)[i], &pm->pos[i])) {
            return 0;
        }
    }
    for (int i = 0; i < 4; i++) {
        if (!ReadFloat(&a->rotQuat[i], &q[i])) {
            return 0;
        }
    }
    /* The game divides by the squared size of the quaternion. A size far from 1 is not a rotation. */
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
    pm->m[0][0] = 1.0f - (yy + zz);
    pm->m[0][1] = xy + wz;
    pm->m[0][2] = xz - wy;
    pm->m[1][0] = xy - wz;
    pm->m[1][1] = 1.0f - (xx + zz);
    pm->m[1][2] = yz + wx;
    pm->m[2][0] = xz + wy;
    pm->m[2][1] = yz - wx;
    pm->m[2][2] = 1.0f - (xx + yy);
    return 1;
}

/* Face i of the mesh in world space, with its normal. 0 if a vertex is unreadable or the face is degenerate. */
static int PlaceFace(const PlaceMesh *pm, int i, f32 *normal, f32 (*corner)[3]) {
    f32 e1[3], e2[3], n[3], len;

    for (int j = 0; j < 3; j++) {
        int idx = pm->faces[i][j];
        f32 v[3], x, y, z;

        if (idx < 0 || idx >= pm->numVerts || !ReadFloat(&pm->verts[idx].x, &v[0]) ||
            !ReadFloat(&pm->verts[idx].y, &v[1]) || !ReadFloat(&pm->verts[idx].z, &v[2])) {
            return 0;
        }
        x = pm->scale[0] * v[0];
        y = pm->scale[1] * v[1];
        z = pm->scale[2] * v[2];
        corner[j][0] = pm->m[0][0] * x + pm->m[1][0] * y + pm->m[2][0] * z;
        corner[j][1] = pm->m[0][1] * x + pm->m[1][1] * y + pm->m[2][1] * z;
        corner[j][2] = pm->m[0][2] * x + pm->m[1][2] * y + pm->m[2][2] * z;
    }
    for (int i2 = 0; i2 < 3; i2++) {
        e1[i2] = corner[1][i2] - corner[0][i2];
        e2[i2] = corner[2][i2] - corner[0][i2];
    }
    Cross(e1, e2, n);
    if (AbsSum(n) < 0.01f) {
        return 0;
    }
    len = Sqrt(Dot(n, n));
    for (int k = 0; k < 3; k++) {
        normal[k] = n[k] / len;
        for (int j = 0; j < 3; j++) {
            corner[j][k] += pm->pos[k];
            if (!(corner[j][k] < MAX_WORLD && corner[j][k] > -MAX_WORLD)) {
                return 0;
            }
        }
    }
    return 1;
}

/* The faces of an actor's collision mesh, if its bounding sphere reaches the scan. PLAT is the platform that
 * the actor belongs to, or NULL for a landscape actor. */
static void AddActorFaces(Scan *s, const Actor *a, const Platform *plat) {
    const CollFace *faces = a->faces;
    const ObjectInstance *inst = a->inst;
    const Mesh *mesh;
    const u8 *bits = NULL;
    PlaceMesh pm;
    int place = 0;
    int n;
    f32 d[3];

    if (!SafePointer(faces) || !SafePointer(inst) || !SafePointer(inst->obj) || !SafePointer(inst->obj->mesh) || !SafeFloats(&a->pos.x, 3) ||
        !SafeFloat(&a->radius) || a->radius < 0.0f) {
        scanBad++;
        return;
    }
    mesh = inst->obj->mesh;
    n = mesh->numFaces;
    if (n < 1 || n > MAX_MESH_FACES) {
        return;
    }
    d[0] = a->pos.x - s->c[0];
    d[1] = a->pos.y - s->c[1];
    d[2] = a->pos.z - s->c[2];
    f32 reach = s->r + a->radius;
    if (Dot(d, d) > reach * reach) {
        return;
    }
    if (run.collEdges) {
        bits = OpenBits(s, mesh);
    }
    /* A platform whose faces the game did not bring up to date on this game frame is placed from its mesh. */
    if (run.collFresh && plat != NULL && !(plat->flags & PLATFORM_NO_UPDATE) &&
        plat->lastUpdate != (u16)frameCount) {
        place = BeginPlace(&pm, a, mesh);
        if (place) {
            s->fresh++;
        } else {
            scanBad++;
        }
    }
    for (int i = 0; i < n && !TimeUp(s); i++) {
        if (!SafePointer(&faces[i]) || !SafePointer((const u8 *)&faces[i] + sizeof(CollFace) - 4)) {
            scanBad++;
            return;
        }
        if (place) {
            f32 normal[3], corner[3][3];
            if (PlaceFace(&pm, i, normal, corner)) {
                AddTri(s, normal, (const f32(*)[3])corner, faces[i].terrain, 0, &faces[i], bits ? bits[i] : 0);
            }
        } else {
            AddFace(s, &faces[i], 0, bits ? bits[i] : 0);
        }
    }
}

/* q turns v: v + 2w(q x v) + 2 q x (q x v), with q = (x, y, z) a unit quaternion. */
static void Rotate(const f32 *q, const f32 *v, f32 *out) {
    f32 t[3] = { 2.0f * (q[1] * v[2] - q[2] * v[1]), 2.0f * (q[2] * v[0] - q[0] * v[2]),
                 2.0f * (q[0] * v[1] - q[1] * v[0]) };

    out[0] = v[0] + q[3] * t[0] + (q[1] * t[2] - q[2] * t[1]);
    out[1] = v[1] + q[3] * t[1] + (q[2] * t[0] - q[0] * t[2]);
    out[2] = v[2] + q[3] * t[2] + (q[0] * t[1] - q[1] * t[0]);
}

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

/* The collision spheres of an actor, placed as CalcCollisionSpheres (collision.c) places them. Returns the number
 * drawn. The game turns a point with GetRotationFromQuaternion, which takes acos of a value that can stray from
 * range and give NaN; this copy turns by the unit quaternion directly and leaves out a sphere whose actor has a
 * quaternion far from unit size. */
static int AddSpheres(Scan *s, const Actor *a, u32 rgba) {
    const ActorObject *o = a->obj;
    f32 q[4], pos[3];
    int added = 0;

    if (!SafePointer(o)) {
        return 0;
    }
    for (int i = 0; i < 3; i++) {
        if (!ReadFloat(&(&a->pos.x)[i], &pos[i])) {
            return 0;
        }
    }
    for (int i = 0; i < 4; i++) {
        if (!ReadFloat(&a->rotQuat[i], &q[i])) {
            return 0;
        }
    }
    f32 qq = q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3];
    int turns = qq > 0.8f && qq < 1.25f;
    for (int k = 0; k < o->numSpheres && k < 8 && s->spheres < MAX_SPHERES; k++) {
        const CollSphere *sp = &o->spheres[k];
        f32 c[3], sc[3], radius;

        if (!ReadFloat(&sp->pos[0], &sc[0]) || !ReadFloat(&sp->pos[1], &sc[1]) || !ReadFloat(&sp->pos[2], &sc[2]) ||
            !ReadFloat(&sp->radius, &radius) || !(radius > 0.0f && radius <= 1000.0f)) {
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
        /* The corners lie a little outside the sphere. The faces are near its surface. */
        f32 size = radius * 1.12f * 0.001f;
        Vtx *v = s->v;
        int ok = 1;
        for (int i = 0; i < SPHERE_VERTS && ok; i++) {
            f32 p[3];
            for (int j = 0; j < 3; j++) {
                p[j] = c[j] + icoCorners[i][j] * size;
                f32 d = (p[j] - s->c[j]) * SCALE;
                ok &= d <= MAX_COORD && d >= -MAX_COORD;
            }
            if (ok) {
                PutVertex(&v[i], p, s->c, rgba | ((sp->flags & 4) ? 0x30 : 0x60));
            }
        }
        if (ok) {
            s->v += SPHERE_VERTS;
            s->spheres++;
            added++;
        }
    }
    return added;
}

/* The spheres of every enemy near Glover: an actor of an enemy type that collides with other actors (flag 8, as
 * CheckActorCollisions tests). The reach is twice the scan's radius, as the spheres matter well before contact. */
static void AddEnemySpheres(Scan *s) {
    Actor *a = actorList.head.next;

    for (int i = 0; i < MAX_ENEMY_ACTORS && SafePointer(a) && a != &actorList.head && s->spheres < MAX_SPHERES;
         i++, a = a->next) {
        f32 d[3], radius;

        if (a->type < SNEEZY_TYPE || a->type > NME_FROG_TYPE || a->type == 6 || !(a->flags & 8)) {
            continue;
        }
        if (!SafeFloats(&a->pos.x, 3) || !SafeFloat(&a->radius)) {
            continue;
        }
        radius = a->radius < 0.0f ? 0.0f : a->radius;
        d[0] = a->pos.x - s->c[0];
        d[1] = a->pos.y - s->c[1];
        d[2] = a->pos.z - s->c[2];
        f32 reach = 2.0f * s->r + radius;
        if (Dot(d, d) <= reach * reach) {
            s->enemies += AddSpheres(s, a, ENEMY_RGBA);
        }
    }
}

void CollViewDraw(void) {
    int b = draw_buffer & 1;
    Scan s;
    Gfx *g = lists[b];
    u16 perspNorm;
    f32 mf[4][4];

    CollStats stats = { 0 };

    /* Only on frames of a level in which the game drew the world, so its camera matrix is this frame's. */
    if (gameInfo.mode != GAME_MODE_LEVEL || gameInfo.level >= LEVEL_PRESENT || !gfxIsDrawing ||
        onlyDrawBackdrops || (gameInfo.fade >= 276 && gameInfo.fade2 >= 276) || !SafeFloats(&hand.pos.x, 3) ||
        !SafeFloats(&actualCamPos[b].x, 3) || fieldOfView <= 0 || nearPlaneDist <= 0 ||
        farPlaneDist <= nearPlaneDist || !(hand.pos.x < MAX_CENTRE && hand.pos.x > -MAX_CENTRE) ||
        !(hand.pos.y < MAX_CENTRE && hand.pos.y > -MAX_CENTRE) ||
        !(hand.pos.z < MAX_CENTRE && hand.pos.z > -MAX_CENTRE)) {
        collStats = stats;
        return;
    }
    if (run.collRadius != lastRadiusChoice) {
        lastRadiusChoice = run.collRadius;
        radius = radii[run.collRadius % ARRAY_COUNT(radii)];
    }
    s.c[0] = hand.pos.x;
    s.c[1] = hand.pos.y;
    s.c[2] = hand.pos.z;
    s.cam[0] = actualCamPos[b].x;
    s.cam[1] = actualCamPos[b].y;
    s.cam[2] = actualCamPos[b].z;
    FreeCamEye(s.cam);
    s.r = radius;
    s.v = vertices[b];
    s.faces = s.spheres = 0;
    s.ev = vertices[b] + EDGE_BASE;
    s.edges = s.edgesFull = s.edgesSkipped = s.fresh = s.enemies = 0;
    s.contactCount = 0;
    s.full = 0;
    s.start = osGetCount();
    scanRange = scanBad = 0;
    edgeBuilds = 0;
    if (run.collEdges) {
        /* The kept meshes are dropped when the switch was off, the level changed, the frame count went back, or a
         * savestate load asked. */
        if (!edgesWereOn || gameInfo.level != edgeLevel || frameCount < edgeFrame || edgeResetRequest) {
            edgeResetRequest = 0;
            EdgeReset();
        }
        edgeLevel = gameInfo.level;
        edgeFrame = frameCount;
    }
    edgesWereOn = run.collEdges;
    s.steps = 0;
    s.stop = 0;

    /* The faces that the glove and the ball touched on the last game frame. */
    for (int a = 0; a < 2; a++) {
        const ActorObject *o = (a == 0 ? &hand : &ball)->obj;
        for (int k = 0; SafePointer(o) && k < 4; k++) {
            if (SafePointer(o->planes[k]) && SafePointer((const u8 *)o->planes[k] + sizeof(CollFace) - 4)) {
                AddFace(&s, o->planes[k], 1, 0);
            }
        }
    }
    /* The platforms. A platform's faces are brought up to date only when something is near it. */
    Platform *p = platformList.head.next;
    for (int i = 0; i < MAX_PLATFORMS && SafePointer(p) && p != &platformList.head && !TimeUp(&s); i++, p = p->next) {
        if (!(p->flags & 0x84)) {
            AddActorFaces(&s, &p->actor, p);
        }
    }
    /* The landscape. */
    Actor *a = collisionList.head.collNext;
    for (int i = 0; i < MAX_COLLISION_ACTORS && SafePointer(a) && a != &collisionList.head && !TimeUp(&s);
         i++, a = a->collNext) {
        AddActorFaces(&s, a, NULL);
    }
    int faces = s.faces;
    int holes = run.collHoles ? AddHoles(&s, vertices[b] + HOLE_BASE) : 0;
    if (run.collSpheres) {
        AddSpheres(&s, &hand, 0xFFFFFF00);
        AddSpheres(&s, &ball, 0xFFE04000);
    }
    if (run.collEnemies) {
        AddEnemySpheres(&s);
    }
    /* A full buffer or a scan cut short: a smaller radius next frame. Half full or less: back towards the menu's. */
    f32 chosen = radii[run.collRadius % ARRAY_COUNT(radii)];
    if (s.full || s.stop) {
        radius = radius * 0.75f > MIN_RADIUS ? radius * 0.75f : MIN_RADIUS;
    } else if (faces < MAX_FACES / 2) {
        radius = radius * 1.25f + 1.0f < chosen ? radius * 1.25f + 1.0f : chosen;
    }
    stats.faces = faces;
    stats.spheres = s.spheres;
    stats.range = scanRange;
    stats.bad = scanBad;
    stats.slow = s.stop;
    stats.full = s.full;
    stats.radius = (s16)s.r;
    stats.enemies = s.enemies;
    stats.fresh = s.fresh;
    stats.edges = s.edges;
    stats.holes = holes;
    stats.edgesFull = s.edgesFull;
    stats.edgesSkipped = s.edgesSkipped > 255 ? 255 : s.edgesSkipped;
    stats.counts = osGetCount() - s.start;
    collStats = stats;
    if (faces == 0 && s.spheres == 0 && s.edges == 0 && holes == 0) {
        return;
    }

    /* The matrices: the game's projection for this frame, then its camera; the model moves the half-unit
     * vertices back to Glover's place. */
    guPerspective(&projection[b], &perspNorm, fieldOfView, 4.0f / 3.0f, nearPlaneDist, farPlaneDist, 1.0f);
    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 4; j++) {
            mf[i][j] = 0.0f;
        }
    }
    mf[0][0] = mf[1][1] = mf[2][2] = 1.0f / SCALE;
    mf[3][0] = s.c[0];
    mf[3][1] = s.c[1];
    mf[3][2] = s.c[2];
    mf[3][3] = 1.0f;
    guMtxF2L(mf, &modelView[b]);
    int onSurfaces = run.collView == COLLVIEW_SURFACES;
    viewport[b].scale[0] = viewport[b].trans[0] = VP_X;
    viewport[b].scale[1] = viewport[b].trans[1] = VP_Y;
    viewport[b].scale[2] = VP_Z;
    viewport[b].trans[2] = VP_Z - (onSurfaces ? Z_BIAS : 0);
    viewport[b].scale[3] = viewport[b].trans[3] = 0;

    /* The overlay runs in the frame's last task, which starts from the microcode's first state: everything is
     * set again. The render mode is the game's translucent surface; on surfaces, also with Z compare and no Z
     * update. The combiner gives the vertex colour and alpha. */
    *g++ = GfxSync(CMD_PIPESYNC);
    *g++ = GfxCmd(CMD_SETSCISSOR << 24, (320 * 4) << 12 | (240 * 4));
    if (onSurfaces) {
        *g++ = GfxCmd(CMD_SETZIMG << 24, PhysAddr(zbuffer));
    }
    *g++ = GfxViewport(&viewport[b]);
    *g++ = GfxMatrix(&projection[b], MTX_PROJECTION | MTX_LOAD);
    *g++ = GfxMatrix(dynamicp + DYNAMIC_CAMERA_VIEW, MTX_PROJECTION);
    *g++ = GfxPerspNormalize(perspNorm);
    *g++ = GfxMatrix(&modelView[b], MTX_LOAD);
    *g++ = GfxCmd(CMD_TEXTURE << 24, 0);
    *g++ = GfxCmd(CMD_CLEARGEOMETRYMODE << 24, GEOM_ALL);
    *g++ = GfxCmd(CMD_SETGEOMETRYMODE << 24, GEOM_SHADE | GEOM_SHADING_SMOOTH | (onSurfaces ? GEOM_ZBUFFER : 0));
    *g++ = GfxSync(CMD_PIPESYNC);
    *g++ = GfxSetOtherMode(CMD_SETOTHERMODE_H, 4, 20, 0xCF0);
    *g++ = GfxSetOtherMode(CMD_SETOTHERMODE_L, 0, 32, onSurfaces ? 0x00504A50 : 0x00504240);
    *g++ = GfxCmd(0xFCFFFFFF, 0xFFFE793C);

    const Vtx *v = vertices[b];
    for (int f = 0; f < faces; f += BATCH) {
        int n = faces - f < BATCH ? faces - f : BATCH;
        *g++ = GfxVertices(v + f * 3, n * 3);
        for (int k = 0; k < n; k++) {
            *g++ = GfxTriangle(k * 3, k * 3 + 1, k * 3 + 2);
        }
    }
    v += faces * 3;
    for (int k = 0; k < s.spheres; k++, v += SPHERE_VERTS) {
        *g++ = GfxVertices(v, SPHERE_VERTS);
        for (int t = 0; t < SPHERE_FACES; t += 2) {
            *g++ = GfxTriangles(icoFaces[t][0], icoFaces[t][1], icoFaces[t][2], icoFaces[t + 1][0],
                                icoFaces[t + 1][1], icoFaces[t + 1][2]);
        }
    }
    /* The open edges come last. They lie over the faces. */
    v = vertices[b] + EDGE_BASE;
    for (int e = 0; e < s.edges; e += EDGE_BATCH) {
        int n = s.edges - e < EDGE_BATCH ? s.edges - e : EDGE_BATCH;
        *g++ = GfxVertices(v + e * EDGE_VERTS, n * EDGE_VERTS);
        for (int k = 0; k < n; k++) {
            *g++ = GfxTriangles(k * 4, k * 4 + 1, k * 4 + 2, k * 4 + 2, k * 4 + 1, k * 4 + 3);
        }
    }
    v = vertices[b] + HOLE_BASE;
    for (int e = 0; e < holes; e += EDGE_BATCH) {
        int n = holes - e < EDGE_BATCH ? holes - e : EDGE_BATCH;
        *g++ = GfxVertices(v + e * EDGE_VERTS, n * EDGE_VERTS);
        for (int k = 0; k < n; k++) {
            *g++ = GfxTriangles(k * 4, k * 4 + 1, k * 4 + 2, k * 4 + 2, k * 4 + 1, k * 4 + 3);
        }
    }
    *g++ = GfxEndDisplayList();

    osWritebackDCache(lists[b], (g - lists[b]) * sizeof(Gfx));
    osWritebackDCache(vertices[b], (faces * 3 + s.spheres * SPHERE_VERTS) * sizeof(Vtx));
    osWritebackDCache(vertices[b] + EDGE_BASE, s.edges * EDGE_VERTS * sizeof(Vtx));
    osWritebackDCache(vertices[b] + HOLE_BASE, holes * EDGE_VERTS * sizeof(Vtx));
    osWritebackDCache(&modelView[b], sizeof(Mtx));
    osWritebackDCache(&projection[b], sizeof(Mtx));
    osWritebackDCache(&viewport[b], sizeof(Vp));
    *glistp++ = GfxDisplayList(lists[b]);
}
