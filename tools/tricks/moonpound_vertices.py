"""Vectorised (numpy) version of model.collide_plane for sweeping many columns at once. Same logic, same order."""
import numpy as np

CAP20 = True      # False: let the 20th interpolation step stand instead of falling back to prevPos (what-if)
TOL = 0.001       # barycentric tolerance of the inside test


class VFace:
    def __init__(self, f):
        self.n = np.array(f.n)
        self.d = f.d
        self.v = np.array(f.v)
        self.elen = np.array(f.elen)
        self.edir = np.array(f.edir)
        self.proj = f.proj
        self.axis = f.axis
        self.degenerate = (f.proj[2] == 0.0 and f.proj[3] == 0.0) or (f.proj[0] == 0.0 and f.proj[1] == 0.0)
        # zero-area faces have a NaN plane and a garbage axis; they never make a contact in the game either
        if any(x != x for x in f.n) or f.d != f.d or max(f.axis) > 2:
            self.degenerate = True
        self.src, self.idx = f.src, f.idx


def face_catches(P, prev, r, F):
    """P, prev: (N,3) centre and previous centre; r: radius (scalar or (N,)). Returns a boolean (N,): the face makes a
    contact (an inside hit or an edge hit)."""
    N = P.shape[0]
    if F.degenerate:
        return np.zeros(N, bool)
    n, d = F.n, F.d
    pos = P.copy()
    dist = -(pos @ n + d)
    sprev = prev @ n + d
    behind = dist > 0.0
    skip = behind & (-sprev > 0.0)  # prev also behind: back face, skipped
    act = behind & ~skip
    if act.any():
        k = 0
        todo = act.copy()
        while todo.any():
            pos[todo] = 0.9 * pos[todo] + 0.1 * prev[todo]
            k += 1
            dist[todo] = -(pos[todo] @ n + d)
            if k == 20 and CAP20:
                pos[todo] = prev[todo]
                dist[todo] = -(pos[todo] @ n + d)
                break
            if k >= 60:
                break
            todo = todo & (dist > 0.0)
    ok = ~skip & ~(r < -dist)
    q = pos + dist[:, None] * n[None, :]
    a0, a1 = F.axis
    u0 = q[:, a0] - F.v[0][a0]
    v0 = q[:, a1] - F.v[0][a1]
    p = F.proj
    alpha = np.full(N, -1.0)
    if p[0] == 0.0:
        beta = u0 / p[1]
        inb = (beta >= -TOL) & (beta <= 1.0 + TOL)
        alpha = np.where(inb, (v0 - beta * p[3]) / p[2], -1.0)
    else:
        beta = (v0 * p[0] - u0 * p[2]) / (p[3] * p[0] - p[1] * p[2])
        inb = (beta >= -TOL) & (beta <= 1.0 + TOL)
        alpha = np.where(inb, (u0 - beta * p[1]) / p[0], -1.0)
    inside = ~((alpha < -TOL) | (alpha + beta > 1.0 + TOL))
    hit_face = ok & inside
    # edges (only where the inside test failed)
    need = ok & ~inside
    hit_edge = np.zeros(N, bool)
    if need.any():
        for j in range(3):
            if F.elen[j] == -1.0:
                continue
            t = pos @ F.edir[j] - F.v[j] @ F.edir[j]
            rng = (t >= 0.0) & (t <= F.elen[j])
            e = F.v[j][None, :] + t[:, None] * F.edir[j][None, :]
            dd = pos - e
            near = (dd * dd).sum(1) < r * r
            hit_edge |= need & rng & near
    return hit_face | hit_edge


def any_catch(P, prev, r, faces):
    hit = np.zeros(P.shape[0], bool)
    for F in faces:
        hit |= face_catches(P, prev, r, F)
    return hit


def covered(X, Z, ytop, faces):
    """Columns (X, Z) that lie over an up-facing face, by AltitudeCheckPlane's own test (collision.c 1338-1370): the
    point on the plane straight below (X, ytop, Z) is inside the triangle (tolerance 0.001). Returns a boolean array."""
    N = len(X)
    out = np.zeros(N, bool)
    for F in faces:
        if F.degenerate or F.n[1] <= 0.0:
            continue
        pos = np.stack([X, np.full(N, ytop), Z], 1)
        t = (pos @ F.n + F.d) / F.n[1]
        ok = t > 0.0
        q = pos.copy()
        q[:, 1] = ytop - t
        a0, a1 = F.axis
        u0 = q[:, a0] - F.v[0][a0]
        v0 = q[:, a1] - F.v[0][a1]
        p = F.proj
        if p[0] == 0.0:
            alpha = u0 / p[1]
            inb = (alpha >= -0.001) & (alpha <= 1.001)
            beta = np.where(inb, (v0 - alpha * p[3]) / p[2], -1.0)
        else:
            alpha = (v0 * p[0] - u0 * p[2]) / (p[3] * p[0] - p[1] * p[2])
            inb = (alpha >= -0.001) & (alpha <= 1.001)
            beta = np.where(inb, (u0 - alpha * p[1]) / p[0], -1.0)
        out |= ok & ~((beta < -0.001) | (beta + alpha > 1.001))
    return out


def crossing(cur, prev, faces, min_ny=0.25):
    """Boolean (N,): the vertical segment prev -> cur passes through the plane of a face with n.y >= MIN_NY at a point
    inside the face's own triangle (the column is over the face by AltitudeCheckPlane's test)."""
    N = cur.shape[0]
    out = np.zeros(N, bool)
    X, Z = cur[:, 0], cur[:, 2]
    for F in faces:
        if F.degenerate or F.n[1] < min_ny:
            continue
        s_cur = cur @ F.n + F.d
        s_prev = prev @ F.n + F.d
        cr = (s_prev >= 0.0) & (s_cur < 0.0)
        if not cr.any():
            continue
        yp = -(F.n[0] * X + F.n[2] * Z + F.d) / F.n[1]
        q = np.stack([X, yp, Z], 1)
        a0, a1 = F.axis
        u0 = q[:, a0] - F.v[0][a0]
        v0 = q[:, a1] - F.v[0][a1]
        p = F.proj
        if p[0] == 0.0:
            alpha = u0 / p[1]
            inb = (alpha >= -0.001) & (alpha <= 1.001)
            beta = np.where(inb, (v0 - alpha * p[3]) / p[2], -1.0)
        else:
            alpha = (v0 * p[0] - u0 * p[2]) / (p[3] * p[0] - p[1] * p[2])
            inb = (alpha >= -0.001) & (alpha <= 1.001)
            beta = np.where(inb, (u0 - alpha * p[1]) / p[0], -1.0)
        out |= cr & ~((beta < -0.001) | (beta + alpha > 1.001))
    return out
