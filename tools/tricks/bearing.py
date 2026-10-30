"""Standalone model of MAZ-R into a wall (horizontal plane only, flat floor).

The comparison with the emulator sweeps needs sweep logs that the repository does not hold (README.md).

Each fact cites gloveworld's C (file:line of the version in the repository).

One game frame L_n is, in order (main.c MainGameLoop, 1038-1064):
  S0 = ball.vel                                   (main.c 1038)
  UpdateActor(hand)                               (actor.c 381-518)
  UpdateActor(ball); ball.prevVel = S0            (main.c 1043-1044)
  ProcessInput()                                  (controller.c 363-715)
  UpdateActorPositionDependencies()               (actor.c 1148-1319)
  MoveBall(); MoveHand() -> MaintainAction()      (hand.c 1309-2760)
"""
import math
import os

# ActorPhysics of the ball types (the values of gloveworld's defaultBehaviour table)
BALLS = {
    "normal":  dict(r=9.0,  mass=10.0, accel=0.5,     maxSpeed=26.0, friction=0.94, bounce=0.8),
    "bowling": dict(r=14.0, mass=14.0, accel=0.26455, maxSpeed=26.0, friction=0.97, bounce=-1.0),
    "power":   dict(r=6.0,  mass=7.0,  accel=0.75,    maxSpeed=26.0, friction=0.90, bounce=0.99),
    "bearing": dict(r=6.0,  mass=10.0, accel=0.5,     maxSpeed=36.4, friction=0.95, bounce=0.6),
    "beach":   dict(r=25.0, mass=3.0,  accel=0.5,     maxSpeed=26.0, friction=0.70, bounce=0.7),
    "crystal": dict(r=9.0,  mass=9.0,  accel=0.5,     maxSpeed=36.4, friction=0.94, bounce=0.6),
}
HAND_MAXSPEED = 14.0          # hand.phys->maxSpeed (measured, atl1 boot print)
HAND_AIR_FRICTION = 1 - (1 - 0.8) / 4   # actor.c 715 with phys->friction 0.8: 0.95 (hand.obj->planes[0] == NULL)
HAND_GROUND_FRICTION = 0.6              # actor.c 719-722 with hand.friction = handInertia 0.6 (hand.obj->planes[0] set)
HAND_ACCEL, HAND_MASS = 5.0, 10.0
JOINED_CAP = 7.0              # BallJoinedMaxSpeed (ball.c 641-657), hand.c 1473
AFTER_ACTION_CAP = 13.0       # ApplyAction sets ball.phys->moveSpeed = 13 (hand.c 673)
SPIN_OFFSET_COS = math.cos(1.0)   # MoveBallToHand (ball.c 892): RotateVector2DXYZ by 1 - hand.rot.x, hand.rot.x = 0


def vadd(a, b): return [a[0] + b[0], a[1] + b[1]]
def vsub(a, b): return [a[0] - b[0], a[1] - b[1]]
def vmul(a, k): return [a[0] * k, a[1] * k]
def vdot(a, b): return a[0] * b[0] + a[1] * b[1]
def vlen(a): return math.hypot(a[0], a[1])


class Wall:
    """An infinite wall: the plane n.(p - p0) = 0 with the unit normal n pointing to the side that is solid to the glove
    (the front). sd(p) > 0 in front."""

    def __init__(self, n, p0):
        l = vlen(n)
        self.n = [n[0] / l, n[1] / l]
        self.p0 = p0

    def sd(self, p):
        return vdot(self.n, vsub(p, self.p0))


def collide_ball(wall, r, prev, pos, vel, bounce_coef, on_ground=True, allow_bounce=True, shatter_speed=None):
    """collision.c CollideActorWithPlane (796-1040) + CheckCollisions's bounce part (633-660) for one sphere and one
    plane that is large enough. Returns (pos, vel, touched).
    - The plane is skipped when the sphere centre is behind it and prevPos is behind it too (854-871).
    - When the centre is behind and prevPos in front, pos is moved towards prevPos in steps of 10% (858-867)."""
    p = list(pos)
    dist = -wall.sd(p)                       # > 0: centre behind the plane
    if dist > 0.0:
        if -wall.sd(prev) <= 0.0:            # prevPos in front (or on the plane)
            k = 0
            while dist > 0.0:
                p = vadd(vmul(p, 0.9), vmul(prev, 0.1))
                k += 1
                dist = -wall.sd(p)
                if k == 20:
                    p = list(prev)
                    dist = -wall.sd(p)
                    break
        else:
            return pos, vel, False           # seen from behind: no collision
    if r < -dist:                            # farther than the radius in front
        return pos, vel, False
    shatter = None
    if shatter_speed is not None:
        mv = abs(vdot(vsub(pos, prev), wall.n))
        ln_ = max(mv, abs(vdot(vel, wall.n)))
        sp3 = vlen(vel)
        if ln_ > shatter_speed or (shatter_speed < sp3 and ln_ > shatter_speed / 1.5):
            shatter = True
    q = vadd(p, vmul(wall.n, dist))          # the point on the plane
    newpos = vadd(q, vmul(wall.n, r))        # pushed out to the radius (951)
    speed = vlen(vel)
    # bounce decision (963-985) with vy ~ -1.2 left out: len = dir.n
    bounce = allow_bounce       # measured: a wall touched in the previous frame gives no bounce (cf. 970-978)
    if speed > 0 and bounce:
        ln = vdot(vel, wall.n) / speed
        if ln > -0.3 or (ln > -0.8 and (bounce_coef <= 0.0 or on_ground)):
            bounce = False
    if bounce and bounce_coef >= 0.0:
        d = vlen(vel)
        refl = vsub(vel, vmul(wall.n, 2 * vdot(vel, wall.n)))
        newvel = vmul(refl, bounce_coef)     # unit reflection * speed * bounce (637)
    else:
        newvel = vsub(newpos, prev)          # 641: vel = pos - prevPos
        sp = vlen(newvel)
        if sp != 0 and sp > vlen(vel):
            newvel = vmul(newvel, vlen(vel) / sp)
    if shatter:
        return newpos, newvel, "shatter"
    return newpos, newvel, True


class Sim:
    """The ball held in the glove (JOINED) walking at a wall, then A x4, Z (cancel), R (SPIN).

    pad: unit vector of the stick/D-pad direction in world space times magnitude: (dir, mag). The glove's facing follows
    the pad direction (controller.c 517)."""

    def __init__(self, ball="bearing", wall=None, b=(0.0, 0.0), face=(1.0, 0.0), pad_mag=79.0, vb=(0.0, 0.0), hand_fric=HAND_AIR_FRICTION):
        self.p = BALLS[ball]
        self.ball_name = ball
        self.shattered = False
        self.hand_fric = hand_fric
        self.wall = wall
        self.b = list(b)
        self.vb = list(vb)
        self.f = list(face)               # hand.rot.y as a unit vector (sin, cos)
        self.pad_mag = pad_mag
        self.pad_dir = list(face)
        self.mode = "JOINED"
        self.prev_mode = "WALK"
        self.h = vadd(self.b, vmul(self.f, 9.0 - self.p["r"]))
        self.vh = [0.0, 0.0]
        self.hand_collide = False         # hand.flags & 0x18 (JOINED, PREWHACK: off)
        self.move_speed = JOINED_CAP
        self.lock = 0
        self.a_hold = 0
        self.log = []
        self.frame = 0
        self.spin_frames = 0
        self.prev_touched = False

    # -- helpers -------------------------------------------------------------------------------------------------
    def best_pos(self):
        """GetHandPosAim (ball.c 755-785): the hand's place for the ball: (9 - r) ahead of the ball (r < 9; clamp -5)."""
        off = max(self.p["r"] - 9.0, -5.0)
        return vsub(self.b, vmul(self.f, off))

    def move_hand_to_ball(self, snap=True):
        """MoveHandToBall (ball.c 713-753) with the pad held: scale 1 -> the hand is put at bestPos."""
        if snap or self.pad_mag != 0:
            self.h = self.best_pos()
        self.vh_y = 0.0

    def move_ball_to_hand(self):
        """MoveBallToHand (ball.c 796-905) on the first SPIN frame (spinPos = 0): the ball is put (9 - r) * cos(1) behind
        the hand along the facing (for r < 9)."""
        off = (9.0 - self.p["r"]) * SPIN_OFFSET_COS
        self.b = vsub(self.h, vmul(self.f, off))
        self.vb = list(self.vh)

    # -- one game frame --------------------------------------------------------------------------------------------
    def step(self, A=False, Z=False, R=False):
        P = self.p
        S0 = list(self.vb)
        # UpdateActor(hand)
        sp = vlen(self.vh)
        if sp > HAND_MAXSPEED:
            self.vh = vmul(self.vh, HAND_MAXSPEED / sp)
        hprev = list(self.h)
        self.h = vadd(self.h, self.vh)
        self.vh = vmul(self.vh, self.hand_fric)
        if self.hand_collide and self.wall is not None:
            # the hand's sphere (radius 13..19) against the wall; only used to show a non-clip
            self.h, _, _ = collide_ball(self.wall, 13.0, hprev, self.h, self.vh, 0.0)
        # UpdateActor(ball)
        sp = vlen(self.vb)
        if sp > P["maxSpeed"]:
            self.vb = vmul(self.vb, P["maxSpeed"] / sp)
        bprev = list(self.b)
        self.b = vadd(self.b, self.vb)
        fr = P["friction"] if self.pad_mag else max(0.0, 1 - (1 - P["friction"]) * 4)
        self.vb = vmul(self.vb, fr)
        touched = False
        if self.wall is not None:
            self.b, v2, touched = collide_ball(self.wall, P["r"], bprev, self.b, self.vb, P["bounce"],
                                               allow_bounce=not self.prev_touched,
                                               shatter_speed=10.0 if self.ball_name == "crystal" else None)
            if touched == "shatter":
                self.shattered = True
                touched = True
            if touched:
                self.vb = v2
            self.prev_touched = touched
        # ProcessInput
        if self.mode == "JOINED":
            k = HAND_ACCEL / 0.5 * P["accel"] * 10.0 / P["mass"] * 0.32
            push = k * self.pad_mag / 120.0
            old = vlen(self.vb)
            if old <= 2.0:
                push *= 1.25
            self.vb = vadd(self.vb, vmul(self.pad_dir, push))
            sp = vlen(self.vb)
            cap = self.move_speed
            if sp > cap:
                self.vb = vmul(self.vb, cap / sp) if old <= cap else vmul(self.vb, 0.9)
        # UpdateActorPositionDependencies
        if self.mode == "JOINED":
            self.move_hand_to_ball()
        elif self.mode == "SPIN":
            self.move_ball_to_hand()
        # MaintainAction
        prev_mode = self.mode
        cancelled = False
        if self.mode == "PREWHACK" and Z:
            # ApplyAction(HAND_JOINED) from PREWHACK (hand.c 763-788): ball.vel = prevVel; hand.vel = ball.vel
            self.mode = "JOINED"
            self.vb = list(S0)
            self.move_hand_to_ball()
            self.vh = list(self.vb)
            self.lock = 4
            self.move_speed = AFTER_ACTION_CAP
            cancelled = True
        elif self.mode == "JOINED" and R:
            # hand.c 1464-1468: R held -> transformState = 30; ApplyAction(HAND_SPIN)
            self.mode = "SPIN"
            self.move_speed = AFTER_ACTION_CAP
        if prev_mode == "SPIN" and self.spin_frames == 0:
            self.vh = vmul(self.vh, 0.7)      # hand.c 2897-2899: the hand is not on the ground (its collision flags are off)
        if prev_mode == "SPIN":
            self.spin_frames += 1
        if prev_mode == "JOINED" and not cancelled:
            self.move_speed = JOINED_CAP if prev_mode == "JOINED" and self.mode == "JOINED" else self.move_speed
        # the hand's collision flags follow the mode at the START of MaintainAction (hand.c 1416-1425)
        self.hand_collide = prev_mode not in ("JOINED", "PREWHACK", "BOUNCE")
        # the action table: A held for 4 reads -> PREWHACK (hand.c 331-335); A latched after the cancel
        if self.mode == "JOINED" and not cancelled:
            if A:
                self.a_hold += 1
            else:
                self.a_hold = 0
            if self.a_hold >= 4 and self.lock == 0:
                self.mode = "PREWHACK"
        if self.lock > 0:
            self.lock -= 1
        self.frame += 1
        row = dict(mode=self.mode, b=list(self.b), h=list(self.h), vb=list(self.vb), vh=list(self.vh), touched=touched)
        if self.wall is not None:
            row["sdb"] = self.wall.sd(self.b)
            row["sdh"] = self.wall.sd(self.h)
        self.log.append(row)
        return row


def run_sweep1_trial(s, ball="bearing", D0=150.0, wall_x=600.0, extra=14, hand_fric=HAND_AIR_FRICTION):
    """Same schedule as exp_sweep1.py: s frames of UP, A x4, A+Z, A+Z+R, then 14 more. Ball starts at rest D0 from the
    wall plane x = wall_x (normal -x)."""
    w = Wall((-1.0, 0.0), (wall_x, 0.0))
    sim = Sim(ball, w, b=(wall_x - D0, 0.0), face=(1.0, 0.0), hand_fric=hand_fric)
    sched = [(0,)] * s + [("A",)] * 4 + [("A", "Z")] + [("A", "Z", "R")] * (1 + extra)
    for btn in sched:
        sim.step(A="A" in btn and sim.a_hold >= 0 and True, Z="Z" in btn, R="R" in btn)
    return sim



def prewhack_sim(ball, wall, b, vb, face=(1.0, 0.0), pad_mag=79.0, hand_fric=HAND_AIR_FRICTION):
    """The state just before the cancel frame: PREWHACK, hand at the ball's place, hand.vel = 0 (hand.c 1676-1690)."""
    sim = Sim(ball, wall, b=b, face=face, pad_mag=pad_mag, vb=vb, hand_fric=hand_fric)
    sim.mode = "PREWHACK"
    sim.h = sim.best_pos()
    sim.hand_collide = False
    return sim


def maz_r(ball, wall, b, vb, k=1, face=(1.0, 0.0), pad_mag=79.0, extra=4, hand_fric=HAND_AIR_FRICTION):
    """Z on the first frame (the cancel), R read by the k-th frame after it. Returns the Sim; log[0] is after the cancel
    frame, log[k] after the frame that reads R, log[k+1] after the free-move frame."""
    sim = prewhack_sim(ball, wall, b, vb, face, pad_mag, hand_fric)
    sim.step(A=True, Z=True)
    for j in range(1, k):
        sim.step(A=True, Z=True)
    sim.step(A=True, Z=True, R=True)
    for _ in range(extra):
        sim.step(A=True, Z=True, R=True)
    return sim


def clip_check(ball, d, S, k=1, theta=0.0, face=(1.0, 0.0), pad_mag=79.0, wall_x=600.0, hand_fric=HAND_AIR_FRICTION):
    """The model's verdict for a trial of sweep 2: distance D of the ball centre to the plane at the start of the cancel
    frame, horizontal speed S towards the wall (along +x), wall turned by THETA from head-on."""
    n = (-math.cos(theta), math.sin(theta))
    wall = Wall(n, (wall_x, 700.0))
    xb = wall_x - d / math.cos(theta)
    sim = maz_r(ball, wall, (xb, 700.0), (S, 0.0), k, face, pad_mag, hand_fric=hand_fric)
    free = sim.log[k + 1]            # after the free-move frame
    return dict(sdh=free["sdh"], sdb=free["sdb"], clip=free["sdh"] < 0.0 and not sim.shattered, shattered=sim.shattered, sim=sim)


def _load(name):
    import json, os
    return json.load(open(os.path.join(os.path.dirname(os.path.abspath(__file__)), "out", name)))


def compare_sweep1():
    """Natural approach (UP held, speed 7), A x4, Z, R: the positions up to the free-move frame and the clip decision."""
    res = _load("sweep1.json")
    worst_pos, agree = 0.0, 0
    for r in res:
        sim = run_sweep1_trial(r["s"])
        err = 0.0
        for i in range(0, min(len(r["rows"]) - 1, len(sim.log))):
            er, mr = r["rows"][i + 1], sim.log[i]     # emulator row i+1 = the state after the logic frame the model logs as i
            if i <= r["ci"] + 1:
                err = max(err, abs(er["bx"] - mr["b"][0]), abs(er["hx"] - mr["h"][0]))
        worst_pos = max(worst_pos, err)
        emu_clip = min(600.0 - row["bx"] for row in r["rows"]) < -0.5
        agree += emu_clip == (min(row["sdb"] for row in sim.log) < -0.5)
    print("sweep 1 (natural approach, bearing, 30 trials): clip decision agrees in %d; largest position error up to the clip frame %.4f units" % (agree, worst_pos))


def compare_sweep2(names, label):
    rows = []
    for n in names:
        rows += _load(n)
    agree, worst, bad = 0, 0.0, []
    for r in rows:
        hf = r.get("hand_fric", HAND_GROUND_FRICTION if r["type"] in ("normal", "bowling") else HAND_AIR_FRICTION)
        k = r.get("k", 1)
        c = clip_check(r["type"], r["d"], r["S"], k, r.get("theta", 0.0), hand_fric=hf)
        th = r.get("theta", 0.0)
        nx, nz = -math.cos(th), math.sin(th)
        emu_sdh = nx * (r["hx"][k + 1] - 600.0) + nz * (r.get("hz", [700.0] * 5)[k + 1] - 700.0)
        emu_shat = 42 in r.get("modes", [])
        emu_clip = emu_sdh < 0 and not emu_shat
        agree += emu_clip == c["clip"]
        if not emu_shat and not c["shattered"]:
            worst = max(worst, abs(emu_sdh - c["sdh"]))
        if emu_clip != c["clip"]:
            bad.append((r["type"], r["S"], r["d"], k))
    print("%s: %d trials; clip decision agrees in %d; largest difference of the hand's distance to the plane after the free-move frame %.4f; disagreements %s" % (label, len(rows), agree, worst, bad))


def compare_sweep3_4_real():
    res = _load("sweep3.json")
    for var, hf in (("maz", HAND_AIR_FRICTION), ("mazG", HAND_GROUND_FRICTION)):
        ok = n = 0
        for r in res:
            if r["variant"] != var:
                continue
            sim = run_sweep1_trial(r["s"], hand_fric=hf)
            n += 1
            ok += (min(row["sdh"] for row in sim.log) < -0.5) == r["clip"]
        print("sweep 3 (natural approach, variant %s): clip decision agrees in %d of %d" % (var, ok, n))
    res = _load("sweep4.json")
    ok = 0
    for r in res:
        ball = "power" if r["variant"] == "power" else "bearing"
        hf = HAND_GROUND_FRICTION if r["variant"] == "ground" else HAND_AIR_FRICTION
        ok += clip_check(ball, r["d"], r["S"], 1, 0.0, hand_fric=hf)["clip"] == r["clip"]
    print("sweep 4 (power ball, ball in the air, hand with planes[0] set): clip decision agrees in %d of %d" % (ok, len(res)))
    res = _load("real2.json")
    ok = sum(clip_check("bearing", r["d"], r["S"], 1, 0.0, wall_x=80.0)["clip"] == r["clip"] for r in res["inject"])
    ok2 = sum((min(row["sdh"] for row in run_sweep1_trial(r["s"], wall_x=80.0).log) < -0.5) == r["clip"] for r in res["natural"])
    print("real wall (ACOURSE x = 80): injection %d of %d, natural approach %d of %d" % (ok, len(res["inject"]), ok2, len(res["natural"])))


def compare_sweep6():
    """The stick (and so the glove's facing) turned away from the ball's speed, which points straight at the wall."""
    res = _load("sweep6.json")
    ok, worst = 0, 0.0
    for r in res:
        off = math.radians(r["off"])
        face = (math.cos(off), math.sin(off))
        sim = maz_r("bearing", Wall((-1.0, 0.0), (600.0, 700.0)), (600.0 - r["d"], 700.0), (r["S"], 0.0), 1, face=face)
        free = sim.log[2]
        ok += (free["sdh"] < 0) == (r["sdh2"] < 0)
        worst = max(worst, abs(free["sdh"] - r["sdh2"]))
    print("sweep 6 (stick turned 0 to 60 degrees from the ball's speed): %d trials; clip decision agrees in %d; largest difference of the glove's distance %.3f" % (len(res), ok, worst))


if __name__ == "__main__":
    if not os.path.isdir(os.path.join(os.path.dirname(os.path.abspath(__file__)), "out")):
        raise SystemExit("bearing.py: the sweep logs are not in the repository (README.md).")
    compare_sweep1()
    compare_sweep2(["sweep2_A1.json", "sweep2_A2.json"], "sweep 2 (head-on, bearing, S x d x R delay)")
    compare_sweep2(["sweep2_ANG.json"], "sweep 2 (wall turned 15 to 80 degrees)")
    compare_sweep2(["sweep2_TYPES.json"], "sweep 2 (rubber, bowling and crystal balls)")
    compare_sweep3_4_real()
    compare_sweep6()
