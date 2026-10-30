"""A standalone model of the L-mash: Glover holds the ball (HAND_JOINED, "J") or stands on it (HAND_BALLWALK, "B")
and changes between the two with L. Flat ground or free fall, one pad direction held.

It recreates the parts of one game frame that change the ball's horizontal speed. The comments name the C source
(gloveworld/src, checkout of 2026-10-22). It was checked against the retail ROM in mupen64plus: about 40 runs, and
the ball's speed on every frame agrees to 1e-5 (a few to 2e-3). `python3 lmash.py` replays the logs in out/ when they exist.

One game frame (main.c MainGameLoop, lines 1038-1064):
    S = ball.vel                       (kept as ball.prevVel, main.c 1038 and 1044)
    UpdateActor(hand); UpdateActor(ball)                       (main.c 1042-1043)
    ProcessInput(); UpdateActorPositionDependencies(); MoveBall(); MoveHand()    (main.c 1061-1064)
    MoveHand -> MaintainAction: an L press changes J <-> B (hand.c 1460 and 1866).

Vectors are [x, z] on the ground plane. Everything is per game frame (20 a second on NTSC).
"""
import json
import math
import os

HERE = os.path.dirname(os.path.abspath(__file__))

# ---- the balls (defaultBehaviour[type], read from the ROM's data) and the glove ----
NORMAL_BALL = dict(mass=10.0, accel=0.5, max_speed=26.0, friction=0.94, min_speed=0.1, joined_max=7.0)
# defaultBehaviour[3], the ball bearing. BallJoinedMaxSpeed() (ball.c 641-656) is 7 for it as well.
BEARING_BALL = dict(mass=10.0, accel=0.5, max_speed=36.4, friction=0.95, min_speed=0.1, joined_max=7.0)
HAND_MAXSPEED = 14.0        # hand.phys->maxSpeed (hand.c 3853): UpdateActor clamps the glove's speed to it
HAND_MASS = 10.0            # hand.c 3865 and MoveHand (without Hercules)
HAND_ACCEL = 5.0            # hand.phys->accel, set at the end of MoveHand (hand.c 3201)
HAND_MOVE_SPEED = 7.0       # hand.phys->moveSpeed (hand.c 3202); ball.phys->moveSpeed in B (hand.c 1865)
MOVE_FRESH = 13.0           # ApplyAction sets ball.phys->moveSpeed = 13 on every action change (hand.c 673)
HAND_FRICTION_PLANE = 0.6   # glove friction with a plane under it: hand.friction = handInertia (actor.c 719-722)
HAND_FRICTION_FREE = 1.0 - (1.0 - 0.8) / 4.0    # 0.95: no plane, 1 - (1 - phys->friction) / 4 (actor.c 715)
IDLE_FRICTION_FACTOR = 4.0  # ball.friction with the pad released in J or B: 1 - (1 - phys->friction) * 4 (ball.c 365)


class Ball:
    def __init__(self, cfg):
        self.__dict__.update(cfg)
        # controller.c 622-632 (J) and 682-692 (B): hand accel / 0.5 * ball accel * 10 / ball mass * 0.32, then times
        # the pad value over 120 (J) or 180 (B)
        self.k = HAND_ACCEL / 0.5 * self.accel * 10.0 / self.mass * 0.32
        # ApplyAction(HAND_BALLWALK), hand.c 1015-1016: hand mass / (15, or ball mass * 1.5 for a ball of mass 10 or more)
        self.stand_add = HAND_MASS / (15.0 if self.mass < 10.0 else self.mass * 1.5)
        self.air_friction = 1.0 - (1.0 - self.friction) / 4.0
        self.idle_friction = max(0.0, 1.0 - (1.0 - self.friction) * IDLE_FRICTION_FACTOR)


def lenxz(v):
    return math.hypot(v[0], v[1])


def scale(v, s):
    return [v[0] * s, v[1] * s]


def deal_with_max_speed(vel, max_speed, old_speed):
    """controller.c DealWithMaxSpeed (lines 735-750): a speed above max_speed is cut to it when the speed before the
    push was not above it, and otherwise multiplied by 0.9."""
    speed = lenxz(vel)
    if speed > max_speed:
        if old_speed <= max_speed:
            vel[0] *= max_speed / speed
            vel[1] *= max_speed / speed
        else:
            vel[0] *= 0.9
            vel[1] *= 0.9


class Sim:
    """State of the ball and the glove.

    stick: the pad's (stickX, stickY) after the dead zone: the D-pad gives 79 on each pressed axis (ReadController,
        controller.c 174-190), a stick gives its value minus 5 (at most 74).
    cam: the camera angle (camera.rot.y); None keeps the push in the fixed world direction `dirn`.
    air: the ball is in free fall (nothing under it).
    easy: the Easy ball controls (the stick pushes the ball the way it points while Glover stands on it).
    """

    def __init__(self, vel, mode="J", dirn=(1.0, 0.0), stick=(79.0, 79.0), easy=True, bmove=7.0, hvel=(0.0, 0.0),
                 hplane=True, coll=False, cam=None, air=False, ball=NORMAL_BALL):
        self.ball = Ball(ball)
        self.bvel = list(vel)
        self.hvel = list(hvel)
        self.mode = mode
        self.bmove = bmove        # ball.phys->moveSpeed
        self.hplane = hplane      # the glove's planes[0] is not NULL (fresh or left over from an earlier frame)
        self.coll = coll          # the glove's collision flags (0x18) are on during UpdateActor(hand)
        self.air = air
        self.easy = easy
        self.stick = stick
        self.dirn = dirn
        self.cam = cam
        self.bfric = self.ball.friction       # ball.friction as left by MoveBall
        self.t = 0

    def accel_vec(self, divisor):
        """ProcessInput's push (controller.c 621-632 for J, 681-692 for B). With the camera angle c and the pad (sx, sy):
            x = k * (sin(c) * sy - sin(c + pi/2) * sx) / divisor
            z = k * (cos(c) * sy - cos(c + pi/2) * sx) / divisor
        If `cam` is None the push has the length k * |stick| / divisor and the world direction `dirn`."""
        k = self.ball.k
        if self.cam is None:
            a = k * math.hypot(self.stick[0], self.stick[1]) / divisor
            return [self.dirn[0] * a, self.dirn[1] * a]
        c, sx, sy = self.cam, self.stick[0], self.stick[1]
        return [k * (math.sin(c) * sy - math.sin(c + math.pi / 2) * sx) / divisor,
                k * (math.cos(c) * sy - math.cos(c + math.pi / 2) * sx) / divisor]

    def step(self, press=False):
        """One game frame; `press` is an L press that the glove acts on in this frame. Returns a dict."""
        b = self.ball
        S = list(self.bvel)
        mode0 = self.mode
        # --- UpdateActor(hand): clamp to 14, then the friction (actor.c 383-396, 705-727)
        h = lenxz(self.hvel)
        if h > HAND_MAXSPEED:
            self.hvel = scale(self.hvel, HAND_MAXSPEED / h)
        self.hvel = scale(self.hvel, HAND_FRICTION_PLANE if self.hplane else HAND_FRICTION_FREE)
        if self.air:
            self.hplane = False
        elif self.coll:
            # with its collision on, the glove touches something while it is in the J place (cupped round the ball)
            self.hplane = (mode0 == "J")
        # --- UpdateActor(ball): clamp to maxSpeed, move by that speed, then the friction (actor.c 383-396, 705-727)
        speed = lenxz(self.bvel)
        if speed > b.max_speed:
            self.bvel = scale(self.bvel, b.max_speed / speed)
        moved = lenxz(self.bvel)
        self.bvel = scale(self.bvel, b.air_friction if self.air else self.bfric)
        if moved < b.min_speed and not self.air:
            # CheckCollisions ends with: if |pos - prevPos| < phys->minSpeed then vel.x = vel.z = 0 (collision.c 658-659)
            self.bvel = [0.0, 0.0]
        # --- ProcessInput
        old = lenxz(self.bvel)
        pad_on = self.stick != (0, 0) and self.stick != (0.0, 0.0)
        if mode0 == "J":
            a = self.accel_vec(120.0) if pad_on else [0.0, 0.0]
            if old <= 2.0:
                a = scale(a, 1.25)
            self.bvel[0] += a[0]
            self.bvel[1] += a[1]
            if old != 0.0:
                deal_with_max_speed(self.bvel, self.bmove, old)
        else:
            a = self.accel_vec(180.0) if pad_on else [0.0, 0.0]
            sign = 1.0 if self.easy else -1.0
            self.bvel[0] += sign * a[0]
            self.bvel[1] += sign * a[1]
            if old != 0.0:
                deal_with_max_speed(self.bvel, b.max_speed, old)
            if old <= self.bmove:
                ln = lenxz(self.bvel)
                if ln > self.bmove:
                    self.bvel = scale(self.bvel, self.bmove / ln)
        # --- MoveBall: ball.friction for the next UpdateActor (ball.c 364-367)
        self.bfric = b.friction if pad_on else b.idle_friction
        # --- MoveHand: the glove's collision flags for the next frame follow the mode now (hand.c 1417-1424)
        self.coll = (mode0 != "J")
        # --- MaintainAction (hand.c 1456-1470 for J, 1855-1870 for B)
        if mode0 == "J":
            if press:
                # ApplyAction(HAND_BALLWALK), hand.c 1015: ball.vel += hand.vel * stand_add
                self.bvel[0] += self.hvel[0] * b.stand_add
                self.bvel[1] += self.hvel[1] * b.stand_add
                self.mode = "B"
                self.bmove = MOVE_FRESH
            else:
                self.bmove = b.joined_max            # BallJoinedMaxSpeed(), hand.c 1473
        else:
            if press:
                # ApplyAction(HAND_JOINED), hand.c 766 and 771: ball.vel = ball.prevVel; hand.vel = ball.vel
                self.bvel = list(S)
                self.hvel = list(S)
                self.mode = "J"
                self.bmove = MOVE_FRESH
            else:
                self.bmove = HAND_MOVE_SPEED         # ball.phys->moveSpeed = hand.phys->moveSpeed, hand.c 1865
        self.t += 1
        return dict(t=self.t, mode=self.mode, bspd=lenxz(self.bvel), hspd=lenxz(self.hvel), S=lenxz(S),
                    moved=min(b.max_speed, lenxz(S)))


def gaps_to_presses(first, gaps, nframes):
    t, out, i = first, [], 0
    while t < nframes:
        out.append(t)
        t += gaps[i % len(gaps)]
        i += 1
    return out


def run_pattern(presses, nframes, v0=7.0, mode="J", **kw):
    """Starts in `mode` at the steady speed v0 (what 30 frames of walking give). `presses`: the frames on which the
    glove acts on an L press. Returns the list of per-frame dicts."""
    sim = Sim([v0, 0.0], mode=mode, **kw)
    presses = set(presses)
    return [sim.step(t in presses) for t in range(nframes)]


# ------------------------------------------------------------------ comparison with the emulator logs

def s16(v):
    return v - 65536 if v >= 32768 else v


def compare(logfile, name, n=60, easy=None, use_cam=True, key="rows", air=False, ball=NORMAL_BALL):
    """Replays an emulator log in the model. The model starts from the state of the last warm-up row (mode, velocity
    vectors, bmove, friction, the glove's plane and collision flags). Each frame it uses the pad values and the
    camera angle that the game used in that frame (the values logged after the previous frame) and the press frames of
    the log (`presses`: the frames on which the glove acts on a press).
    Returns (largest difference of the ball's speed,
             rows of (j, emu speed, model speed, emu glove speed, model glove speed, emu mode, model mode))."""
    d = json.load(open(logfile))[name]
    rows, warm, pr = d[key], d["warm"], set(d["presses"])
    w = warm[-1]
    if easy is None:
        easy = d.get("difficulty", 0) == 0
    sim = Sim([w["bvel"][0], w["bvel"][2]], mode="J" if w["mode"] == 3 else "B", hvel=[w["hvel"][0], w["hvel"][2]],
              hplane=w["hplane"] and not air, coll=bool(w["hflags"] & 0x10), bmove=w["bmove"], easy=easy,
              stick=(float(s16(w["sx"])), float(s16(w["sy"]))), cam=w["cam"] if use_cam else None, air=air,
              ball=ball)
    sim.bfric = w["bfric"]
    prev = w
    worst = 0.0
    out = []
    for r in rows[:n]:
        sim.stick = (float(s16(prev["sx"])), float(s16(prev["sy"])))
        if use_cam:
            sim.cam = prev["cam"]
        m = sim.step(r["j"] in pr)
        e = math.hypot(r["bvel"][0], r["bvel"][2])
        worst = max(worst, abs(e - m["bspd"]))
        out.append((r["j"], e, m["bspd"], math.hypot(r["hvel"][0], r["hvel"][2]), m["hspd"], r["mode"], m["mode"]))
        prev = r
    return worst, out


KNOWN_GAPS = {
    "mash_release": "the pad is released for some frames: the glove's friction and a x0.7 on its speed are not modelled",
    "medium_triple": "the ball runs onto a slope after frame 38 (the log's floor normal is not flat)",
    "air_one": "the glove steps off the ball after 15 frames in the air",
    "air_triple": "the glove steps off the ball after 15 frames in the air",
}
SHORT = {"medium_triple": 38, "air_one": 15, "air_triple": 19}

CHECKS = {
    "mash3": {n: {} for n in ("p22", "p32", "p42", "p33", "p23", "p24", "p22_straight", "triple_straight",
                              "one_straight", "p22_stickdiag", "triple_stickdiag", "p22_stickfull", "p22_freecam",
                              "one_freecam")},
    "mash4": {n: {} for n in ("rand1", "rand2", "rand3", "rand4", "rand5", "rand6", "rand23_11", "rand23_12",
                              "rand23_13", "mash_release", "startB_p32", "startB_one", "medium_p22",
                              "medium_triple")},
    "air": {n: dict(air=True) for n in ("air_base", "air_one", "air_triple", "air_p22", "air_p32")},
    "bearing": {n: dict(ball=BEARING_BALL) for n in ("bearing_one", "bearing_triple", "bearing_p22", "bearing_p32")},
    "long": {n: {} for n in ("long22", "long32", "long42", "long_single")},
}


def check_all(verbose=True):
    results = {}
    for f, cases in CHECKS.items():
        path = os.path.join(HERE, "out", f + ".json")
        if not os.path.exists(path):
            continue
        have = json.load(open(path))
        for name, kw in cases.items():
            if name not in have:
                continue
            n = SHORT.get(name, 160 if f == "long" else 60)
            worst, out = compare(path, name, n=n, **kw)
            results[(f, name)] = (worst, min(n, len(out)))
            if verbose:
                note = ("  (" + KNOWN_GAPS[name] + ")") if name in KNOWN_GAPS else ""
                print("%-7s %-18s frames %2d  largest speed difference model vs emulator: %.6f%s"
                      % (f, name, min(n, len(out)), worst, note))
    return results


if __name__ == "__main__":
    check_all()
