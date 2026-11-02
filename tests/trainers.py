"""The trick trainers (practice/include/trainers.h) in Atlantis 1: the common code, the L mash trainer and the MBZ-L
trainer.

1. The pad at the hook against the pad at ProcessInput, over 1,500 game frames of random presses (the open point of
   docs/plan.md, "Trick trainers").
2. Quiet: with the switch off nothing is computed or drawn; in a pause, under the free camera and in noclip the
   view is empty.
3. L mash: patterns of L presses at chosen gaps with the diagonal D-pad. The verdict of every press and the
   target are compared with the tables of docs/game.md, computed from the frame log (an independent record).
4. MBZ-L: B and A attempts with chosen Z and L timing, against the windows of docs/game.md, speeds and verdict numbers.
5. A savestate load and a level start clear the state.

Pictures: trainers_lmash.png and trainers_mbz.png (needs --gfx angrylion)."""
import math
import os
import random
import struct
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "tools", "tricks"))
import layout  # noqa: E402
import lmash  # noqa: E402  the L mash model (tools/tricks/lmash.py)
import trace  # noqa: E402
from framelog import read_log  # noqa: E402
from tools import C_ALL, press, wait_for  # noqa: E402

RUN_FRAME_LOG, RUN_TRAINERS, RUN_MOON_JUMP, RUN_NOCLIP, RUN_FREE_CAM = 0x31, 0x48, 0x33, 0x34, 0x46
TR_LMASH, TR_LINE, TR_MBZL, TR_MOON = 1, 2, 4, 8
VIEW_SIZE = 0x24C
LM_STRIP_CELLS = 24
ACTION_RESET, ACTION_SAVE_STATE, ACTION_LOAD_STATE = 0, 8, 9   # practice/include/actions.h
DIAG = dict(hook_calls=0x00, frames=0x04, first_diff=0x08, last_diff=0x0C, first_pressed_diff=0x10,
            last_pressed_diff=0x14, steps=0x18, skipped=0x1C)
ACTOR_POS, ACTOR_PREV_POS, ACTOR_VEL = 0x34, 0x40, 0x58
ACTOR_PLAT_PREV_POS = 0x4C
ACTOR_STATE, ACTOR_ROT = 0xD4, 0x78

JOINED, PREWHACK, THROWAIM, BOUNCE, BALLWALK, RELEASE = 3, 5, 11, 14, 20, 9
CONT_L = 0x0020

# L mash verdicts (trainers.h)
LM_NAMES = {0: "NONE", 1: "STAND", 2: "HOLD"}
LMV_NONE, LMV_ON_TIME, LMV_LATE, LMV_SLOW, LMV_REST, LMV_EARLY = range(6)


class Rig:
    """Reads and drives the game from a test."""

    def __init__(self, emu, sym):
        self.emu, self.sym = emu, sym
        self.hand, self.ball, self.cam = sym("hand"), sym("ball"), sym("camera")
        self.run = sym("run")
        self.fc2 = sym("frameCount2")
        self.start = None  # the ball's place at the level's start
        self.yaw0 = None   # the camera's yaw there: the tests hold it, so the pad always points the same way

    def fc(self):
        return self.emu.r32(self.fc2)

    def vec(self, a):
        return [self.emu.rf(a + 4 * i) for i in range(3)]

    def setvec(self, a, v):
        for i in range(3):
            self.emu.wf(a + 4 * i, v[i])

    def mode(self):
        return self.emu.r16(self.emu.r32(self.hand + ACTOR_STATE) + 4)

    def next_frame(self, limit=40):
        """Waits (in video frames) until the game frame counter changes: the logic of that frame is then complete."""
        start = self.fc()
        for _ in range(limit):
            yield 1
            if self.fc() != start:
                return
        self.emu.fail("no game frame")

    def shift_all(self, dx, dz):
        """Moves the glove and the ball by (dx, dz) at a game frame boundary."""
        for a in (self.ball, self.hand):
            for off in (ACTOR_POS, ACTOR_PREV_POS, ACTOR_PLAT_PREV_POS):
                for k, d in ((0, dx), (2, dz)):
                    self.emu.wf(a + off + 4 * k, self.emu.rf(a + off + 4 * k) + d)

    def hold_camera(self, yaw):
        """Sets the camera's yaw and its aim (rot.y, rotAim.y)."""
        self.emu.wf(self.cam + ACTOR_ROT + 4, yaw)
        self.emu.wf(self.cam + ACTOR_ROT + 4 + 12, yaw)

    def diag(self):
        base = self.sym("trainerDiag")
        return {k: self.emu.r32(base + off) for k, off in DIAG.items()}

    def view(self):
        """The newest TrainerView as a dict."""
        seq = self.emu.r32(self.sym("trainerViewSeq"))
        raw = bytes(self.emu.read(self.sym("trainerViews") + (seq & 1) * VIEW_SIZE, VIEW_SIZE))
        frame2, on, valid, mbz_count = struct.unpack_from(">IBBB", raw, 0)
        kind, verdict, gap, over, warn, cells, lmode = struct.unpack_from(">7B", raw, 8)
        strip = list(raw[0x10:0x28])
        metro = raw[0x0F]
        move, peak, avg20, target = struct.unpack_from(">4f", raw, 0x28)
        v = dict(seq=seq, frame2=frame2, on=on, valid=valid, mbz_count=mbz_count,
                 lmash=dict(kind=kind, verdict=verdict, gap=gap, over=over, warn=warn, cells=cells, mode=lmode,
                            strip=strip, move=move, peak=peak, avg20=avg20, target=target, metro=metro), mbz=[])
        for i in range(mbz_count):
            o = 0x38 + i * 0x2C
            (k, ver, zkind, n, zf, aim, cancel, lf, lt, sflags, air, done, bmask, zmask, lmask,
             _) = struct.unpack_from(">BBBbbbbbbBBBHHHH", raw, o)
            v0, speed, speed3, speed6, best = struct.unpack_from(">5f", raw, o + 0x14)
            (done_frame,) = struct.unpack_from(">I", raw, o + 0x28)
            v["mbz"].append(dict(kind=k, verdict=ver, zkind=zkind, n=n, zframe=zf, aim=aim, cancel=cancel, l=lf,
                                 ltarget=lt, sflags=sflags, air=air, done=done, bmask=bmask, zmask=zmask,
                                 lmask=lmask, v0=v0, speed=speed, speed3=speed3, speed6=speed6, best=best,
                                 done_frame=done_frame))
        return v

    def picture(self, name):
        """The frame buffer that holds a drawn picture: the one with more colours (the other is still being drawn)."""
        colours = [len(set(bytes(self.emu.read(self.sym(("cfb_16_a", "cfb_16_b")[b]), 320 * 240 * 2))[::2])) for b in (0, 1)]
        self.emu.fb_png(name, self.sym(("cfb_16_a", "cfb_16_b")[colours.index(max(colours))]))


def boot_to_atlantis(emu, sym):
    gi = sym("gameInfo")
    fc = lambda: emu.r32(sym("frameCount"))  # noqa: E731
    mode = lambda: emu.r8(gi)  # noqa: E731
    level = lambda: emu.r8(gi + 1)  # noqa: E731
    yield from wait_for(emu, lambda: level() == trace.PRESENT and fc() >= 200, 4000, "the logos")
    yield from press(emu, "START", frames=6)
    yield from wait_for(emu, lambda: level() == trace.FLYTHRU and mode() == 4 and fc() >= 30, 3000, "the title")
    yield from press(emu, *C_ALL)
    for b in ("A", "DOWN", "DOWN", "A"):  # Warp, Go
        yield from press(emu, b)
    yield from wait_for(emu, lambda: level() == trace.ATLANTIS1 and mode() == 4 and fc() >= 100, 3000, "Atlantis 1")
    emu.w8(sym("settings") + layout.SETTINGS_HUD, 0)  # the timer off
    emu.w8(sym("settings") + layout.SETTINGS_TRAINER_HOLD, 1)  # the result hold off: these tests read the live views


def get_joined(r):
    """The glove holds the ball on the level's flat start floor, where the ball waits (the glove starts in the water).
    The ball always starts from the same place."""
    emu = r.emu
    emu.release()
    if r.start is None:
        r.start = r.vec(r.ball + ACTOR_POS)
        r.yaw0 = emu.rf(r.cam + ACTOR_ROT + 4)
    if r.mode() == BALLWALK:
        yield from step(r, ("L",))  # stand on the ball no more
        yield from step(r, ())
    for off in (ACTOR_POS, ACTOR_PREV_POS, ACTOR_PLAT_PREV_POS):
        r.setvec(r.ball + off, r.start)
    r.setvec(r.ball + ACTOR_VEL, [0, 0, 0])
    yield 3
    bp = r.start
    a = emu.rf(r.cam + ACTOR_ROT + 4)
    hp = [bp[0] - 30 * math.sin(a), bp[1] + 5, bp[2] - 30 * math.cos(a)]
    for off in (ACTOR_POS, ACTOR_PREV_POS, ACTOR_PLAT_PREV_POS):
        r.setvec(r.hand + off, hp)
    r.setvec(r.hand + ACTOR_VEL, [0, 0, 0])
    emu.hold("UP")
    for _ in range(80):
        yield from r.next_frame()
        if r.mode() == JOINED:
            break
    emu.release()
    if r.mode() != JOINED:
        emu.fail(f"the glove did not take the ball (action {r.mode()})")
    for _ in range(20):
        yield from r.next_frame()


def step(r, buttons=(), x=0, y=0):
    """Sets the input for the next game frame and waits for that frame. Returns the trainers' view after it."""
    f = r.fc()
    r.emu.hold(*buttons, x=x, y=y)
    for n in range(200):
        if r.fc() != f:
            break
        yield 1
    else:
        r.emu.fail("the game frame counter does not advance")
    if r.fc() != f + 1:
        r.emu.fail(f"game frames skipped: {f} to {r.fc()}")
    return r.view()


def xz_dist(a, b):
    return math.hypot(a[0] - b[0], a[2] - b[2])


def run(emu, sym, args):
    r = Rig(emu, sym)
    yield from boot_to_atlantis(emu, sym)
    yield 60
    emu.w8(r.run + RUN_FRAME_LOG, 1)
    yield from get_joined(r)
    only = os.environ.get("TRAINERS_ONLY")  # while developing: a list of the checks to run
    for name, check in (("pad", pad_check), ("quiet", quiet_check), ("lmash", lmash_check), ("mbz", mbz_check),
                        ("reset", reset_check)):
        if not only or name in only.split(","):
            yield from check(r)


def pad_check(r):
    """1. The pad at the hook against the pad at ProcessInput, over 1,000 game frames of random presses."""
    emu = r.emu
    emu.w8(r.run + RUN_TRAINERS, TR_LMASH)
    rng = random.Random(7)
    d0 = r.diag()
    pads = ("A", "B", "Z", "L", "UP", "RIGHT")
    for i in range(1000):
        if rng.random() < 0.5:
            emu.pulse(rng.choice(pads), polls=1)
        elif rng.random() < 0.2:
            emu.hold(*rng.sample(pads, rng.randint(0, 3)))
        yield from r.next_frame()
    emu.release()
    d1 = r.diag()
    diff = {k: d1[k] - d0[k] for k in d1}
    print("trainers: pad check over", diff["frames"], "game frames:", diff)
    emu.w8(r.run + RUN_TRAINERS, 0)
    if diff["frames"] < 600:
        emu.fail(f"the hook and ProcessInput met on only {diff['frames']} of 1000 game frames")
    # The last hook run before the game frame sees the pad that ProcessInput sees, bar a read in between. The first
    # hook run of the frame does not: that is why the logic runs in ProcessInput.
    if diff["last_diff"] > 5 or diff["last_pressed_diff"] > 5:
        emu.fail(f"the pad at the last hook run differs from the pad at ProcessInput on {diff['last_diff']} frames")
    yield from get_joined(r)


def reset_check(r):
    """5. A savestate load and a level start clear the state of the trainers."""
    emu = r.emu
    emu.w8(r.run + RUN_TRAINERS, TR_LMASH | TR_MBZL)
    yield from get_joined(r)
    # The practice ROM's own savestate (the harness's whole-machine states would also restore the trainers' memory).
    requests = r.sym("requests")
    emu.w16(requests, emu.r16(requests) | 1 << ACTION_SAVE_STATE)
    for _ in range(3):
        yield from r.next_frame()
    yield from mash(r, set(gaps_to_presses(0, (2,), 20)), 20)
    before = r.view()["lmash"]
    if before["kind"] == 0 or before["cells"] < LM_STRIP_CELLS:
        emu.fail(f"the L mash trainer holds no state to clear: {before}")
    emu.w16(requests, emu.r16(requests) | 1 << ACTION_LOAD_STATE)
    for _ in range(3):
        yield from r.next_frame()
    after = r.view()["lmash"]
    if after["kind"] != 0 or after["peak"] != 0.0 or after["cells"] > 8:
        emu.fail(f"a savestate load did not clear the L mash trainer: {after}")
    print("trainers: a savestate load clears the state")
    yield from get_joined(r)
    yield from mash(r, set(gaps_to_presses(0, (2,), 20)), 20)
    if r.view()["lmash"]["kind"] == 0:
        emu.fail("the L mash trainer holds no state to clear before the level start")
    emu.w16(requests, emu.r16(requests) | 1 << ACTION_RESET)
    yield from wait_for(emu, lambda: r.fc() < 20, 2000, "the level to restart")
    if r.view()["valid"]:
        emu.fail("the view is not empty at a level start")
    yield from wait_for(emu, lambda: r.fc() >= 30 and r.view()["valid"], 2000, "the trainers to run in the new level")
    after = r.view()["lmash"]
    if after["kind"] != 0 or after["peak"] != 0.0 or after["cells"] > r.fc():
        emu.fail(f"a level start did not clear the L mash trainer: {after}")
    print("trainers: a level start clears the state")
    emu.w8(r.run + RUN_TRAINERS, 0)


def quiet_check(r):
    """2. Off: no hook work, no text, an empty view. Quiet: noclip, moon jump, the free camera and a pause give an empty
    view, and the trainers come back after each. (An exit is not tested.)"""
    emu = r.emu
    text_frames = lambda: emu.r32(r.sym("practice") + 0x08)  # noqa: E731  Practice.textFrames
    d0, t0 = r.diag(), text_frames()
    yield 40
    if r.diag()["hook_calls"] != d0["hook_calls"]:
        emu.fail("the hook counted calls with every trainer off")
    if text_frames() - t0 >= 2:
        emu.fail("text is drawn with every trainer off")
    emu.w8(r.run + RUN_TRAINERS, TR_LMASH | TR_MBZL)
    yield from r.next_frame()
    yield from r.next_frame()
    v = r.view()
    if not v["valid"]:
        emu.fail("the view is empty with the glove in a level")
    t0 = text_frames()
    yield 30
    if text_frames() - t0 < 2:
        emu.fail("no text is drawn with the trainers on")
    # Each switch is a byte in memory: the Run switches, and the game's pause flag (the game thread goes on and the game
    # frames do not: the pause is waited out in video frames).
    paused = r.sym("gamePaused")
    for name, addr, value in (("noclip", r.run + RUN_NOCLIP, 1), ("moon jump", r.run + RUN_MOON_JUMP, 1),
                              ("free camera", r.run + RUN_FREE_CAM, 3), ("pause", paused, 1)):
        emu.w8(addr, value)
        if addr == paused:
            yield 30
        else:
            for _ in range(3):
                yield from r.next_frame()
        if r.view()["valid"]:
            emu.fail(f"the trainers are not quiet in {name}")
        emu.w8(addr, 0)
        for _ in range(4):
            yield from r.next_frame()
        if not r.view()["valid"]:
            emu.fail(f"the trainers did not come back after {name}")
    emu.w8(r.run + RUN_TRAINERS, 0)
    for _ in range(3):
        yield from r.next_frame()
    if r.view()["valid"]:
        emu.fail("the view is not empty after the trainers were turned off")
    print("trainers: off, noclip, moon jump, the free camera and a pause are quiet")


# ---- L mash ------------------------------------------------------------------------------------------------------

def expected_verdicts(entries):
    """From the frame log: (frame, kind, verdict, gap, over) of every L press in the glove's holding or standing
    action, by the table of docs/game.md (docs/plan.md, "Trick trainers")."""
    out, last = [], None
    for idx, kind in entries:
        gap = idx - last[0] if last and last[1] != kind and idx - last[0] <= 30 else 0
        if gap == 0:
            verdict, over = LMV_NONE, 0
        elif gap < 2:
            verdict, over = LMV_EARLY, 0
        elif gap == 2:
            verdict, over = LMV_ON_TIME, 0
        elif kind == 1:
            verdict, over = LMV_LATE, gap - 2
        elif gap >= 9:
            verdict, over = LMV_REST, 0
        else:
            verdict, over = LMV_SLOW, gap - 2
        out.append((idx, kind, verdict, gap, over))
        last = (idx, kind)
    return out


def mash(r, presses, n, dpad=("UP", "RIGHT"), warm=30, lock_cam=True, y_lift=0.0):
    """Holds the pad for WARM + N game frames and presses L on the game frames WARM + j for j in PRESSES. Returns the
    rows: the view, the frame log's newest entry and the ball's move for each game frame."""
    emu = r.emu
    rows = []
    c0 = r.yaw0
    r.hold_camera(c0)
    pos = r.vec(r.ball + ACTOR_POS)
    home = [pos[0], pos[2]]
    if y_lift:
        for a in (r.ball, r.hand):
            for off in (ACTOR_POS, ACTOR_PREV_POS, ACTOR_PLAT_PREV_POS):
                emu.wf(a + off + 4, emu.rf(a + off + 4) + y_lift)
    last = r.vec(r.ball + ACTOR_POS)
    for i in range(warm + n):
        j = i - warm
        view = yield from step(r, dpad + (("L",) if j in presses else ()))
        if lock_cam:
            r.hold_camera(c0)
        pos = r.vec(r.ball + ACTOR_POS)
        entry = read_log(emu, r.sym)[-1]
        rows.append(dict(j=j, view=view, entry=entry, disp=xz_dist(pos, last)))
        if os.environ.get("TRAINERS_DEBUG") and i < 40:
            print("  ball flags", hex(emu.r32(r.ball + 0x28)), "water", emu.r8(emu.r32(r.ball + ACTOR_STATE) + 9),
                  "hand water", emu.r8(emu.r32(r.hand + ACTOR_STATE) + 9), "pos", [round(v, 1) for v in pos])
        last = pos
        if abs(pos[0] - home[0]) > 40 or abs(pos[2] - home[1]) > 40:
            r.shift_all(home[0] - pos[0], home[1] - pos[2])
            last = r.vec(r.ball + ACTOR_POS)
    emu.release()
    if os.environ.get("TRAINERS_DEBUG"):
        print("  skipped so far", r.diag()["skipped"], "steps", r.diag()["steps"])
    return rows


def gaps_to_presses(first, gaps, n):
    t, out, i = first, [], 0
    while t < n:
        out.append(t)
        t += gaps[i % len(gaps)]
        i += 1
    return out


def check_mash(r, name, rows, target=None):
    """Checks every press of a run against the verdicts computed from the frame log, and the numbers."""
    emu = r.emu
    if os.environ.get("TRAINERS_DEBUG"):
        for k, x in enumerate(rows[:46]):
            e, v = x["entry"], x["view"]["lmash"]
            print(k, "log frame", e[0], "mode", e[4], "pressed", hex(e[2]), "| view frame2", x["view"]["frame2"],
                  "kind", v["kind"], "verdict", v["verdict"], "gap", v["gap"], "move", round(v["move"], 2),
                  "disp", round(x["disp"], 2), "strip", v["strip"][-4:])
    entries = [(k, 1 if e[4] == JOINED else 2) for k, e in enumerate(x["entry"] for x in rows)
               if e[2] & CONT_L and e[4] in (JOINED, BALLWALK)]
    want = expected_verdicts(entries)
    for idx, kind, verdict, gap, over in want:
        v = rows[idx]["view"]["lmash"]
        got = (v["kind"], v["verdict"], v["gap"], v["over"])
        if got != (kind, verdict, gap, over):
            emu.fail(f"{name}: press on frame {idx}: the trainer shows {got}, the table gives {(kind, verdict, gap, over)}")
    # The move is the ball's displacement in the game frame before.
    worst = max(abs(rows[k]["view"]["lmash"]["move"] - rows[k - 1]["disp"]) for k in range(1, len(rows)))
    if worst > 0.01:
        emu.fail(f"{name}: MOVE differs from the ball's displacement by {worst:.4f}")
    moves = [x["view"]["lmash"]["move"] for x in rows]
    last = rows[-1]["view"]["lmash"]
    avg = sum(moves[-20:]) / 20
    if abs(last["avg20"] - avg) > 0.01:
        emu.fail(f"{name}: AVG20 {last['avg20']:.3f}, not {avg:.3f}")
    if abs(last["peak"] - max(moves[want[0][0]:])) > 0.01 if want else False:
        emu.fail(f"{name}: PEAK {last['peak']:.3f}, not {max(moves[want[0][0]:]):.3f}")
    if target is not None and not last["warn"] & 0x08 and abs(last["target"] - target) > 0.05:
        emu.fail(f"{name}: TARGET {last['target']:.3f}, the model gives {target:.3f}")
    gaps = [w[3] for w in want]
    print(f"trainers: {name}: {len(want)} presses, gaps {gaps[:14]}, verdicts agree; "
          f"avg20 {last['avg20']:.2f} target {last['target']:.2f} peak {last['peak']:.2f}")
    return want


def model_target(stick, air=False):
    r = lmash.run_pattern(range(0, 300, 2), 300, stick=stick, air=air)
    moved = [x["moved"] for x in r]
    return sum(moved[100:140]) / 40


def lmash_check(r):
    """3. L mash."""
    emu = r.emu
    emu.w8(r.run + RUN_TRAINERS, TR_LMASH)
    yield from get_joined(r)
    skip0 = r.diag()["skipped"]
    diag = (79.0, 79.0)

    # (2,2): the perfect mash; the target is the model's.
    rows = yield from mash(r, set(gaps_to_presses(0, (2,), 60)), 60)
    want = check_mash(r, "mash 2,2", rows, target=model_target(diag))
    if any(w[2] not in (LMV_NONE, LMV_ON_TIME) for w in want):
        emu.fail("a perfect 2,2 mash was not on time on every press")
    if rows[-1]["view"]["lmash"]["warn"] & 0x0F:
        emu.fail(f"a warning shows for the diagonal D-pad: {rows[-1]['view']['lmash']['warn']:#x}")
    if rows[-1]["view"]["lmash"]["avg20"] < 22.0:
        emu.fail("the ball moved less than 22 a frame in the perfect mash")
    r.picture("trainers_lmash")
    # A teleport (a respawn, a checkpoint) is not a move: it does not set the move or the peak.
    last = rows[-1]["view"]["lmash"]
    emu.wf(r.ball + ACTOR_POS, emu.rf(r.ball + ACTOR_POS) + 100.0)
    tele = (yield from step(r, ("UP", "RIGHT")))["lmash"]
    if tele["peak"] > last["peak"] + 0.01 or tele["move"] > 30.0:
        emu.fail(f"a teleport of 100 units counts as a move: move {tele['move']:.1f}, peak {last['peak']:.1f} to {tele['peak']:.1f}")
    r.emu.release()

    yield from get_joined(r)
    rng = random.Random(3)
    for name, gaps in (("mash 3,2", (3, 2)), ("mash 2,3", (2, 3)), ("mash 4,2", (4, 2)),
                       ("mash random", tuple(rng.choice((2, 2, 3, 4, 5, 9, 11)) for _ in range(40)))):
        rows = yield from mash(r, set(gaps_to_presses(0, gaps, 60)), 60)
        check_mash(r, name, rows)
        yield from get_joined(r)

    # The straight D-pad: PAD TOO WEAK on the ground, with the target for it.
    rows = yield from mash(r, set(gaps_to_presses(0, (2,), 60)), 60, dpad=("UP",))
    check_mash(r, "mash 2,2 straight", rows, target=model_target((0.0, 79.0)))
    if not rows[-1]["view"]["lmash"]["warn"] & 0x01:
        emu.fail("no PAD TOO WEAK with the straight D-pad on the ground")
    yield from get_joined(r)

    # The pad let go: PAD RELEASED. The camera not held: CAMERA TURNING with the diagonal D-pad.
    rows = yield from mash(r, set(gaps_to_presses(0, (2,), 30)), 30, lock_cam=False)
    turning = sum(1 for x in rows[10:] if x["view"]["lmash"]["warn"] & 0x02)
    print(f"trainers: camera not held: CAMERA TURNING on {turning} of {len(rows) - 10} frames")
    if turning < 10:
        emu.fail("no CAMERA TURNING with the camera turning")
    yield from get_joined(r)
    for _ in range(4):
        view = yield from step(r, ())
    if not view["lmash"]["warn"] & 0x04:
        emu.fail("no PAD RELEASED with the pad let go")

    # Normal ball controls on Medium.
    slot = emu.r16(r.sym("currentSaveSlot"))
    difficulty = r.sym("saveSlot") + slot * 0x1C + 0x11
    emu.w8(r.sym("settings") + layout.SETTINGS_BALL_NORMAL, 1)
    emu.w8(difficulty, 1)
    view = yield from step(r, ("UP",))
    if not view["lmash"]["warn"] & 0x20:
        emu.fail("no NORMAL CONTROLS with the retail controls on Medium")
    emu.w8(difficulty, 0)
    view = yield from step(r, ("UP",))
    if view["lmash"]["warn"] & 0x20:
        emu.fail("NORMAL CONTROLS shows on Easy")
    emu.w8(r.sym("settings") + layout.SETTINGS_BALL_NORMAL, 0)
    emu.release()
    yield from get_joined(r)

    # In the air with the straight D-pad: no PAD TOO WEAK, IN AIR on, and the model's target for the air.
    for _ in range(3):
        yield from step(r, ("UP",))
    rows = yield from mash(r, set(gaps_to_presses(0, (2,), 14)), 14, dpad=("UP",), warm=2, y_lift=900.0)
    air = [x["view"]["lmash"] for x in rows[4:12]]
    print(f"trainers: in the air: warnings {[hex(v['warn']) for v in air]}, target {air[-1]['target']:.2f}")
    if any(v["warn"] & 0x01 for v in air) or not all(v["warn"] & 0x08 for v in air):
        emu.fail("in the air: PAD TOO WEAK shows, or IN AIR does not")
    if abs(air[-1]["target"] - model_target((0.0, 79.0), air=True)) > 0.05:
        emu.fail(f"in the air: TARGET {air[-1]['target']:.3f}, the model gives {model_target((0.0, 79.0), air=True):.3f}")

    d = r.diag()
    if d["skipped"] != skip0:
        emu.fail(f"ProcessInput ran {d['skipped'] - skip0} times without a state from the hook")
    emu.w8(r.run + RUN_TRAINERS, 0)
    yield from get_joined(r)


# ---- MBZ-L -------------------------------------------------------------------------------------------------------

def mk_plan(n, key, hold_len, z_down, l_at, dirs=("UP",)):
    plan = []
    for i in range(n):
        b = list(dirs)
        if key and i < hold_len:
            b.append(key)
        if i in z_down:
            b.append("Z")
        if l_at is not None and i == l_at:
            b.append("L")
        plan.append(tuple(b))
    return plan


# name: (plan, expected verdict, aim, cancel, speed, n)
MBV = dict(LIVE=0, PERFECT=1, L_EARLY_LOST=2, L_EARLY_STOOD=3, L_LATE=4, NO_L=5, Z_LATE=6, Z_FRESH=7, NO_Z=8,
           Z_HELD=9, RELEASED=10)
GROUND_SCENARIOS = {
    "B perfect": (mk_plan(20, "B", 8, range(7), 7), "PERFECT", 6, 6, 10.43, 0),
    "B L late 1": (mk_plan(20, "B", 8, range(8), 8), "L_LATE", 6, 6, 9.09, 1),
    "B L late 2": (mk_plan(20, "B", 8, range(9), 9), "L_LATE", 6, 6, 8.01, 2),
    "B Z late 1": (mk_plan(20, "B", 9, range(7, 9), 8), "Z_LATE", 6, 7, None, 1),
    "B Z fresh on 4": (mk_plan(20, "B", 8, range(4, 8), None), "Z_FRESH", -1, -1, None, 4),
    # Z down at the start, let go, and pressed again before the aim: a fresh press.
    "B Z armed, released, fresh on 5": (mk_plan(20, "B", 8, {0, 5, 6, 7}, None), "Z_FRESH", -1, -1, None, 5),
    "B no Z": (mk_plan(20, "B", 14, (), None), "NO_Z", 6, -1, None, 0),
    "B L on 5": (mk_plan(20, "B", 8, range(7), 5), "L_EARLY_STOOD", -1, -1, None, 2),
    "B L on 6": (mk_plan(20, "B", 8, range(7), 6), "L_EARLY_LOST", 6, 6, None, 1),
    "B Z held": (mk_plan(20, "B", 8, range(10), 7), "Z_HELD", 6, 6, 10.43, 0),
    # B let go in the aim, with Z coming on the frame that the aim ends: the release counts.
    "B released in the aim": (mk_plan(20, "B", 8, (), None), "RELEASED", 6, -1, None, 9),
    "B released, Z on the frame after": (mk_plan(20, "B", 8, {10}, None), "RELEASED", 6, -1, None, 9),
    "A perfect": (mk_plan(20, "A", 6, range(5), 5), "PERFECT", 4, 4, 10.43, 0),
    "A Z fresh on 3": (mk_plan(20, "A", 6, range(3, 5), 5), "PERFECT", 4, 4, 10.43, 0),
    "A L late 1": (mk_plan(20, "A", 6, range(6), 6), "L_LATE", 4, 4, 9.09, 1),
}


AIR_SCENARIOS = {
    "air B perfect": (mk_plan(26, "B", 8, range(7), 7), "PERFECT", 6, 6, 14.00, 0),
    "air B L late 1": (mk_plan(26, "B", 8, range(8), 8), "L_LATE", 6, 6, 13.68, 1),
    "air B L on 5": (mk_plan(26, "B", 8, range(7), 5), "L_EARLY_LOST", 6, 6, None, 2),
}


def mbz_check(r):
    """4. MBZ-L from the ground: the glove holds the ball, which runs at 7, Z held (armed)."""
    emu = r.emu
    emu.w8(r.run + RUN_TRAINERS, TR_MBZL)
    yield from get_joined(r)
    c0 = r.yaw0
    r.hold_camera(c0)
    pos = r.vec(r.ball + ACTOR_POS)
    home = [pos[0], pos[2]]

    def stepper(buttons):
        view = yield from step(r, buttons)
        r.hold_camera(c0)
        p = r.vec(r.ball + ACTOR_POS)
        if abs(p[0] - home[0]) > 40 or abs(p[2] - home[1]) > 40:
            r.shift_all(home[0] - p[0], home[1] - p[2])
        return view

    # Stand on the ball, walk, then L with Z: Z is held and not new when the glove holds the ball again.
    for b in [("L",)] + [("UP",)] * 12 + [("UP", "L", "Z")] + [("UP", "Z")] * 5:
        view = yield from stepper(b)
    if r.mode() != JOINED:
        emu.fail(f"the glove is in action {r.mode()}, not JOINED, in the base state")
    state = emu.save("mbz_ground")
    yield 3
    results = {}
    for name, (plan, verdict, aim, cancel, speed, n) in GROUND_SCENARIOS.items():
        emu.hold("UP", "Z")
        emu.load(state)
        yield 3
        for b in plan:
            view = yield from stepper(b)
        a = view["mbz"][0] if view["mbz"] else None
        if a is None:
            emu.fail(f"MBZ {name}: no attempt shown")
        results[name] = a
        v0, sp, best = a["v0"], a["speed"], a["best"]
        print(f"trainers: MBZ-L {name}: verdict {a['verdict']} n {a['n']} aim {a['aim']} cancel {a['cancel']} "
              f"z {a['zkind']}/{a['zframe']} l {a['l']} target {a['ltarget']} bmask {a['bmask']:#x} V {v0:.2f} speed {sp:.2f} best {best:.2f} "
              f"+3 {a['speed3']:.2f} +6 {a['speed6']:.2f}")
        if a["verdict"] != MBV[verdict]:
            emu.fail(f"MBZ {name}: verdict {a['verdict']}, not {verdict}")
        if a["n"] != n:
            emu.fail(f"MBZ {name}: the verdict's number is {a['n']}, not {n}")
        if aim is not None and a["aim"] != aim:
            emu.fail(f"MBZ {name}: the aim shows on frame {a['aim']}, not {aim}")
        if cancel is not None and a["cancel"] != cancel:
            emu.fail(f"MBZ {name}: the cancel is on frame {a['cancel']}, not {cancel}")
        if speed is not None:
            if abs(v0 - 7.0) > 0.15 or abs(sp - speed) > 0.07:
                emu.fail(f"MBZ {name}: speed {v0:.2f} to {sp:.2f}, docs/game.md gives 7.00 to {speed}")
            if abs(best - 10.43) > 0.07:
                emu.fail(f"MBZ {name}: the best speed is {best:.2f}, not 10.43")
        if name == "B perfect":
            r.picture("trainers_mbz")

    # From a bounce (B pressed while the glove holds the ball; the ball then flies at 8): the aim starts from BOUNCE.
    yield from get_joined(r)
    for b in [("L",)] + [("UP",)] * 10 + [("UP", "L", "Z")] + [("UP", "Z")] * 3 + [("UP", "B", "Z"), ("UP", "Z"), ("UP", "Z")]:
        yield from stepper(b)
    if r.mode() != BOUNCE:
        emu.fail(f"the glove is in action {r.mode()}, not BOUNCE, in the air base state")
    state = emu.save("mbz_air")
    yield 3
    for name, (plan, verdict, aim, cancel, speed, n) in AIR_SCENARIOS.items():
        emu.hold("UP", "Z")
        emu.load(state)
        yield 3
        for b in plan:
            view = yield from stepper(b)
        a = view["mbz"][0]
        print(f"trainers: MBZ-L {name}: verdict {a['verdict']} n {a['n']} aim {a['aim']} cancel {a['cancel']} "
              f"V {a['v0']:.2f} speed {a['speed']:.2f} best {a['best']:.2f} +3 {a['speed3']:.2f} +6 {a['speed6']:.2f}")
        if a["verdict"] != MBV[verdict]:
            emu.fail(f"MBZ {name}: verdict {a['verdict']}, not {verdict}")
        if a["n"] != n:
            emu.fail(f"MBZ {name}: the verdict's number is {a['n']}, not {n}")
        if aim is not None and a["aim"] != aim:
            emu.fail(f"MBZ {name}: the aim shows on frame {a['aim']}, not {aim}")
        if speed is not None and (abs(a["v0"] - 8.0) > 0.05 or abs(a["speed"] - speed) > 0.05):
            emu.fail(f"MBZ {name}: speed {a['v0']:.2f} to {a['speed']:.2f}, docs/game.md gives 8.00 to {speed}")
        if speed is not None and abs(a["best"] - 14.0) > 0.05:
            emu.fail(f"MBZ {name}: the best speed is {a['best']:.2f}, not 14.00")
    emu.w8(r.run + RUN_TRAINERS, 0)
