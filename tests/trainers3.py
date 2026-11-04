"""The wall-clip indicator (practice/include/trainers.h) in Atlantis 1.

One face of the start floor's actor is rewritten in RAM as a large wall (the plane x = 100, turned towards the ball).
With the ball bearing in the glove, A held to the windup and Z on a chosen frame, then R on the next, the trainer's
cancel distance is the ball's real distance to the wall at the start of the cancel frame, its verdict (inside the
window or not) is checked against whether the glove really went through the wall, the window edge is compared with
the real edge found by trials at 0.5 units steps, and the live line follows the ball. The indicator is quiet with
another ball and when the switch is off.

Picture: trainers3_clip.png (needs --gfx angrylion)."""
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "tools", "tricks"))
import math  # noqa: E402
import struct  # noqa: E402
from trainers import (ACTOR_POS, ACTOR_PREV_POS, ACTOR_VEL, RUN_NOCLIP, RUN_TRAINERS, Rig, boot_to_atlantis,  # noqa: E402
                      get_joined, step)
from trainerlog import log_state  # noqa: E402

TR_CLIP = 0x40


def run(emu, sym, args):
    r = Rig(emu, sym)
    yield from boot_to_atlantis(emu, sym)
    yield 60
    yield from clip_check(r)


WALL_X = 100.0
FLOOR_ACTOR, WALL_FACE = 0x80335110, 2   # the actor of the start floor (Atlantis 1) and the face that becomes the wall
ACTOR_FACES = 0xE0
CLIP_OFFSET = 0x20C
BALL_TYPE = 0x1F                         # BallControl.type
HAND_JOINED, HAND_PREWHACK = 3, 5


def make_wall(r, x=WALL_X):
    """Rewrites one face of the floor's actor as a large wall at x, its normal pointing to -x, as the game computes its
    fields (CalculatePlaneInfoWithEdgeLens)."""
    e = r.emu
    face = e.r32(FLOOR_ACTOR + ACTOR_FACES) + 0x78 * WALL_FACE
    if face == e.r32(e.r32(r.ball + 0xD8) + 4):
        e.fail("the wall's face is the one under the ball")
    v = [(x, -300.0, 122.0 - 800.0), (x, -300.0, 122.0 + 800.0), (x, 600.0, 122.0)]
    n = (-1.0, 0.0, 0.0)
    fl = list(n) + [-(n[0] * v[0][0])] + [c for p in v for c in p]
    dirs, lens = [], []
    for i in range(3):
        d = [v[(i + 1) % 3][k] - v[i][k] for k in range(3)]
        ln = math.sqrt(sum(c * c for c in d))
        lens.append(ln)
        dirs += [c / ln for c in d]
    fl += dirs + lens
    fl += [v[1][1] - v[0][1], v[2][1] - v[0][1], v[1][2] - v[0][2], v[2][2] - v[0][2]]   # axis 1, 2: y then z
    for i, val in enumerate(fl):
        e.wf(face + 4 * i, val)
    e.w8(face + 0x75, 1)
    e.w8(face + 0x76, 2)


def clip_view(r):
    seq = r.emu.r32(r.sym("trainerViewSeq"))
    raw = bytes(r.emu.read(r.sym("trainerViews") + (seq & 1) * 0x24C + CLIP_OFFSET, 0x40))
    why, verdict, ground, angled, has, rverdict, outcome, rat, age = struct.unpack_from(">BBBBBBBbH", raw, 0)
    dist, speed, lo, hi, minspeed, rdist, rspeed, rhi, behind = struct.unpack_from(">9f", raw, 0x0C)
    counts = struct.unpack_from(">I", raw, 0x30)[0]
    return dict(counts=counts, why=why, verdict=verdict, ground=ground, angled=angled, has=has, rverdict=rverdict, outcome=outcome,
                rat=rat, age=age, dist=dist, speed=speed, lo=lo, hi=hi, minspeed=minspeed, rdist=rdist, rspeed=rspeed,
                rhi=rhi, behind=behind)


def hand_x(r):
    return r.emu.rf(r.hand + ACTOR_POS)


def ball_x(r):
    return r.emu.rf(r.ball + ACTOR_POS)


def to_bearing(r):
    """Presses R in the holding action until the ball is the bearing."""
    emu = r.emu
    for _ in range(6):
        if emu.r8(r.sym("ballControl") + BALL_TYPE) == 3:
            return
        yield from step(r, ("R",))
        yield from step(r, ())
        for _ in range(14):
            yield from r.next_frame()
        if r.mode() != HAND_JOINED:
            for _ in range(30):
                yield from r.next_frame()
    if emu.r8(r.sym("ballControl") + BALL_TYPE) != 3:
        emu.fail(f"the ball is not the bearing (type {emu.r8(r.sym('ballControl') + BALL_TYPE)})")


def place_run(r, x0):
    """The ball held by the glove at x0 on the start floor, the camera held turned to +x."""
    emu = r.emu
    yaw = math.pi / 2
    r.yaw0 = yaw
    emu.release()
    if r.mode() == 20:
        yield from step(r, ("L",))
        yield from step(r, ())
    bp = [x0, 8.0, 122.0]
    for off in (ACTOR_POS, ACTOR_PREV_POS, 0x4C):
        r.setvec(r.ball + off, bp)
    r.setvec(r.ball + ACTOR_VEL, [0, 0, 0])
    r.hold_camera(yaw)
    yield 3
    hp = [bp[0] - 30, bp[1] + 5, bp[2]]
    for off in (ACTOR_POS, ACTOR_PREV_POS, 0x4C):
        r.setvec(r.hand + off, hp)
    r.setvec(r.hand + ACTOR_VEL, [0, 0, 0])
    emu.hold("UP")
    for _ in range(80):
        r.hold_camera(yaw)
        yield from r.next_frame()
        if r.mode() == HAND_JOINED:
            break
    emu.release()
    if r.mode() != HAND_JOINED:
        emu.fail(f"the glove did not take the ball (action {r.mode()})")
    for _ in range(20):
        r.hold_camera(yaw)
        yield from r.next_frame()


def trial(r, trigger):
    """Walks at the wall holding UP. On the first game frame that ends with the ball at most TRIGGER from the wall, A goes
    down (4 frames), then Z joins it (the cancel), then R (the schedule: A x4, A+Z, A+Z+R). Returns
    the ball's distances after each frame from A on, the most that the glove went past the wall, and the trainer's view."""
    emu = r.emu
    dists, went = [], -1e9
    i0 = None
    for i in range(200):
        d = WALL_X - ball_x(r)
        if i0 is None and d <= trigger:
            i0 = i
        buttons = ["UP"]
        if i0 is not None:
            n = i - i0
            buttons.append("A")
            if n >= 4:
                buttons.append("Z")
            if n >= 5:
                buttons.append("R")
        yield from step(r, tuple(buttons))
        r.hold_camera(r.yaw0)
        if os.environ.get("TRAINERS_DEBUG") and i0 is not None:
            print("   n", i - i0, "buttons", buttons, "mode", r.mode(), "ball d %.2f" % (WALL_X - ball_x(r)), "hand x %.2f" % hand_x(r),
                  "clip", {k: (round(v, 2) if isinstance(v, float) else v) for k, v in clip_view(r).items() if k in ("why", "has", "rdist", "dist", "verdict", "age")})
        if i0 is not None:
            dists.append(WALL_X - ball_x(r))
            went = max(went, hand_x(r) - WALL_X)
            if i - i0 >= 14:
                break
        if d < -20:
            break
    emu.release()
    return dists, went, clip_view(r)


def clip_check(r):
    emu = r.emu
    yield from get_joined(r)
    make_wall(r)
    yield from place_run(r, -150.0)
    yield from to_bearing(r)
    emu.w8(r.run + RUN_TRAINERS, TR_CLIP)
    yield from place_run(r, -150.0)
    # The live line follows the ball.
    seen = []
    for i in range(200):
        yield from step(r, ("UP",))
        r.hold_camera(r.yaw0)
        seen.append(clip_view(r))
        if WALL_X - ball_x(r) < 45.0:
            break
    if seen[0]["why"] != 3:
        emu.fail(f"a wall is reported 200 units away: {seen[0]}")
    v = seen[-1]
    d_real = WALL_X - ball_x(r)
    print(f"trainers3: walking: wall {v['dist']:.2f} (ball is {d_real:.2f} from the plane now), speed {v['speed']:.3f}, "
          f"window {v['lo']:.1f} to {v['hi']:.2f}, ground {v['ground']}, why {v['why']}")
    if v["why"] != 0 or abs(v["dist"] - d_real) > 8.0 or v["speed"] < 3.0:
        emu.fail(f"the live line is wrong: {v}")
    emu.release()
    # Trials. The start is moved by half units, so the cancel frame's distance takes many values.
    plan = [(63.0, -150.0 + 0.5 * k) for k in range(14)] + [(56.0, -150.0 + 2.0 * k) for k in range(3)]
    rows = []
    for trigger, x0 in plan:
        yield from place_run(r, x0)
        yield from to_bearing(r)    # the R of the last trial changed the ball
        dists, went, f = yield from trial(r, trigger)
        if not f["has"] or len(dists) < 6:
            emu.fail(f"no cancel seen for trigger {trigger} at {x0}: {f}")
        # The cancel frame starts from the state after the frame with Z on the pad... find which frame's distance it is.
        match = [i for i, d in enumerate(dists) if abs(d - f["rdist"]) < 0.02]
        rows.append(dict(trigger=trigger, x0=x0, d=f["rdist"], went=went, f=f, match=match))
    for row in rows:
        f = row["f"]
        print(f"trainers3: x0 {row['x0']:.1f} trigger {row['trigger']:.0f}: cancel D {row['d']:.2f} speed {f['rspeed']:.3f} "
              f"top {f['rhi']:.2f} verdict {f['rverdict']} R at {f['rat']} outcome {f['outcome']}; glove "
              f"{row['went']:+.2f} past the wall; at frame {row['match']}")
    emu.release()
    idx = {row["match"][0] if row["match"] else -1 for row in rows}
    if len(idx) != 1 or -1 in idx:
        emu.fail(f"the cancel distance is not the ball's distance after the same frame every time: {idx}")
    bad = 0
    yes = [row["d"] for row in rows if row["went"] > 0.5]
    no = [row["d"] for row in rows if row["went"] <= 0.5]
    for row in rows:
        f = row["f"]
        if f["rat"] != 1:
            emu.fail(f"R at {f['rat']}, not 1")
        if (f["outcome"] == 1) != (row["went"] > 0.5):
            emu.fail(f"the outcome {f['outcome']} does not match the glove ({row['went']:.2f} past the wall)")
        if (f["rverdict"] == 0) != (row["went"] > 0.5) and abs(row["d"] - f["rhi"]) > 0.5:
            bad += 1
    print(f"trainers3: real clips up to D {max(yes) if yes else None}; the least D without a clip {min([d for d in no if d > 12], default=None)}")
    if bad:
        emu.fail(f"{bad} verdicts differ from the game away from the edge of the window")
    if not yes or not [d for d in no if d > 12]:
        emu.fail("the trials found no edge of the window")
    worst = max(max(row['f']['counts'] for row in rows), max(c['counts'] for c in seen))
    print(f"trainers3: the wall scan took at most {worst} counts ({worst / 46875:.2f} ms) in these frames")
    print(f"trainers3: {len(rows)} cancels: verdicts and outcomes match the game, away from the edge")
    wc = [l for l in log_state(emu, r.sym)[3] if l[1].startswith("WC ")]
    if not wc or not ("CLIPPED" in wc[0][1] or "NO CLIP" in wc[0][1]):
        emu.fail(f"the wall clip left no good line in the trainer log: {wc}")
    print(f"trainers3: the trainer log has the cancels, newest {wc[0][1]!r}")
    r.picture("trainers3_clip")
    # Not the bearing (the last R made another ball): no line, and a reason. Quiet in noclip.
    yield from place_run(r, -150.0)
    if emu.r8(r.sym("ballControl") + BALL_TYPE) == 3:
        emu.fail("the ball is still the bearing")
    yield from step(r, ("UP",))
    if clip_view(r)["why"] != 2:
        emu.fail(f"another ball gives why {clip_view(r)['why']}, not 2")
    emu.w8(r.run + RUN_NOCLIP, 1)
    for _ in range(3):
        yield from r.next_frame()
    if r.view()["valid"]:
        emu.fail("the wall clip is not quiet in noclip")
    emu.w8(r.run + RUN_NOCLIP, 0)
    emu.w8(r.run + RUN_TRAINERS, 0)
    print("trainers3: another ball gives no line, and noclip is quiet")
