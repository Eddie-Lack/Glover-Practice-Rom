"""Reference model of trainer (d), the Moon Pound indicator: what the practice code would compute each game frame.

Input: the glove's state at the start of a game pass (position, velocity, sphere radius; the snapshot of the harness)
and the level's collision faces. Output: what a slam started with Z on the NEXT controller read would do: where the
fall column is, which pass the glove crosses a floor, whether any face makes a contact (LAND) or none does (CLIP), and
the nearest dead cell (a place where the sphere overlaps the mesh but the game's test finds no contact) with the
direction and the distance to it.

The slam physics (hand.c, actor.c), measured in exp_slam and exp_drift:
  pass 0   the pass of the snapshot, no Z yet (a jump pass): moves by vel; vel.y -= 1.2; vel.xz *= 0.665
  pass 1   the Z pass: same; ApplyAction(HAND_SLAM) at its end (actionTime 0, actionLock 4, target radius 16)
  pass 2   moves by vel; MaintainAction (slam, actionTime 1): vel.y = 0; vel.xz *= 0.3325
  pass 3   no vertical move; vel.xz *= 0.3325
  pass 4   no vertical move; actionTime 3: vel.y = -14
  pass 5.. falls 14 a pass
  horizontal: after a pass that moved the glove less than 0.1 (minSpeed, collision.c CheckCollisions) vel.xz is zeroed.
  radius: from pass 2 on r += (16 - r) / 4 each pass (actor.c UpdateActor, radiusRate 4); 15.051 in pass 5.
"""
import math
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import moonpound as model

M_JUMP = 0.665      # 0.95 * 0.7 per jump pass without stick input (exp_drift)
M_SLAM = 0.3325     # 0.95 * 0.7 * 0.5 per slam pass
MIN_SPEED = 0.1
GRAVITY = 1.2
FALL = 14.0
SLAM_TARGET = 16.0


def passes_positions(pos, vel, r0=13.0, npass=12):
    """Centre positions P_0..P_npass and the radius of each pass, for a slam started with Z on the next read."""
    P = [tuple(pos)]
    vx, vy, vz = vel
    radii = []
    r = r0
    for k in range(npass):
        if k >= 2:
            r += (SLAM_TARGET - r) / 4.0
        radii.append(r)
        x, y, z = P[-1]
        if k <= 2:
            dy = vy
            vy -= GRAVITY if k < 2 else 0.0
        elif k in (3, 4):
            dy = 0.0
        else:
            dy = -FALL
        if k == 2:
            vy = 0.0
        P.append((x + vx, y + dy, z + vz))
        d = math.hypot(vx, vz)
        m = M_JUMP if k < 2 else M_SLAM
        if d < MIN_SPEED:
            vx = vz = 0.0
        else:
            vx *= m
            vz *= m
    return P, radii


def simulate(pos, vel, faces, r0=13.0, npass=14, min_ny=0.25):
    """Run the passes against FACES. Returns dict(verdict, column, passes). verdict: 'LAND' (a contact before any floor
    crossing), 'CLIP' (the centre crossed a floor face with no contact), 'VOID' (nothing below)."""
    P, radii = passes_positions(pos, vel, r0, npass)
    out = []
    verdict = "VOID"
    # ConditionsMet(HAND_SLAM) needs vel.y > -9 after the Z pass's gravity (hand.c 395): vy_S0 - 2 * 1.2
    if vel[1] - 2 * GRAVITY <= -9.0:
        return dict(verdict="NO_Z", column=(P[5][0], P[5][2]), passes=out, P=P)
    for k in range(npass):
        prev, cur = P[k], P[k + 1]
        contacts, fin = model.check_collisions(cur, prev, radii[k], faces)
        crossed = None
        for f in faces:
            if f.n[1] < min_ny or any(x != x for x in f.n):
                continue
            s_cur = model.dot(f.n, cur) + f.d
            s_prev = model.dot(f.n, prev) + f.d
            if s_prev >= 0.0 > s_cur:
                if inside_footprint(f, cur[0], cur[2]):
                    crossed = f
                    break
        out.append(dict(k=k, prev=prev, cur=cur, r=radii[k], contacts=[(c[0].src, c[0].idx, c[1][0]) for c in contacts],
                        crossed=(crossed.src, crossed.idx) if crossed else None, fin=fin))
        if contacts:
            verdict = "LAND"
            if crossed is not None and False:
                verdict = "CLIP"
            break
        if crossed is not None:
            verdict = "CLIP"
            break
    return dict(verdict=verdict, column=(P[5][0], P[5][2]), passes=out, P=P)


def inside_footprint(f, x, z):
    """AltitudeCheckPlane's inside test of the point on f's plane straight below (x, z)."""
    if f.n[1] <= 0.0:
        return False
    yp = -(f.n[0] * x + f.n[2] * z + f.d) / f.n[1]
    q = (x, yp, z)
    a0, a1 = f.axis
    u0 = q[a0] - f.v[0][a0]
    v0 = q[a1] - f.v[0][a1]
    p = f.proj
    if p[0] == 0.0:
        alpha = u0 / p[1]
        if not (-0.001 <= alpha <= 1.001):
            return False
        beta = (v0 - alpha * p[3]) / p[2]
    else:
        alpha = (v0 * p[0] - u0 * p[2]) / (p[3] * p[0] - p[1] * p[2])
        if not (-0.001 <= alpha <= 1.001):
            return False
        beta = (u0 - alpha * p[1]) / p[0]
    return not (beta < -0.001 or beta + alpha > 1.001)


def guide(pos, vel, faces, cands, r0=13.0, reach=40.0):
    """What the trainer shows each frame while the glove jumps: for 'Z on the next read' the verdict, the fall column, and
    the nearest dead-zone vertex of CANDS (cone.candidates) with the direction and distance from the column.
    Returns dict(verdict, column, target, to_target (dx, dz), dist, zone_dist) or target None."""
    sim = simulate(pos, vel, faces, r0)
    C = sim["column"]
    heights = [p[1] for p in sim["P"]]
    best = None
    for c in cands:
        V = c["v"]
        if V[1] > pos[1]:
            continue
        # height of the centre above V at the last fall pass that is still above V (the phase of the slam against this vertex)
        above = [y - V[1] for y in heights[5:] if y > V[1]]
        if not above:
            continue
        d = min(above)
        T = (V[0] + d * c["centre"][0], V[2] + d * c["centre"][1])
        dist = math.hypot(T[0] - C[0], T[1] - C[1])
        if dist < reach and (best is None or dist < best[0]):
            best = (dist, V, T, d)
    out = dict(verdict=sim["verdict"], column=C, target=None)
    if best:
        dist, V, T, d = best
        out.update(target=V, centre=T, to_target=(T[0] - C[0], T[1] - C[1]), dist=dist, phase_height=d)
        # distance along the line to the first column that clips (0 when already clipping), step 0.02
        if sim["verdict"] == "CLIP":
            out["zone_dist"] = 0.0
        else:
            ux, uz = (T[0] - C[0]) / max(dist, 1e-9), (T[1] - C[1]) / max(dist, 1e-9)
            zd = None
            k = 0
            while k * 0.02 <= dist + 0.5 and k < 200:
                col = (C[0] + ux * k * 0.02, C[1] + uz * k * 0.02)
                dx, dz = col[0] - C[0], col[1] - C[1]
                p2 = (pos[0] + dx, pos[1], pos[2] + dz)
                if simulate(p2, vel, faces, r0)["verdict"] == "CLIP":
                    zd = k * 0.02
                    break
                k += 1
            out["zone_dist"] = zd
    return out


def advance_jump(pos, vel):
    """One jump pass with no stick input and no contact (the state the next snapshot would show)."""
    x, y, z = pos
    vx, vy, vz = vel
    P = (x + vx, y + vy, z + vz)
    d = math.hypot(vx, vz)
    if d < MIN_SPEED:
        vx = vz = 0.0
    else:
        vx *= M_JUMP
        vz *= M_JUMP
    return P, (vx, vy - GRAVITY, vz)


def strip(pos, vel, faces, n=8, r0=13.0):
    """Verdicts for Z read 1, 2, ... n frames after the snapshot (index k: Z shows in snapshot row r + 1 + k)."""
    out = []
    for k in range(n):
        out.append(simulate(pos, vel, faces, r0)["verdict"])
        pos, vel = advance_jump(pos, vel)
    return out
