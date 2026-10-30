/* The Moon Pound aim (moonaim.h, trainers.h). The slam falls through the collision mesh at a few mesh vertices, where
 * the game's sphere test finds no contact (docs/game.md, "Moon Pound"). While Glover jumps, this code plays the slam
 * forward for Z on each of the next 8 game frames, with the game's own contact test ported as a pure function, and
 * looks for the nearest vertex whose dead zone the fall column could reach. A port of tools/tricks/moonpound_trainer.py
 * and cone.py. It reads the game's faces and writes nothing to the game.
 *
 * The work is bounded. One budget (STEP_COUNTS of the CPU counter, about 4 ms) covers a whole game frame: copying the
 * faces, finding the hole candidates, the 8 slams and the search. Each part checks the counter between its small
 * steps and stops when the budget is gone, and the view says so (MOWF_SLOW). The faces within REACH of Glover are
 * copied into a buffer (MAX_FACES) at the start of each jump, when the glove has moved BUILD_MOVE sideways or
 * BUILD_DY up or down, every REFRESH game frames, and after a level start or a savestate load. The hole candidates
 * are found over as many game frames as that takes. A slam is played over the few faces that its column can touch.
 * A verdict is only given for a slam that lies inside what the buffer is known to hold; any other cell is grey. */
/* This file is compute-bound; the build default (-Os) is slower for it. */
#pragma GCC optimize("O2")
#include "actions.h"
#include "fmt.h"
#include "game.h"
#include "moonaim.h"
#include "practice.h"
#include "safe.h"
#include "savestate.h"
#include "text.h"

#define MAX_FACES 192
#define MAX_CANDS 64
#define PASSES 14          /* slam passes played: the hover and ten falls */
#define FALL 14.0f         /* the slam's fall a pass (hand.c, MaintainAction) */
#define MIN_NY 0.25f       /* the lowest normal.y of a floor */
#define TOL 0.001f         /* the inside test's tolerance (collision.c) */
#define REACH_MAX 120.0f
#define REACH_MIN 60.0f
#define BUILD_MOVE 24.0f
#define BUILD_DY 24.0f     /* the buffer holds the heights from 330 below to 80 above the glove of the build */
#define BUILD_BELOW 330.0f
#define BUILD_ABOVE 80.0f
#define REFRESH 100
#define GAP_MIN 4          /* game frames from one finished build to the start of the next, except at the start of a jump */
#define CAND_BUILD 72.0f   /* candidates are found this far from the buffer's centre */
#define CAND_REACH 40.0f   /* and used this far from the fall column, or nearer when the column is far from the centre */
#define ZONE_STEPS 16
#define ZONE_RANGE 1.5f    /* the search starts when the column is this near the hole centre */
#define PHASE_WINDOW_TOP 2.0f /* the 20-step cap leaves a hole only when the centre was this near the vertex (14 * 0.9^19) */
#define STEP_COUNTS 187500 /* 4 ms of the CPU counter (46.875 MHz); it also counts the time of other threads */
#define HOLE_COUNTS 70312  /* 1.5 ms: the budget of the hole painter's build */
#define STRIP_COUNTS 140625 /* the part of it for the slams and the search (3 ms); the build of the buffer gets the rest */
#define POLY_MAX 40
#define STICK_PER_UNIT (36.0f / 2.85f) /* raw stick units that move the column 1 unit (docs/game.md, "Moon Pound") */
#define STICK_DEAD 5.0f

typedef struct MFace {
    f32 n[3], d;
    f32 v[3][3];
    f32 edir[3][3];
    f32 elen[3];
    f32 proj[4];
    f32 lo[3], hi[3];
    u8 axis[2];
} MFace;

typedef struct Cand {
    f32 v[3];
    f32 cx, cz; /* the hole's centre for a centre one unit above the vertex, relative to the vertex */
} Cand;

typedef struct Buf {
    MFace faces[MAX_FACES];
    f32 key[MAX_FACES];        /* the squared distance from the centre to the face's box */
    f32 bb[MAX_FACES][8];      /* lo x, y, z, hi x, y, z, normal.y: what the slams test first, in a small array */
    Cand cands[MAX_CANDS];
    int faceCount, candCount, worst; /* worst: the farthest face of a full buffer */
    f32 x, y, z;               /* the centre: the glove when the build began */
    f32 cover;                 /* every face within this distance of the centre is in the buffer; < 0: none is sure */
    u8 full;                   /* more faces than fit: the nearest are kept */
    u8 cap;                    /* the candidates stopped at MAX_CANDS */
} Buf;

/* A buffer is built in the staging one over several game frames, in the work of the job below. When it is complete it
 * replaces the active one, which serves the slams until then. */
static Buf bufs[2];
static Buf *act = &bufs[0];
static Buf *stg = &bufs[1];

enum { JOB_IDLE, JOB_COLLECT, JOB_CANDS };
static struct {
    u8 stage;
    u8 platforms;        /* the platform list is being walked, not the landscape's */
    int actorIdx;        /* the position in the list of the actor that is being copied */
    const Actor *actor;  /* and the actor that stood there: a list that changed restarts the job */
    int faceIdx;         /* the next face of that actor */
    int candNext;        /* the next face for the candidates, and its next corner */
    int candCorner;
    u32 candMax;         /* the longest MakeCandidate so far */
    f32 reach;
} job;

static const MoonIn *in;
static f32 reach = REACH_MAX;
static u8 jumpActive;         /* the last step was in a jump */
static u8 cacheOk;            /* the slams of the last step can be reused (MoonAimStep) */
static u32 bufSerial;         /* counts the swaps of the active buffer */
static u32 buildAge;          /* game frames since the last commit */
static u16 rebuilds;
static u32 stepStart;
static u8 slow;               /* this step used its budget */
static u8 cut;                /* and left work undone: cells grey, or the search short */
static u32 budget = STEP_COUNTS; /* the build's share of the step: the aim's, or the painter's */

static f32 Sqrt(f32 x) {
    return __builtin_sqrtf(x);
}

static f32 Abs(f32 x) {
    return x < 0.0f ? -x : x;
}

static f32 Dot(const f32 *a, const f32 *b) {
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

/* True once the slams and the search have used their part of the budget, or when another piece of work as long as EST
 * would not fit. */
static int OutOfTime(u32 est) {
    if (!slow && osGetCount() - stepStart + est > STRIP_COUNTS) {
        slow = 1;
    }
    return slow;
}

/* The build of the buffer goes on with what the slams leave of the step's budget. */
static int JobTimeUp(void) {
    return osGetCount() - stepStart > budget;
}

void MoonAimReset(void) {
    job.stage = JOB_IDLE;
    jumpActive = 0;
    cacheOk = 0;
    bufSerial++;
    act->faceCount = act->candCount = 0;
    act->cover = -1.0f;
    act->full = act->cap = 0;
    buildAge = 0;
    reach = REACH_MAX;
}

/* ---------------------------------------------------------------------------------------------------------
 * The faces
 */

/* Copies a game face if its corners lie inside the box. 0 if not, or if a number is not usable, or if the contact
 * test can never find a contact on it (a face with no area). The corners are read first: most faces are far away. */
static int LoadFace(const CollFace *c, MFace *o, f32 xlo, f32 xhi, f32 zlo, f32 zhi, f32 ylo, f32 yhi) {
    const f32 *src = (const f32 *)c;
    f32 raw[29];

    for (int i = 4; i < 13; i++) {
        if (!ReadFloat(&src[i], &raw[i])) {
            return 0;
        }
    }
    for (int i = 0; i < 3; i++) {
        o->lo[i] = o->hi[i] = raw[4 + i];
        for (int k = 1; k < 3; k++) {
            if (raw[4 + 3 * k + i] < o->lo[i]) {
                o->lo[i] = raw[4 + 3 * k + i];
            }
            if (raw[4 + 3 * k + i] > o->hi[i]) {
                o->hi[i] = raw[4 + 3 * k + i];
            }
        }
    }
    if (o->hi[0] < xlo || o->lo[0] > xhi || o->hi[2] < zlo || o->lo[2] > zhi || o->hi[1] < ylo || o->lo[1] > yhi) {
        return 0;
    }
    for (int i = 0; i < 4; i++) {
        if (!ReadFloat(&src[i], &raw[i])) {
            return 0;
        }
    }
    for (int i = 13; i < 29; i++) {
        if (!ReadFloat(&src[i], &raw[i])) {
            return 0;
        }
    }
    if (c->axis[0] > 2 || c->axis[1] > 2) {
        return 0;
    }
    if ((raw[27] == 0.0f && raw[28] == 0.0f) || (raw[25] == 0.0f && raw[26] == 0.0f)) {
        return 0;
    }
    for (int i = 0; i < 3; i++) {
        o->n[i] = raw[i];
    }
    o->d = raw[3];
    for (int k = 0; k < 3; k++) {
        for (int i = 0; i < 3; i++) {
            o->v[k][i] = raw[4 + 3 * k + i];
            o->edir[k][i] = raw[13 + 3 * k + i];
        }
        o->elen[k] = raw[22 + k];
    }
    for (int i = 0; i < 4; i++) {
        o->proj[i] = raw[25 + i];
    }
    o->axis[0] = c->axis[0];
    o->axis[1] = c->axis[1];
    return 1;
}

/* The squared distance from (cx, cz) to the box of a face. */
static f32 BoxKey(const MFace *f, f32 cx, f32 cz) {
    f32 dx = f->lo[0] > cx ? f->lo[0] - cx : cx > f->hi[0] ? cx - f->hi[0] : 0.0f;
    f32 dz = f->lo[2] > cz ? f->lo[2] - cz : cz > f->hi[2] ? cz - f->hi[2] : 0.0f;

    return dx * dx + dz * dz;
}

static void FindWorst(Buf *b) {
    b->worst = 0;
    for (int j = 1; j < b->faceCount; j++) {
        if (b->key[j] > b->key[b->worst]) {
            b->worst = j;
        }
    }
}

static void SetBox(Buf *b, int i) {
    const MFace *f = &b->faces[i];

    for (int k = 0; k < 3; k++) {
        b->bb[i][k] = f->lo[k];
        b->bb[i][3 + k] = f->hi[k];
    }
    b->bb[i][6] = f->n[1];
}

/* Starts a new build centred on the glove. */
static void JobStart(void) {
    stg->faceCount = stg->candCount = 0;
    stg->full = stg->cap = 0;
    stg->cover = -1.0f;
    stg->x = in->pos[0];
    stg->y = in->pos[1];
    stg->z = in->pos[2];
    job.stage = JOB_COLLECT;
    job.platforms = 0;
    job.actorIdx = 0;
    job.actor = NULL;
    job.faceIdx = 0;
    job.candNext = job.candCorner = 0;
    job.candMax = 0;
    job.reach = reach;
}

/* The actor at a position of the landscape's list or the platform list: 1, 0 at the end of the list, 2 for a platform
 * that the slam does not touch. */
static int NthActor(int platforms, int idx, const Actor **out) {
    if (!platforms) {
        const Actor *a = collisionList.head.collNext;

        for (int i = 0; i < idx; i++) {
            if (!SafePointer(a) || a == &collisionList.head) {
                return 0;
            }
            a = a->collNext;
        }
        if (idx >= 1024 || !SafePointer(a) || a == &collisionList.head) {
            return 0;
        }
        *out = a;
        return 1;
    } else {
        const Platform *p = platformList.head.next;

        for (int i = 0; i < idx; i++) {
            if (!SafePointer(p) || p == &platformList.head) {
                return 0;
            }
            p = p->next;
        }
        if (idx >= 256 || !SafePointer(p) || p == &platformList.head) {
            return 0;
        }
        *out = &p->actor;
        return (p->flags & 0x84) ? 2 : 1;
    }
}

/* Copies the faces of one actor that lie within the job's reach, from the face that the job is at. A full buffer keeps
 * the nearest faces. 1 when the actor is done, 0 when the time is up. */
static int CopyActor(const Actor *a) {
    const CollFace *list = a->faces;
    const ObjectInstance *inst = a->inst;
    const Mesh *mesh;
    f32 cx = stg->x, cz = stg->z, ylo = stg->y - BUILD_BELOW, yhi = stg->y + BUILD_ABOVE;
    f32 dx, dz, rr;
    int n;

    if (!SafePointer(list) || !SafePointer(inst) || !SafePointer(inst->obj) || !SafePointer(inst->obj->mesh) ||
        !SafeFloat(&a->pos.x) || !SafeFloat(&a->pos.z) || !SafeFloat(&a->radius) || a->radius < 0.0f) {
        return 1;
    }
    mesh = inst->obj->mesh;
    n = mesh->numFaces;
    if (n < 1 || n > 8192) {
        return 1;
    }
    dx = a->pos.x - cx;
    dz = a->pos.z - cz;
    rr = job.reach + a->radius;
    if (dx * dx + dz * dz > rr * rr) {
        return 1;
    }
    for (; job.faceIdx < n; job.faceIdx++) {
        static MFace tmp;
        f32 key;

        if (JobTimeUp()) {
            return 0;
        }
        if (!SafePointer(&list[job.faceIdx]) || !SafePointer((const u8 *)&list[job.faceIdx] + sizeof(CollFace) - 4)) {
            return 1;
        }
        if (!LoadFace(&list[job.faceIdx], &tmp, cx - job.reach, cx + job.reach, cz - job.reach, cz + job.reach, ylo,
                      yhi)) {
            continue;
        }
        key = BoxKey(&tmp, cx, cz);
        if (stg->faceCount < MAX_FACES) {
            stg->key[stg->faceCount] = key;
            stg->faces[stg->faceCount] = tmp;
            SetBox(stg, stg->faceCount++);
            if (stg->faceCount == MAX_FACES) {
                FindWorst(stg);
            }
        } else {
            stg->full = 1;
            if (key < stg->key[stg->worst]) {
                stg->faces[stg->worst] = tmp;
                SetBox(stg, stg->worst);
                stg->key[stg->worst] = key;
                FindWorst(stg);
            }
        }
    }
    return 1;
}

/* Goes on with the copy of the faces. Sets the stage to JOB_CANDS when every actor is done. */
static void CollectRun(void) {
    while (job.stage == JOB_COLLECT) {
        const Actor *a = NULL;
        int what = NthActor(job.platforms, job.actorIdx, &a);

        if (JobTimeUp()) {
            return;
        }
        if (what == 0) {
            if (!job.platforms) {
                job.platforms = 1;
                job.actorIdx = 0;
                job.faceIdx = 0;
                job.actor = NULL;
                continue;
            }
            /* What the buffer is sure to hold: all of a disc around the centre. */
            stg->cover = job.reach;
            if (stg->full) {
                stg->cover = Sqrt(stg->key[stg->worst]);
                if (stg->cover > job.reach) {
                    stg->cover = job.reach;
                }
            }
            job.stage = JOB_CANDS;
            return;
        }
        if (what == 1) {
            if (job.faceIdx > 0 && job.actor != a) {
                JobStart(); /* the list changed under the job */
                return;
            }
            job.actor = a;
            if (!CopyActor(a)) {
                return;
            }
        }
        job.actorIdx++;
        job.faceIdx = 0;
        job.actor = NULL;
    }
}

/* ---------------------------------------------------------------------------------------------------------
 * The hole candidates (tools/tricks/cone.py)
 */

typedef struct Poly {
    int n;
    f32 p[POLY_MAX][2];
} Poly;

/* Keeps the part of a with ax * x + az * z < c (Sutherland and Hodgman). 0 if the result does not fit. */
static int Clip(const Poly *a, Poly *out, f32 ax, f32 az, f32 c) {
    out->n = 0;
    for (int i = 0; i < a->n; i++) {
        const f32 *p = a->p[i];
        const f32 *q = a->p[(i + 1) % a->n];
        f32 fp = ax * p[0] + az * p[1] - c;
        f32 fq = ax * q[0] + az * q[1] - c;

        if (fp < 0.0f) {
            if (out->n >= POLY_MAX) {
                return 0;
            }
            out->p[out->n][0] = p[0];
            out->p[out->n][1] = p[1];
            out->n++;
        }
        if ((fp < 0.0f) != (fq < 0.0f)) {
            f32 t = fp / (fp - fq);

            if (out->n >= POLY_MAX) {
                return 0;
            }
            out->p[out->n][0] = p[0] + t * (q[0] - p[0]);
            out->p[out->n][1] = p[1] + t * (q[1] - p[1]);
            out->n++;
        }
    }
    return 1;
}

/* The area and the centroid of a polygon. */
static f32 PolyStats(const Poly *a, f32 *cx, f32 *cz) {
    f32 area = 0.0f, sx = 0.0f, sz = 0.0f;

    if (a->n < 3) {
        return 0.0f;
    }
    for (int i = 0; i < a->n; i++) {
        const f32 *p = a->p[i];
        const f32 *q = a->p[(i + 1) % a->n];
        f32 cr = p[0] * q[1] - q[0] * p[1];

        area += cr;
        sx += (p[0] + q[0]) * cr;
        sz += (p[1] + q[1]) * cr;
    }
    area *= 0.5f;
    if (Abs(area) < 1e-9f) {
        return 0.0f;
    }
    *cx = sx / (6.0f * area);
    *cz = sz / (6.0f * area);
    return Abs(area);
}

/* The directions in which a centre above the corner K of face F finds an inside contact on F (the small height limit
 * of the contact test). */
static int FaceCatchesDir(const MFace *f, int k, const f32 *w) {
    f32 a[3], b[3], c1[3], c2[3];
    int j = (k + 2) % 3;

    if (Dot(f->n, w) <= 0.0f) {
        return 0;
    }
    for (int i = 0; i < 3; i++) {
        a[i] = f->edir[k][i];
        b[i] = -f->edir[j][i];
    }
    c1[0] = a[1] * w[2] - a[2] * w[1];
    c1[1] = a[2] * w[0] - a[0] * w[2];
    c1[2] = a[0] * w[1] - a[1] * w[0];
    c2[0] = w[1] * b[2] - w[2] * b[1];
    c2[1] = w[2] * b[0] - w[0] * b[2];
    c2[2] = w[0] * b[1] - w[1] * b[0];
    return Dot(c1, f->n) >= 0.0f && Dot(c2, f->n) >= 0.0f;
}

static int SameVertex(const f32 *a, const f32 *b) {
    return Abs(a[0] - b[0]) < 0.05f && Abs(a[1] - b[1]) < 0.05f && Abs(a[2] - b[2]) < 0.05f;
}

typedef struct Incident {
    u8 face;
    u8 corner;
} Incident;

/* The vertex at corner k of face i: 1 and a candidate when the dead cone of its real edges reaches over a floor face
 * that the sphere test misses. */
static int MakeCandidate(int i, int k, Cand *out) {
    const f32 *v = stg->faces[i].v[k];
    Incident inc[24];
    int nInc = 0;
    Poly a, b;
    Poly *cur = &a, *next = &b, *tmp;
    f32 areaSum = 0.0f, sx = 0.0f, sz = 0.0f;
    int anyFeasible = 0;

    for (int j = 0; j < stg->faceCount; j++) {
        const MFace *f = &stg->faces[j];

        if (v[0] < f->lo[0] - 0.05f || v[0] > f->hi[0] + 0.05f || v[2] < f->lo[2] - 0.05f ||
            v[2] > f->hi[2] + 0.05f || v[1] < f->lo[1] - 0.05f || v[1] > f->hi[1] + 0.05f) {
            continue;
        }
        for (int kk = 0; kk < 3; kk++) {
            if (!SameVertex(f->v[kk], v)) {
                continue;
            }
            if (j < i && f->n[1] >= MIN_NY) {
                return 0; /* an earlier up-facing face has this corner: that corner makes the candidate */
            }
            if (nInc < 24) {
                inc[nInc].face = (u8)j;
                inc[nInc].corner = (u8)kk;
                nInc++;
            }
        }
    }
    /* The dead cone: for every real edge e at the vertex, (P - V) . e < 0, with P - V = d (wx, 1, wz). */
    cur->n = 4;
    cur->p[0][0] = cur->p[0][1] = -8.0f;
    cur->p[1][0] = 8.0f;
    cur->p[1][1] = -8.0f;
    cur->p[2][0] = cur->p[2][1] = 8.0f;
    cur->p[3][0] = -8.0f;
    cur->p[3][1] = 8.0f;
    for (int q = 0; q < nInc; q++) {
        const MFace *f = &stg->faces[inc[q].face];
        int kk = inc[q].corner;
        int j = (kk + 2) % 3;

        if (f->elen[kk] != -1.0f && f->elen[kk] != 0.0f) {
            if (!Clip(cur, next, f->edir[kk][0], f->edir[kk][2], -f->edir[kk][1])) {
                return 0;
            }
            tmp = cur;
            cur = next;
            next = tmp;
            if (cur->n == 0) {
                return 0;
            }
        }
        if (f->elen[j] != -1.0f && f->elen[j] != 0.0f) {
            if (!Clip(cur, next, -f->edir[j][0], -f->edir[j][2], f->edir[j][1])) {
                return 0;
            }
            tmp = cur;
            cur = next;
            next = tmp;
            if (cur->n == 0) {
                return 0;
            }
        }
    }
    /* The part of the cone that lies over a floor face at the vertex. */
    for (int q = 0; q < nInc; q++) {
        const MFace *f = &stg->faces[inc[q].face];
        int kk = inc[q].corner;
        int j = (kk + 2) % 3;
        f32 ah[2] = { f->edir[kk][0], f->edir[kk][2] };
        f32 bh[2] = { -f->edir[j][0], -f->edir[j][2] };
        f32 cross, pcx, pcz, area;
        Poly part, part2;

        if (f->n[1] < MIN_NY) {
            continue;
        }
        cross = ah[0] * bh[1] - ah[1] * bh[0];
        if (Abs(cross) < 1e-9f) {
            continue;
        }
        if (cross < 0.0f) {
            f32 t0 = ah[0], t1 = ah[1];

            ah[0] = bh[0];
            ah[1] = bh[1];
            bh[0] = t0;
            bh[1] = t1;
        }
        if (!Clip(cur, &part, ah[1], -ah[0], 0.0f) || part.n == 0) {
            continue;
        }
        if (!Clip(&part, &part2, -bh[1], bh[0], 0.0f) || part2.n < 3) {
            continue;
        }
        area = PolyStats(&part2, &pcx, &pcz);
        if (area <= 1e-9f) {
            continue;
        }
        areaSum += area;
        sx += area * pcx;
        sz += area * pcz;
        if (!anyFeasible) {
            /* Is there a direction in this part that no face at the vertex catches? */
            for (int s = 0; s <= part2.n && !anyFeasible; s++) {
                f32 wx = pcx, wz = pcz, w[3];
                int caught = 0;

                if (s > 0) {
                    wx = pcx + 0.9f * (part2.p[s - 1][0] - pcx);
                    wz = pcz + 0.9f * (part2.p[s - 1][1] - pcz);
                }
                if (Sqrt(wx * wx + wz * wz) > 3.0f) {
                    continue;
                }
                w[0] = wx;
                w[1] = 1.0f;
                w[2] = wz;
                for (int r = 0; r < nInc && !caught; r++) {
                    caught = FaceCatchesDir(&stg->faces[inc[r].face], inc[r].corner, w);
                }
                anyFeasible = !caught;
            }
        }
    }
    if (areaSum <= 0.0f || !anyFeasible) {
        return 0;
    }
    out->v[0] = v[0];
    out->v[1] = v[1];
    out->v[2] = v[2];
    out->cx = sx / areaSum;
    out->cz = sz / areaSum;
    return 1;
}

/* Finds the candidates among the faces from job.candNext on, within CAND_BUILD of the build centre. It stops when the
 * job's time is up and goes on in the next game frame. */
static void CandsRun(void) {
    while (job.candNext < stg->faceCount) {
        const MFace *f = &stg->faces[job.candNext];
        int i = job.candNext;

        if (f->n[1] < MIN_NY || f->hi[0] < stg->x - CAND_BUILD || f->lo[0] > stg->x + CAND_BUILD ||
            f->hi[2] < stg->z - CAND_BUILD || f->lo[2] > stg->z + CAND_BUILD) {
            job.candNext++;
            continue;
        }
        for (; job.candCorner < 3; job.candCorner++) {
            int k = job.candCorner;
            f32 dx = f->v[k][0] - stg->x, dz = f->v[k][2] - stg->z;
            u32 t0;

            if (dx * dx + dz * dz > CAND_BUILD * CAND_BUILD) {
                continue;
            }
            if (stg->candCount >= MAX_CANDS) {
                stg->cap = 1;
                job.candNext = stg->faceCount;
                break;
            }
            /* One call can take a good part of a millisecond. */
            if (osGetCount() - stepStart + job.candMax > budget) {
                return;
            }
            t0 = osGetCount();
            if (MakeCandidate(i, k, &stg->cands[stg->candCount])) {
                stg->candCount++;
            }
            if (osGetCount() - t0 > job.candMax) {
                job.candMax = osGetCount() - t0;
            }
        }
        if (job.candNext < stg->faceCount) {
            job.candNext++;
            job.candCorner = 0;
        }
    }
    /* Done: the staging buffer takes over. */
    {
        Buf *t = act;

        act = stg;
        stg = t;
        bufSerial++;
    }
    job.stage = JOB_IDLE;
    buildAge = 0;
    rebuilds++;
    if (act->full) {
        reach = reach * 0.75f > REACH_MIN ? reach * 0.75f : REACH_MIN;
    } else if (act->faceCount < MAX_FACES / 2) {
        reach = reach * 1.25f + 1.0f < REACH_MAX ? reach * 1.25f + 1.0f : REACH_MAX;
    }
}

/* Goes on with the build, if there is one. */
static void JobRun(void) {
    if (job.stage == JOB_COLLECT) {
        CollectRun();
    }
    if (job.stage == JOB_CANDS) {
        CandsRun();
    }
}

/* ---------------------------------------------------------------------------------------------------------
 * The slam against the faces (tools/tricks/moonpound_trainer.py, moonpound.py)
 */

typedef struct Sim {
    u8 verdict;
    f32 P[PASSES + 1][3];
    f32 R[PASSES];
} Sim;

static f32 mJump, mSlam;

/* The slams of the strip, kept for the next game frame (see MoonAimStep). */
static Sim cellSim[MOON_CELLS];
static u8 cellOk[MOON_CELLS];      /* cellSim[k] is a verdict for the current state */
static f32 predPos[3], predVel[3]; /* the state that the next game frame should start from, if cacheOk */
static u32 cacheSerial;
static f32 cacheMJump, cacheRadius, cacheSlam, cacheGravity;

/* The centre of the glove in each pass, and the radius, for a slam that Z starts on the pass after the first one. */
static void MakePath(const f32 *pos, const f32 *vel, Sim *s) {
    f32 vx = vel[0], vy = vel[1], vz = vel[2];
    f32 r = in->radius;

    s->P[0][0] = pos[0];
    s->P[0][1] = pos[1];
    s->P[0][2] = pos[2];
    for (int k = 0; k < PASSES; k++) {
        f32 dy, d;

        if (k >= 2) {
            r += (in->slamRadius - r) / 4.0f;
        }
        s->R[k] = r;
        if (k <= 2) {
            dy = vy;
            if (k < 2) {
                vy -= in->gravity;
            }
        } else if (k <= 4) {
            dy = 0.0f;
        } else {
            dy = -FALL;
        }
        if (k == 2) {
            vy = 0.0f;
        }
        s->P[k + 1][0] = s->P[k][0] + vx;
        s->P[k + 1][1] = s->P[k][1] + dy;
        s->P[k + 1][2] = s->P[k][2] + vz;
        d = Sqrt(vx * vx + vz * vz);
        if (d < 0.1f) {
            vx = vz = 0.0f;
        } else {
            f32 m = k < 2 ? mJump : mSlam;

            vx *= m;
            vz *= m;
        }
    }
}

/* collision.c NearEdges. */
static int NearEdge(const f32 *pos, const MFace *f, f32 r) {
    for (int j = 0; j < 3; j++) {
        f32 t, e[3], d[3];

        if (f->elen[j] == -1.0f) {
            continue;
        }
        t = Dot(f->edir[j], pos) - Dot(f->v[j], f->edir[j]);
        if (t < 0.0f || t > f->elen[j]) {
            continue;
        }
        for (int i = 0; i < 3; i++) {
            e[i] = f->v[j][i] + f->edir[j][i] * t;
            d[i] = pos[i] - e[i];
        }
        if (Dot(d, d) < r * r) {
            return 1;
        }
    }
    return 0;
}

/* CollideActorWithPlane for one sphere. 0: no contact, 1: an inside contact (after is the new centre), 2: an edge. */
static u32 callCount, iterCount;

static int Collide(const f32 *c, const f32 *prev, f32 r, const MFace *f, f32 *after) {
    callCount++;
    f32 pos[3] = { c[0], c[1], c[2] };
    f32 dist = -(Dot(pos, f->n) + f->d);
    f32 q[3], u0, v0, alpha = -1.0f, beta;
    const f32 *p = f->proj;

    if (dist > 0.0f) {
        if (-(Dot(prev, f->n) + f->d) > 0.0f) {
            return 0; /* seen from behind */
        }
        int k = 0;

        while (dist > 0.0f) {
            for (int i = 0; i < 3; i++) {
                pos[i] = pos[i] * 0.9f + prev[i] * 0.1f;
            }
            k++;
            iterCount++;
            dist = -(Dot(pos, f->n) + f->d);
            if (k == 20) {
                for (int i = 0; i < 3; i++) {
                    pos[i] = prev[i];
                }
                dist = -(Dot(pos, f->n) + f->d);
                break;
            }
        }
    }
    if (r < -dist) {
        return 0;
    }
    for (int i = 0; i < 3; i++) {
        q[i] = pos[i] + f->n[i] * dist;
    }
    u0 = q[f->axis[0]] - f->v[0][f->axis[0]];
    v0 = q[f->axis[1]] - f->v[0][f->axis[1]];
    if (p[0] == 0.0f) {
        beta = u0 / p[1];
        if (beta >= -TOL && beta <= 1.0f + TOL && p[2] != 0.0f) {
            alpha = (v0 - beta * p[3]) / p[2];
        }
    } else {
        f32 den = p[3] * p[0] - p[1] * p[2];

        if (Abs(den) < 1e-20f) {
            return NearEdge(pos, f, r) ? 2 : 0;
        }
        beta = (v0 * p[0] - u0 * p[2]) / den;
        if (beta >= -TOL && beta <= 1.0f + TOL) {
            alpha = (u0 - beta * p[1]) / p[0];
        }
    }
    if (alpha < -TOL || alpha + beta > 1.0f + TOL) {
        return NearEdge(pos, f, r) ? 2 : 0;
    }
    for (int i = 0; i < 3; i++) {
        after[i] = q[i] + f->n[i] * r;
    }
    return 1;
}

/* AltitudeCheckPlane's inside test of the point Q on f's plane, with the tolerance TOL (a fraction of the triangle). */
static int InsideAt(const MFace *f, const f32 *q, f32 tol) {
    f32 u0 = q[f->axis[0]] - f->v[0][f->axis[0]];
    f32 v0 = q[f->axis[1]] - f->v[0][f->axis[1]];
    const f32 *p = f->proj;
    f32 alpha, beta;

    if (p[0] == 0.0f) {
        if (p[2] == 0.0f) {
            return 0;
        }
        alpha = u0 / p[1];
        if (alpha < -tol || alpha > 1.0f + tol) {
            return 0;
        }
        beta = (v0 - alpha * p[3]) / p[2];
    } else {
        f32 den = p[3] * p[0] - p[1] * p[2];

        if (Abs(den) < 1e-20f) {
            return 0;
        }
        alpha = (v0 * p[0] - u0 * p[2]) / den;
        if (alpha < -tol || alpha > 1.0f + tol) {
            return 0;
        }
        beta = (u0 - alpha * p[1]) / p[0];
    }
    return !(beta < -tol || beta + alpha > 1.0f + tol);
}

/* The same for the point on f's plane straight below (x, z). */
static int Footprint(const MFace *f, f32 x, f32 z) {
    f32 q[3] = { x, -(f->n[0] * x + f->n[2] * z + f->d) / f->n[1], z };

    return InsideAt(f, q, TOL);
}

static int idx[MAX_FACES];

/* Plays the slam from the state at the start of a game frame, with Z on the next frame's read and the stick neutral
 * from now on. s->verdict is MC_LAND (a contact came first), MC_CLIP (the centre crossed a floor with no contact),
 * MC_NO_Z (the glove falls too fast for a slam) or MC_VOID (nothing below). */
static u16 simCount;

static void Simulate(const f32 *pos, const f32 *vel, Sim *s) {
    f32 lo0, hi0, lo2, hi2, ytop, ybot;
    int nIdx = 0;

    simCount++;
    MakePath(pos, vel, s);
    if (vel[1] - 2.0f * in->gravity <= -9.0f) {
        s->verdict = MC_NO_Z;
        return;
    }
    lo0 = hi0 = s->P[0][0];
    lo2 = hi2 = s->P[0][2];
    ytop = ybot = s->P[0][1];
    for (int k = 1; k <= PASSES; k++) {
        if (s->P[k][1] < ybot) {
            ybot = s->P[k][1];
        }
        if (s->P[k][1] > ytop) {
            ytop = s->P[k][1];
        }
        if (s->P[k][0] < lo0) {
            lo0 = s->P[k][0];
        }
        if (s->P[k][0] > hi0) {
            hi0 = s->P[k][0];
        }
        if (s->P[k][2] < lo2) {
            lo2 = s->P[k][2];
        }
        if (s->P[k][2] > hi2) {
            hi2 = s->P[k][2];
        }
    }
    lo0 -= in->slamRadius + 1.0f;
    hi0 += in->slamRadius + 1.0f;
    lo2 -= in->slamRadius + 1.0f;
    hi2 += in->slamRadius + 1.0f;
    /* A slam that reaches beyond what the buffer is sure to hold gets no verdict. */
    {
        f32 ex = Abs(lo0 - act->x) > Abs(hi0 - act->x) ? Abs(lo0 - act->x) : Abs(hi0 - act->x);
        f32 ez = Abs(lo2 - act->z) > Abs(hi2 - act->z) ? Abs(lo2 - act->z) : Abs(hi2 - act->z);

        if (act->cover <= 0.0f || ex * ex + ez * ez > act->cover * act->cover ||
            ybot - in->slamRadius - 1.0f < act->y - BUILD_BELOW || ytop + in->slamRadius + 1.0f > act->y + BUILD_ABOVE) {
            s->verdict = MC_GREY;
            return;
        }
    }
    ytop = -1e9f;
    ybot = 1e9f;
    for (int i = 0; i < act->faceCount; i++) {
        const f32 *b = act->bb[i];

        if (b[3] >= lo0 && b[0] <= hi0 && b[5] >= lo2 && b[2] <= hi2) {
            idx[nIdx++] = i;
            if (b[4] > ytop) {
                ytop = b[4];
            }
            if (b[1] < ybot) {
                ybot = b[1];
            }
        }
    }
    s->verdict = MC_VOID;
    for (int k = 0; k < PASSES; k++) {
        const f32 *prev = s->P[k];
        const f32 *cur = s->P[k + 1];
        f32 r = s->R[k] + 1.0f;

        /* Passes above every face near the column touch nothing; below them the fall is over. */
        if ((prev[1] < cur[1] ? prev[1] : cur[1]) - r > ytop) {
            continue;
        }
        if ((prev[1] > cur[1] ? prev[1] : cur[1]) + r < ybot) {
            break;
        }

        f32 ylo = (prev[1] < cur[1] ? prev[1] : cur[1]) - r;
        f32 yhi = (prev[1] > cur[1] ? prev[1] : cur[1]) + r;
        f32 xlo = (prev[0] < cur[0] ? prev[0] : cur[0]) - r;
        f32 xhi = (prev[0] > cur[0] ? prev[0] : cur[0]) + r;
        f32 zlo = (prev[2] < cur[2] ? prev[2] : cur[2]) - r;
        f32 zhi = (prev[2] > cur[2] ? prev[2] : cur[2]) + r;
        f32 c[3] = { cur[0], cur[1], cur[2] };
        int contacts = 0, crossed = 0;

        for (int q = 0; q < nIdx && contacts < 4; q++) {
            const f32 *b = act->bb[idx[q]];
            f32 after[3];
            int res;

            if (b[4] < ylo || b[1] > yhi || b[3] < xlo || b[0] > xhi || b[5] < zlo || b[2] > zhi) {
                continue;
            }
            res = Collide(c, prev, s->R[k], &act->faces[idx[q]], after);
            if (res) {
                contacts++;
                if (res == 1) {
                    c[0] = after[0];
                    c[1] = after[1];
                    c[2] = after[2];
                }
            }
        }
        if (contacts) {
            s->verdict = MC_LAND;
            return;
        }
        for (int q = 0; q < nIdx && !crossed; q++) {
            const f32 *b = act->bb[idx[q]];
            const MFace *f;

            if (b[6] < MIN_NY || cur[0] < b[0] - 1.0f || cur[0] > b[3] + 1.0f || cur[2] < b[2] - 1.0f ||
                cur[2] > b[5] + 1.0f) {
                continue;
            }
            f = &act->faces[idx[q]];
            if (Dot(f->n, prev) + f->d >= 0.0f && Dot(f->n, cur) + f->d < 0.0f && Footprint(f, cur[0], cur[2])) {
                crossed = 1;
            }
        }
        if (crossed) {
            s->verdict = MC_CLIP;
            return;
        }
    }
}

/* One jump pass with no stick input and no contact. */
static void AdvanceJump(f32 *pos, f32 *vel) {
    f32 d = Sqrt(vel[0] * vel[0] + vel[2] * vel[2]);

    pos[0] += vel[0];
    pos[1] += vel[1];
    pos[2] += vel[2];
    if (d < 0.1f) {
        vel[0] = vel[2] = 0.0f;
    } else {
        vel[0] *= mJump;
        vel[2] *= mJump;
    }
    vel[1] -= in->gravity;
}

/* Keeps the state that the next game frame should start from. */
static void Predict(const f32 *pos, const f32 *vel) {
    for (int i = 0; i < 3; i++) {
        predPos[i] = pos[i];
        predVel[i] = vel[i];
    }
    cacheOk = 1;
}

/* ---------------------------------------------------------------------------------------------------------
 * One game frame
 */

void MoonAimStep(const MoonIn *input, MoonView *out) {
    static Sim probe;
    const Sim *aim;
    int reuse;
    f32 pos[3], vel[3];
    int aimCell = -1;
    int grey[MOON_CELLS];
    u8 flags = 0;
    int candShort = 0;
    u32 start, t0, simMax = 0; /* the longest slam so far this step */

    in = input;
    budget = STEP_COUNTS;
    start = stepStart = osGetCount();
    slow = cut = 0;
    simCount = 0;
    callCount = iterCount = 0;
    {
        u8 *p = (u8 *)out;

        for (u32 i = 0; i < sizeof(*out); i++) {
            p[i] = 0;
        }
    }
    out->aimCell = 255;
    out->rebuilds = rebuilds;
    if (in->scaled) {
        out->why = MW_SCALE;
        jumpActive = 0;
        cacheOk = 0;
        return;
    }
    if (in->waterState) {
        out->why = MW_WATER;
        jumpActive = 0;
        cacheOk = 0;
        return;
    }
    if (in->mode != HAND_JUMP || in->onGround) {
        out->why = MW_NOT_JUMP;
        jumpActive = 0;
        cacheOk = 0;
        return;
    }
    /* Each jump pass keeps 0.7 of the speed (MoveHand, no stick) and the air friction (UpdateActor); a slam pass half that. */
    mJump = (1.0f - (1.0f - in->handFriction) / 4.0f) * 0.7f;
    mSlam = mJump * 0.5f;

    /* The face buffer: built again at the start of a jump, and when the glove has moved away from the build centre
     * sideways or in height (not within a few frames of the last build), and now and then. The build takes several game
     * frames; the old buffer serves the slams until the new one is complete. */
    {
        f32 dx = in->pos[0] - act->x, dz = in->pos[2] - act->z, dy = in->pos[1] - act->y;
        int first = !jumpActive;

        jumpActive = 1;
        if (buildAge < 0xFFFF) {
            buildAge++;
        }
        if (first) {
            JobStart();
        } else if (job.stage == JOB_IDLE && buildAge >= GAP_MIN &&
                   (act->cover < 0.0f || Abs(dy) > BUILD_DY || dx * dx + dz * dz > BUILD_MOVE * BUILD_MOVE ||
                    buildAge >= REFRESH)) {
            JobStart();
        }
    }
    /* With no buffer yet there is nothing to play: the build gets the whole budget. */
    if (act->faceCount == 0) {
        JobRun();
    }
    out->rebuilds = rebuilds;
    out->faces = (u16)act->faceCount;
    out->cands = (u16)act->candCount;
    if (act->faceCount == 0) {
        cacheOk = 0;
        out->why = job.stage != JOB_IDLE ? MW_BUILDING : MW_NO_FACES;
        out->flags = (job.stage != JOB_IDLE ? MOWF_BUILD : 0);
        out->counts = osGetCount() - start;
        return;
    }
    out->live = 1;
    out->why = MW_OK;

    /* The strip: Z on the read 1 to 8 game frames from now. The aim's cell is always played; the others only while
     * the budget lasts. */
    for (int k = 0; k < MOON_CELLS; k++) {
        grey[k] = (in->aHeld && in->actionTime < 5) || in->actionLock > k + 2;
        if (!grey[k] && aimCell < 0) {
            aimCell = k;
        }
    }
    /* With no stick input and no contact the next game frame's state is what AdvanceJump gives, and the slam of cell k + 1
     * now is the slam of cell k then: only the new last cell is played. Anything else (the stick moved the glove, a
     * new buffer, other physics) plays them all again. */
    reuse = cacheOk && cacheSerial == bufSerial && cacheMJump == mJump && cacheRadius == in->radius &&
            cacheSlam == in->slamRadius && cacheGravity == in->gravity;
    for (int k = 0; k < 3 && reuse; k++) {
        reuse = Abs(in->pos[k] - predPos[k]) < 1e-3f && Abs(in->vel[k] - predVel[k]) < 1e-4f;
    }
    if (reuse) {
        for (int k = 0; k + 1 < MOON_CELLS; k++) {
            cellSim[k] = cellSim[k + 1];
            cellOk[k] = cellOk[k + 1];
        }
        cellOk[MOON_CELLS - 1] = 0;
    } else {
        for (int k = 0; k < MOON_CELLS; k++) {
            cellOk[k] = 0;
        }
    }
    cacheSerial = bufSerial;
    cacheMJump = mJump;
    cacheRadius = in->radius;
    cacheSlam = in->slamRadius;
    cacheGravity = in->gravity;
    for (int k = 0; k < 3; k++) {
        pos[k] = in->pos[k];
        vel[k] = in->vel[k];
    }
    for (int k = 0; k < MOON_CELLS; k++) {
        if (!cellOk[k]) {
            if (k != aimCell && OutOfTime(simMax)) {
                cut = 1;
                out->cell[k] = MC_GREY;
                AdvanceJump(pos, vel);
                if (k == 0) {
                    Predict(pos, vel);
                }
                continue;
            }
            t0 = osGetCount();
            Simulate(pos, vel, &cellSim[k]);
            /* A cell with no verdict (the buffer does not cover it) is tried again next frame. */
            cellOk[k] = cellSim[k].verdict != MC_GREY;
            if (osGetCount() - t0 > simMax) {
                simMax = osGetCount() - t0;
            }
        }
        out->cell[k] = cellSim[k].verdict;
        if (grey[k]) {
            out->cell[k] = (u8)(out->cell[k] == MC_NO_Z ? MC_NO_Z : MC_GREY);
        }
        if (k == aimCell) {
            out->hover = cellSim[k].P[3][1];
        }
        AdvanceJump(pos, vel);
        if (k == 0) {
            Predict(pos, vel);
        }
    }
    if (in->aHeld && in->actionTime < 5) {
        flags |= MOWF_A_HELD;
    }
    for (int k = 0; k < MOON_CELLS; k++) {
        if (!grey[k] && out->cell[k] == MC_GREY) {
            cut = 1; /* no verdict: the buffer does not cover the slam, or there was no time */
        }
    }
    if (aimCell < 0) {
        /* Every cell is grey: the aim follows the first one all the same. */
        aimCell = 0;
        if (!cellOk[0]) {
            for (int k = 0; k < 3; k++) {
                pos[k] = in->pos[k];
                vel[k] = in->vel[k];
            }
            Simulate(pos, vel, &cellSim[0]);
        }
        out->hover = cellSim[0].P[3][1];
    }
    aim = &cellSim[aimCell];
    out->stripCounts = osGetCount() - start;
    out->aimCell = (u8)aimCell;
    out->col[0] = aim->P[5][0];
    out->col[1] = aim->P[5][2];

    /* The nearest hole. The candidates were found within CAND_BUILD of the build centre. */
    {
        f32 colDist = Sqrt((out->col[0] - act->x) * (out->col[0] - act->x) + (out->col[1] - act->z) * (out->col[1] - act->z));
        f32 best = CAND_BUILD - colDist;
        int bi = -1;
        f32 bd = 0.0f;

        if (best < CAND_REACH) {
            candShort = 1;
        } else {
            best = CAND_REACH;
        }
        for (int i = 0; i < act->candCount; i++) {
            const Cand *c = &act->cands[i];
            f32 d = 1e9f, tx, tz, dist;

            if (c->v[1] > in->pos[1]) {
                continue;
            }
            for (int k = 5; k <= PASSES; k++) {
                f32 h = aim->P[k][1] - c->v[1];

                if (h > 0.0f && h < d) {
                    d = h;
                }
            }
            if (d > 1e8f) {
                continue;
            }
            tx = c->v[0] + d * c->cx;
            tz = c->v[2] + d * c->cz;
            dist = Sqrt((tx - out->col[0]) * (tx - out->col[0]) + (tz - out->col[1]) * (tz - out->col[1]));
            if (dist < best) {
                best = dist;
                bi = i;
                bd = d;
            }
        }
        if (bi >= 0) {
            const Cand *c = &act->cands[bi];

            out->hasTarget = 1;
            out->vtx[0] = c->v[0];
            out->vtx[1] = c->v[1];
            out->vtx[2] = c->v[2];
            out->phase = bd;
            out->target[0] = c->v[0] + bd * c->cx;
            out->target[1] = c->v[2] + bd * c->cz;
            out->dx = out->target[0] - out->col[0];
            out->dz = out->target[1] - out->col[1];
            out->dist = best;
            if (aim->verdict == MC_CLIP) {
                out->zoneKnown = 1;
                out->dist = 0.0f;
            } else if (best < ZONE_RANGE && aim->verdict != MC_NO_Z && aim->verdict != MC_GREY && bd <= PHASE_WINDOW_TOP) {
                /* The nearest column that clips, on the line from the column through the hole's centre. */
                f32 ux = best > 1e-6f ? out->dx / best : 0.0f;
                f32 uz = best > 1e-6f ? out->dz / best : 0.0f;
                f32 span = best + 0.3f;
                f32 step = span / ZONE_STEPS < 0.02f ? 0.02f : span / ZONE_STEPS;
                f32 p0[3], v0[3];

                for (int k = 0; k < 3; k++) {
                    p0[k] = in->pos[k];
                    v0[k] = in->vel[k];
                }
                for (int k = 0; k < aimCell; k++) {
                    AdvanceJump(p0, v0);
                }
                for (f32 s = step; s <= span; s += step) {
                    f32 pp[3] = { p0[0] + ux * s, p0[1], p0[2] + uz * s };

                    if (OutOfTime(simMax)) {
                        cut = 1;
                        break;
                    }
                    Simulate(pp, v0, &probe);
                    if (probe.verdict == MC_CLIP) {
                        out->zoneKnown = 1;
                        out->dist = s;
                        break;
                    }
                }
            }
        }
    }
    /* The direction on the screen, and the stick that would close the gap. */
    if (out->hasTarget) {
        f32 sn = __sinf(in->camYaw), cs = __cosf(in->camYaw);
        f32 sx, sy;

        out->fwd = out->dx * sn + out->dz * cs;
        out->right = -out->dx * cs + out->dz * sn;
        sx = out->right * STICK_PER_UNIT;
        sy = out->fwd * STICK_PER_UNIT;
        out->stickX = Abs(sx) < 0.5f ? 0.0f : sx + (sx > 0.0f ? STICK_DEAD : -STICK_DEAD);
        out->stickY = Abs(sy) < 0.5f ? 0.0f : sy + (sy > 0.0f ? STICK_DEAD : -STICK_DEAD);
    }
    JobRun();
    out->rebuilds = rebuilds;
    out->faces = (u16)act->faceCount;
    out->cands = (u16)act->candCount;
    out->flags = flags | (in->noExit ? MOWF_NO_EXIT : 0) | (act->full ? MOWF_FULL : 0) | (cut ? MOWF_SLOW : 0) |
                 ((act->cap || candShort) ? MOWF_CANDS : 0) | (job.stage != JOB_IDLE ? MOWF_BUILD : 0);
    out->counts = osGetCount() - start;
    out->sims = simCount;
    out->calls = callCount > 65535 ? 65535 : (u16)callCount;
    out->iters = iterCount > 65535 ? 65535 : (u16)iterCount;
}

/* ---------------------------------------------------------------------------------------------------------
 * The hole painter (the collision view's "Mark holes")
 */

HoleView holeViews[2];
volatile u32 holeSeq, holeBegin;
u32 holeLast, holeWorst; /* the CPU counts of the last painter step and of the longest one since the level began */

/* Copies the active buffer's candidates into the view that the draw thread reads next. */
static void PublishHoles(void) {
    HoleView *v = &holeViews[(holeSeq + 1) & 1];
    int n = act->candCount < HOLE_MAX ? act->candCount : HOLE_MAX;

    holeBegin = holeSeq + 1;
    __asm__ volatile("" ::: "memory");
    v->count = (u8)n;
    v->capped = act->cap;
    for (int i = 0; i < n; i++) {
        v->p[i][0] = act->cands[i].v[0] + act->cands[i].cx;
        v->p[i][1] = act->cands[i].v[1];
        v->p[i][2] = act->cands[i].v[2] + act->cands[i].cz;
    }
    __asm__ volatile("" ::: "memory");
    holeSeq = holeSeq + 1;
}

/* One game frame, while the collision view and the painter are on: keeps the face buffer and its candidates up to date
 * around the glove, as the aim does in a jump, with a smaller budget. The buffer is shared with the aim: whichever
 * runs first starts a build, and a build that is under way goes on in both. */
void HolesGameFrame(void) {
    static MoonIn hin;
    static u32 seenStarts, seenLoads, shown = 0xFFFFFFFF;
    f32 dx, dz, dy;

    if (!ActionsInLevel() || !SafeFloats(&hand.pos.x, 3)) {
        return;
    }
    if (seenStarts != practice.levelStarts || seenLoads != stateLoads) {
        seenStarts = practice.levelStarts;
        seenLoads = stateLoads;
        holeWorst = 0;
        MoonAimReset();
    }
    in = &hin;
    budget = HOLE_COUNTS;
    stepStart = osGetCount();
    hin.pos[0] = hand.pos.x;
    hin.pos[1] = hand.pos.y;
    hin.pos[2] = hand.pos.z;
    if (buildAge < 0xFFFF) {
        buildAge++;
    }
    dx = hin.pos[0] - act->x;
    dz = hin.pos[2] - act->z;
    dy = hin.pos[1] - act->y;
    if (job.stage == JOB_IDLE && buildAge >= GAP_MIN &&
        (act->cover < 0.0f || Abs(dy) > BUILD_DY || dx * dx + dz * dz > BUILD_MOVE * BUILD_MOVE || buildAge >= REFRESH)) {
        JobStart();
    }
    JobRun();
    if (shown != bufSerial) {
        shown = bufSerial;
        PublishHoles();
    }
    holeLast = osGetCount() - stepStart;
    if (holeLast > holeWorst) {
        holeWorst = holeLast;
    }
}

/* ---------------------------------------------------------------------------------------------------------
 * The wall in front of the ball (the wall-clip indicator)
 */

#define WALL_RANGE 60.0f    /* walls this far from the ball (plane distance) and its column are looked at */
#define WALL_BELOW 30.0f    /* faces from this far below the ball's centre ... */
#define WALL_ABOVE 30.0f    /* ... to this far above it */
#define WALL_MAX_NY 0.3f    /* the largest |normal.y| of a wall (the collision view's rule) */
#define WALL_MIN_COS 0.2f   /* the ball's direction must hit the face at least this square on */
#define WALL_TOL 0.02f      /* the inside test is this generous, so the seam between two triangles is no gap */

/* Looks at the faces of one actor for a wall in front of POS along DIR (horizontal, unit), and keeps the nearest in OUT. */
static void WallActor(const Actor *a, const f32 *pos, const f32 *dir, const f32 *vel, WallHit *out) {
    static MFace tmp;
    const CollFace *list = a->faces;
    const ObjectInstance *inst = a->inst;
    f32 dx, dz, rr;
    int n;

    if (!SafePointer(list) || !SafePointer(inst) || !SafePointer(inst->obj) || !SafePointer(inst->obj->mesh) ||
        !SafeFloat(&a->pos.x) || !SafeFloat(&a->pos.z) || !SafeFloat(&a->radius) || a->radius < 0.0f) {
        return;
    }
    n = inst->obj->mesh->numFaces;
    if (n < 1 || n > 8192) {
        return;
    }
    dx = a->pos.x - pos[0];
    dz = a->pos.z - pos[2];
    rr = WALL_RANGE + a->radius;
    if (dx * dx + dz * dz > rr * rr) {
        return;
    }
    for (int i = 0; i < n; i++) {
        f32 nd, dist, t, hit[3];

        if (!SafePointer(&list[i]) || !SafePointer((const u8 *)&list[i] + sizeof(CollFace) - 4)) {
            return;
        }
        if (!LoadFace(&list[i], &tmp, pos[0] - WALL_RANGE, pos[0] + WALL_RANGE, pos[2] - WALL_RANGE,
                      pos[2] + WALL_RANGE, pos[1] - WALL_BELOW, pos[1] + WALL_ABOVE)) {
            continue;
        }
        if (Abs(tmp.n[1]) >= WALL_MAX_NY) {
            continue;
        }
        nd = tmp.n[0] * dir[0] + tmp.n[2] * dir[2];
        dist = Dot(tmp.n, pos) + tmp.d;
        if (nd > -WALL_MIN_COS || dist < 0.0f || dist > WALL_RANGE || (out->found && dist >= out->dist)) {
            continue;
        }
        t = dist / -nd;
        for (int k = 0; k < 3; k++) {
            hit[k] = pos[k] + (k == 1 ? 0.0f : dir[k] * t);
        }
        if (!InsideAt(&tmp, hit, WALL_TOL)) {
            continue;
        }
        out->found = 1;
        out->dist = dist;
        out->cosA = -nd;
        out->speedN = -(tmp.n[0] * vel[0] + tmp.n[2] * vel[2]);
        for (int k = 0; k < 3; k++) {
            out->point[k] = hit[k];
            out->normal[k] = tmp.n[k];
        }
    }
}

/* The nearest wall in front of a ball at POS that moves with the horizontal velocity VEL (units a game frame): the
 * distance from the centre to its plane, the speed along its normal and how square it stands. 0 when the ball is too
 * slow to have a direction or no wall is in range. The game's landscape and its static platforms are walked. */
int MoonWallScan(const f32 *pos, const f32 *vel, WallHit *out) {
    f32 sp = Sqrt(vel[0] * vel[0] + vel[2] * vel[2]);
    f32 dir[3];
    const Actor *a;
    const Platform *p;
    u32 t0 = osGetCount();

    out->found = 0;
    out->dist = out->speedN = out->cosA = 0.0f;
    out->point[0] = out->point[1] = out->point[2] = 0.0f;
    out->normal[0] = out->normal[1] = out->normal[2] = 0.0f;
    if (sp < 1.0f) {
        out->counts = 0;
        return 0;
    }
    dir[0] = vel[0] / sp;
    dir[1] = 0.0f;
    dir[2] = vel[2] / sp;
    a = collisionList.head.collNext;
    for (int i = 0; i < 1024 && SafePointer(a) && a != &collisionList.head; i++, a = a->collNext) {
        WallActor(a, pos, dir, vel, out);
    }
    p = platformList.head.next;
    for (int i = 0; i < 256 && SafePointer(p) && p != &platformList.head; i++, p = p->next) {
        if (!(p->flags & 0x84)) {
            WallActor(&p->actor, pos, dir, vel, out);
        }
    }
    out->counts = osGetCount() - t0;
    return out->found;
}

/* ---------------------------------------------------------------------------------------------------------
 * Drawing
 */

#define WHITE 0xFFFFFFFF
#define DIM 0x707888FF
#define LABEL 0xC0D0FFFF
#define GREEN 0x60FF60FF
#define YELLOW 0xFFE070FF
#define RED 0xFF5050FF

#define MO_X 80
#define MO_Y 164
#define LINE 10
#define CELL_W 18

static void Line(Gfx **dl, int x, int y, u32 color, const char *fmt, ...) {
    char buf[48];
    va_list args;

    va_start(args, fmt);
    FormatV(buf, sizeof(buf), fmt, args);
    va_end(args);
    /* Text that would run off the screen is cut. */
    for (int n = 0; buf[n] != 0; n++) {
        if (x + (n + 1) * TEXT_ADVANCE > 320) {
            buf[n] = 0;
            break;
        }
    }
    TextPrint(dl, x, y, 1, color, buf);
}

void MoonAimDraw(Gfx **dl, const MoonView *v) {
    static const char *const whyText[] = { "", "", "IN WATER", "GLOVE SCALED", "NO FACES", "BUILDING" };
    int x = MO_X, y = MO_Y;

    if (!v->live) {
        Line(dl, x, y, DIM, "MOON POUND %s", v->why < 6 ? whyText[v->why] : "");
        return;
    }
    Line(dl, x, y, LABEL, "MOON POUND");
    if (v->flags & MOWF_NO_EXIT) {
        Line(dl, x + 66, y, RED, "NO EXIT");
    }
    if (v->flags & MOWF_SLOW) {
        Line(dl, x + 120, y, YELLOW, "SLOW");
    }
    if (v->flags & MOWF_FULL) {
        Line(dl, x + 150, y, YELLOW, "FULL");
    }
    if (v->flags & MOWF_CANDS) {
        Line(dl, x + 180, y, YELLOW, "HOLES?");
    }
    if (v->flags & MOWF_BUILD) {
        Line(dl, x + 222, y, DIM, "BUILD");
    }
    y += LINE;
    /* The strip: Z 1 to 8 game frames from now. Green clips, red lands, dark is greyed out. */
    for (int k = 0; k < MOON_CELLS; k++) {
        int cx = x + k * CELL_W;
        u8 c = v->cell[k];
        u32 back = c == MC_CLIP ? 0x30B030C0 : c == MC_LAND ? 0xA03030A0 : c == MC_VOID ? 0x606030A0 : 0x30384090;
        u32 fore = c == MC_CLIP ? WHITE : c == MC_GREY || c == MC_NO_Z ? DIM : WHITE;

        TextRect(dl, cx, y, cx + CELL_W - 2, y + 10, back);
        if (k == v->aimCell) {
            TextRect(dl, cx, y + 10, cx + CELL_W - 2, y + 11, WHITE);
        }
        Line(dl, cx + 2, y + 1, fore, "+%d", k + 1);
    }
    y += 14;
    if (!v->hasTarget) {
        Line(dl, x, y, DIM, "NO HOLE NEAR");
        y += LINE;
    } else {
        Line(dl, x, y, WHITE, "AIM %+.2f %+.2f", (double)v->dx, (double)v->dz);
        y += LINE;
        if (v->zoneKnown && v->dist == 0.0f) {
            Line(dl, x, y, GREEN, "DIST 0.00 INSIDE");
        } else {
            Line(dl, x, y, v->zoneKnown ? YELLOW : WHITE, "DIST %.2f%s", (double)v->dist, v->zoneKnown ? " TO CLIP" : "");
        }
        y += LINE;
        /* A map of the screen: the column in the middle, the hole's centre as a square. One unit is 18 pixels. */
        {
            int mx = x + 100, my = y - 6, half = 22;
            f32 fr = v->right * 18.0f, ff = -v->fwd * 18.0f;
            int px, py;

            TextRect(dl, mx - half, my - half, mx + half + 1, my + half + 1, 0x20283080);
            TextRect(dl, mx - half, my, mx + half + 1, my + 1, 0x60708090);
            TextRect(dl, mx, my - half, mx + 1, my + half + 1, 0x60708090);
            px = (int)fr;
            py = (int)ff;
            if (px < -half + 2) {
                px = -half + 2;
            }
            if (px > half - 2) {
                px = half - 2;
            }
            if (py < -half + 2) {
                py = -half + 2;
            }
            if (py > half - 2) {
                py = half - 2;
            }
            TextRect(dl, mx + px - 2, my + py - 2, mx + px + 3, my + py + 3,
                     v->zoneKnown && v->dist == 0.0f ? GREEN : YELLOW);
        }
        /* The stick that closes the gap, when one tap can. */
        if (v->dist > 0.0f && Abs(v->stickX) <= 79.0f && Abs(v->stickY) <= 79.0f) {
            Line(dl, x, y, LABEL, "STICK %+.0f %+.0f", (double)v->stickX, (double)v->stickY);
        } else if (v->dist > 0.0f) {
            Line(dl, x, y, DIM, "STICK TOO FAR");
        }
        y += LINE;
        /* The phase: the centre's height above the vertex in the pass before the cross, modulo the fall of 14. The
         * good band is 0.2 to 1.9. */
        {
            int gx = x, gw = 56;
            f32 ph = v->phase;

            while (ph > FALL) {
                ph -= FALL;
            }
            TextRect(dl, gx, y + 2, gx + gw, y + 6, 0x30384090);
            TextRect(dl, gx + (int)(0.2f * gw / FALL), y + 2, gx + (int)(1.9f * gw / FALL) + 1, y + 6, 0x30B030C0);
            TextRect(dl, gx + (int)(ph * gw / FALL), y, gx + (int)(ph * gw / FALL) + 2, y + 8, WHITE);
            Line(dl, gx + gw + 4, y, DIM, "PHASE %.2f", (double)v->phase);
        }
    }
}
