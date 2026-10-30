"""The dead cone of a mesh vertex: the cheap test behind the Moon Pound indicator.

Theory (notes.md, "Why a vertex leaks"): CollideActorWithPlane finds no contact for a sphere centre P just above a mesh
vertex V when every real (non-inner) edge at V satisfies (P - V) . e < 0, e being the unit vector from V along the edge:
the edge's t falls outside [0, len] (NearEdges), and then the projection of P onto each face at V is outside the face.
Writing P - V = d * (wx, 1, wz) (d the height above V) the condition is a set of linear inequalities in (wx, wz):
    e.x * wx + e.z * wz < -e.y        for every real edge e at V.
The dead cone is that convex polygon K; the zone at height d is V.xz + d * K.
"""
import collections
import json
import math
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import moonpound as model


def clip(poly, a, c):
    """Keep the part of the polygon with a.x*x + a.z*z < c (Sutherland-Hodgman against one half plane)."""
    out = []
    n = len(poly)
    for i in range(n):
        p, q = poly[i], poly[(i + 1) % n]
        fp = a[0] * p[0] + a[1] * p[1] - c
        fq = a[0] * q[0] + a[1] * q[1] - c
        if fp < 0:
            out.append(p)
        if (fp < 0) != (fq < 0):
            t = fp / (fp - fq)
            out.append((p[0] + t * (q[0] - p[0]), p[1] + t * (q[1] - p[1])))
    return out


def polygon_stats(poly):
    if len(poly) < 3:
        return 0.0, (0.0, 0.0)
    A = cx = cz = 0.0
    for i in range(len(poly)):
        x0, z0 = poly[i]
        x1, z1 = poly[(i + 1) % len(poly)]
        cr = x0 * z1 - x1 * z0
        A += cr
        cx += (x0 + x1) * cr
        cz += (z0 + z1) * cr
    A *= 0.5
    if abs(A) < 1e-12:
        return 0.0, (0.0, 0.0)
    return abs(A), (cx / (6 * A), cz / (6 * A))


def vertex_edges(faces, tol=0.01):
    """Group the corners of FACES by position (tolerance TOL, as CheckForInnerEdges matches) and return
    {key: dict(v=position, edges=[unit vectors from the vertex along real edges], faces=[...])}."""
    verts = collections.OrderedDict()
    for F in faces:
        for k in range(3):
            v = F.v[k]
            key = (round(v[0], 1), round(v[1], 1), round(v[2], 1))
            e = verts.setdefault(key, dict(v=v, edges=[], faces=[], up=0, fk=[]))
            e["faces"].append((F.src, F.idx))
            e["fk"].append((F, k))
            if F.n[1] >= 0.1:
                e["up"] += 1
            # the edge leaving this corner: edge k, from corner k to corner k+1
            if F.elen[k] != -1.0 and F.elen[k] != 0.0:
                e["edges"].append(tuple(F.edir[k]))
            # the edge arriving at this corner: edge k-1, from corner k-1 to corner k; from V it points back
            j = (k - 1) % 3
            if F.elen[j] != -1.0 and F.elen[j] != 0.0:
                e["edges"].append(tuple(-c for c in F.edir[j]))
    return verts


def dead_cone(edges, box=8.0):
    """The polygon K of (wx, wz) with e.x*wx + e.z*wz < -e.y for all edges. Empty list when there is none."""
    poly = [(-box, -box), (box, -box), (box, box), (-box, box)]
    for e in edges:
        poly = clip(poly, (e[0], e[2]), -e[1])
        if not poly:
            return []
    return poly


def inradius(poly, centre):
    """Distance from CENTRE to the nearest polygon side (the zone radius per unit height if centre is inside)."""
    best = 1e9
    n = len(poly)
    for i in range(n):
        p, q = poly[i], poly[(i + 1) % n]
        dx, dz = q[0] - p[0], q[1] - p[1]
        L = math.hypot(dx, dz)
        if L == 0:
            continue
        d = abs((centre[0] - p[0]) * dz - (centre[1] - p[1]) * dx) / L
        best = min(best, d)
    return best


def wedge_planes(F, k):
    """Plan-view wedge of face F at its corner k: the two edge directions (horizontal parts, from the corner), ordered
    counter-clockwise (x to z), or None for a face seen edge-on."""
    a = F.edir[k]
    j = (k - 1) % 3
    b = tuple(-c for c in F.edir[j])
    ah, bh = (a[0], a[2]), (b[0], b[2])
    cross = ah[0] * bh[1] - ah[1] * bh[0]
    if abs(cross) < 1e-9:
        return None
    if cross < 0:
        ah, bh = bh, ah
    return ah, bh


def floor_zone(fk_list, K, min_ny=0.25):
    """The parts of the dead cone K that lie over a floor: K clipped to the plan wedge of each up-facing face at the vertex.
    FK_LIST is [(face, corner index)]. Returns a list of polygons."""
    out = []
    for F, k in fk_list:
        if F.n[1] < min_ny:
            continue
        w = wedge_planes(F, k)
        if w is None:
            continue
        ah, bh = w
        poly = clip(K, (ah[1], -ah[0]), 0.0)
        if poly:
            poly = clip(poly, (-bh[1], bh[0]), 0.0)
        if len(poly) >= 3 and polygon_stats(poly)[0] > 1e-9:
            out.append(poly)
    return out


def face_catches_dir(F, k, w):
    """The small-height limit of CollideActorWithPlane for a centre at V + d*w (w.y = 1): does face F, with V at its corner K,
    make an inside contact? (an edge contact is the polar-cone condition, handled by dead_cone)"""
    n = F.n
    nw = n[0] * w[0] + n[1] * w[1] + n[2] * w[2]
    if nw <= 0.0:
        return False  # behind the face: skipped (prev is behind too)
    a = F.edir[k]
    j = (k - 1) % 3
    b = tuple(-c for c in F.edir[j])
    def cr(u, v):
        return (u[1] * v[2] - u[2] * v[1], u[2] * v[0] - u[0] * v[2], u[0] * v[1] - u[1] * v[0])
    c1 = cr(a, w)
    c2 = cr(w, b)
    return (c1[0] * n[0] + c1[1] * n[1] + c1[2] * n[2] >= 0.0) and (c2[0] * n[0] + c2[1] * n[1] + c2[2] * n[2] >= 0.0)


def feasible_dirs(fk_list, parts):
    """Sample the floor parts of the dead cone and keep the directions no incident face catches. Returns a list of (wx, wz)."""
    out = []
    for poly in parts:
        A, cen = polygon_stats(poly)
        pts = [cen] + [(cen[0] + 0.9 * (p[0] - cen[0]), cen[1] + 0.9 * (p[1] - cen[1])) for p in poly]
        for (wx, wz) in pts:
            if math.hypot(wx, wz) > 3.0:
                continue
            w = (wx, 1.0, wz)
            if not any(face_catches_dir(F, k, w) for F, k in fk_list):
                out.append((wx, wz))
    return out


def candidates(faces, min_ny=0.25):
    """Vertices whose dead cone reaches over a floor: the cheap filter. Returns a list of dicts with the vertex, the cone
    polygon K, the floor parts and the zone centre (per unit height)."""
    verts = vertex_edges(faces)
    out = []
    for key, e in verts.items():
        if not e["up"]:
            continue
        K = dead_cone(e["edges"])
        if not K:
            continue
        parts = floor_zone(e["fk"], K, min_ny)
        if not parts:
            continue
        dirs = feasible_dirs(e["fk"], parts)
        if not dirs:
            continue
        area = sum(polygon_stats(p)[0] for p in parts)
        cen_x = sum(polygon_stats(p)[0] * polygon_stats(p)[1][0] for p in parts) / area
        cen_z = sum(polygon_stats(p)[0] * polygon_stats(p)[1][1] for p in parts) / area
        out.append(dict(key=key, v=e["v"], K=K, parts=parts, area=area, centre=(cen_x, cen_z), nedges=len(e["edges"]),
                        faces=e["faces"][:3]))
    return out


if __name__ == "__main__":
    hub = int(sys.argv[1])
    faces = model.load_faces(hub)
    verts = vertex_edges(faces)
    cand = candidates(faces)
    keys = {c["key"] for c in cand}
    print("vertices", len(verts), "candidates (dead cone over a floor face)", len(cand))
    leaks = json.load(open(os.path.join(os.path.dirname(os.path.abspath(__file__)), "out", "leaks_hub%d.json" % hub)))
    found = sum((round(d["v"][0], 1), round(d["v"][1], 1), round(d["v"][2], 1)) in keys for d in leaks)
    print("scan leaks %d; with a candidate cone %d; candidates that leak in no phase of the scan: %d" % (len(leaks), found, len(cand) - found))
    missing = [d for d in leaks if (round(d["v"][0], 1), round(d["v"][1], 1), round(d["v"][2], 1)) not in keys]
    for d in missing[:10]:
        print("  leak without candidate:", d["v"], d["faces"][:2], "rmax", d["rmax"])
