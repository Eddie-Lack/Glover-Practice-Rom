"""Standalone model of the glove's slam against the collision faces (gloveworld/src/collision.c).

Code mirrored here:
  CollideActorWithPlane  collision.c 796-1000  (one sphere against one face)
  NearEdges              collision.c 774-794
  CheckCollisions        collision.c 502-763   (the faces of the landscape actors, then the platforms)
  MaintainAction HAND_SLAM  hand.c 1801-1811   (vel.y 0 for actionTime < 3, then -14)
  UpdateActor radius step   actor.c 372-379    (r += clamp((target - r) / 4, +-radiusRate))

All arithmetic is in doubles (the game uses 32-bit floats). The decisions that matter here (a point just inside or
outside a triangle by the 0.001 tolerance) are near-ties only in a region far smaller than the clip zones.
"""
import json
import math
import os

HERE = os.path.dirname(os.path.abspath(__file__))


def dot(a, b):
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]


def sub(a, b):
    return (a[0] - b[0], a[1] - b[1], a[2] - b[2])


def add(a, b):
    return (a[0] + b[0], a[1] + b[1], a[2] + b[2])


def mul(a, s):
    return (a[0] * s, a[1] * s, a[2] * s)


def length(a):
    return math.sqrt(dot(a, a))


class Face:
    """A CollFace (gloveworld/include/actor.h) read from the game: plane, corners, edge directions and lengths."""
    __slots__ = ("n", "d", "v", "edir", "elen", "proj", "axis", "terrain", "src", "idx")

    def __init__(self, f, src="", idx=0):
        self.n = tuple(f["plane"][:3])
        self.d = f["plane"][3]
        self.v = [tuple(x) for x in f["v"]]
        self.elen = list(f["edgeLen"])
        self.edir = [tuple(x) for x in f["edgeDir"]] if "edgeDir" in f else self._edir()
        self.proj = list(f["proj"])
        self.axis = list(f["axis"])
        self.terrain = f.get("terrain", 0)
        self.src = src
        self.idx = idx

    def _edir(self):
        out = []
        for i in range(3):
            dv = sub(self.v[(i + 1) % 3], self.v[i])
            L = length(dv)
            out.append(mul(dv, 1.0 / L) if L else dv)
        return out


def near_edges(point, f, r):
    """collision.c NearEdges: the first edge whose interior (t in [0, len]) is nearer than r. Returns (dist, point)."""
    for j in range(3):
        if f.elen[j] == -1.0:
            continue
        t = dot(f.edir[j], point) - dot(f.v[j], f.edir[j])
        if t < 0.0 or t > f.elen[j]:
            continue
        e = add(f.v[j], mul(f.edir[j], t))
        d = sub(point, e)
        if dot(d, d) < r * r:
            return length(d), e
    return 0.0, None


def collide_plane(c, prev, r, f, trace=None):
    """One sphere (centre C, previous centre PREV, radius R) against face F, as CollideActorWithPlane does for the
    first sphere. Returns None (no contact), ("face", q, pos_after) or ("edge", edge_dist, edge_point)."""
    p = f.proj
    if (p[2] == 0.0 and p[3] == 0.0) or (p[0] == 0.0 and p[1] == 0.0):
        return None
    if any(x != x for x in f.n) or f.d != f.d or max(f.axis) > 2:
        return None  # a zero-area face: NaN plane, never a contact
    n, d = f.n, f.d
    pos = c
    dist = -(dot(pos, n) + d)
    if dist > 0.0:
        if -(dot(prev, n) + d) <= 0.0:
            k = 0
            while dist > 0.0:
                pos = add(mul(pos, 0.9), mul(prev, 0.1))
                k += 1
                dist = -(dot(pos, n) + d)
                if k == 20:
                    pos = prev
                    dist = -(dot(pos, n) + d)
                    break
        else:
            return None
    if r < -dist:
        return None
    q = add(pos, mul(n, dist))
    a0, a1 = f.axis
    u0 = q[a0] - f.v[0][a0]
    v0 = q[a1] - f.v[0][a1]
    alpha = -1.0
    if p[0] == 0.0:
        beta = u0 / p[1]
        if -0.001 <= beta <= 1.001:
            alpha = (v0 - beta * p[3]) / p[2]
    else:
        beta = (v0 * p[0] - u0 * p[2]) / (p[3] * p[0] - p[1] * p[2])
        if -0.001 <= beta <= 1.001:
            alpha = (u0 - beta * p[1]) / p[0]
    if alpha < -0.001 or alpha + beta > 1.001:
        ed, ep = near_edges(pos, f, r)
        if ed == 0.0:
            return None
        return ("edge", ed, ep, pos)
    return ("face", q, add(q, mul(n, r)), pos, alpha, beta)


def check_collisions(c, prev, r, faces):
    """collision.c CheckCollisions for one sphere: walk the faces in order; after an interior contact the centre moves
    to q + n r and later faces see the new centre. Returns (contacts, final_centre). contacts is a list of
    (face, result)."""
    contacts = []
    cur = c
    for f in faces:
        res = collide_plane(cur, prev, r, f)
        if res is None:
            continue
        contacts.append((f, res))
        if res[0] == "face":
            cur = res[2]
            if len(contacts) >= 4:
                break
    return contacts, cur


SLAM_RADIUS = [13.0]  # radius at the start of the slam (hand sphere 13)


def radius_series(n, start=13.0, target=16.0, rate=4.0):
    """Sphere radius after each UpdateActor step with the target set (actor.c)."""
    out = []
    r = start
    for _ in range(n):
        r += min(max(-rate, (target - r) / 4.0), rate)
        out.append(r)
    return out


def load_faces(hub, names=None, near=None, radius=400.0):
    """All faces of the hub dump (out/hubN.json): landscape actors first, then platforms, as CheckCollisions walks them.
    NAMES filters by actor name; NEAR (x, z) keeps actors within RADIUS of it."""
    W = json.load(open(os.path.join(HERE, "out", "hub%d.json" % hub)))
    faces = []
    for group in ("landscape", "platforms"):
        for a in W[group]:
            if names and a["name"] not in names:
                continue
            if group == "platforms" and (a["pflags"] & 0x84):
                continue  # CheckCollisions and Altitude skip platforms with flag 0x4 or 0x80 (collision.c 548, 1319)
            if near is not None:
                if math.hypot(a["pos"][0] - near[0], a["pos"][2] - near[1]) > radius + a["radius"]:
                    continue
            for i, f in enumerate(a["cfaces"]):
                faces.append(Face(f, "%s@%.0f,%.0f" % (a["name"], a["pos"][0], a["pos"][2]), i))
    return faces


def fall_slam(x, z, hover_y, faces, r_start=14.0, max_passes=40, dy=-14.0, rad_first=15.051):
    """The fall of a slam at a fixed (x, z): the centre starts at HOVER_Y and moves DY a pass. The radius is 15.051
    in the first falling pass (measured, exp_slam) and follows the target-16 series after. Returns a list of
    (pass, centre y, radius, outcome) up to the first contact."""
    rs = radius_series(8, start=15.051 - (16 - 15.051) / 3.0)  # not used; see below
    r = rad_first
    log = []
    cur = (x, hover_y, z)
    for k in range(max_passes):
        prev = cur
        cur = (x, prev[1] + dy, z)
        contacts, fin = check_collisions(cur, prev, r, faces)
        log.append((k, cur[1], r, [(c[0].src, c[0].idx, c[1][0]) for c in contacts]))
        if contacts:
            return log, fin
        r += (16.0 - r) / 4.0
    return log, cur
