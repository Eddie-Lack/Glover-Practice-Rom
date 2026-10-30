"""The Moon Pound aim (practice/include/trainers.h) at the hub's Space portal.

1. With the glove in JUMP above the dome's tip: the strip, the fall column, the hole's centre and the distance that the
   trainer shows follow the model (tools/tricks/moonpound_trainer.py, cone.py) on every frame, and the vertex is the tip.
2. Following the aim: the model finds a start where the shift that the aim shows makes a later cell clip. From a savestate,
   the glove moved by that shift and Z pressed on that cell falls through the dome; with no shift it lands on it.
3. Grey cells for A and for the action lock; silent in water, with Hercules, and with the trainer off.
4. The stick that the aim shows moves the column onto the hole (within 30 percent).

Pictures: trainers2_moon.png and trainers2_moon_inside.png (needs --gfx angrylion). TRAINERS_DEBUG=1 prints every frame."""
import math
import os
import struct
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "tools", "tricks"))
import cone  # noqa: E402  the hole candidates (tools/tricks/cone.py)
import layout  # noqa: E402
import moonpound  # noqa: E402  the contact test (tools/tricks/moonpound.py)
import moonpound_trainer  # noqa: E402  the aim's model (tools/tricks/moonpound_trainer.py)
import trace  # noqa: E402
from tools import press, wait_for  # noqa: E402
from trainers import ACTOR_POS, ACTOR_ROT, ACTOR_VEL, RUN_TRAINERS, TR_MOON, Rig, step  # noqa: E402
from trainers2 import read_view  # noqa: E402
from trainerlog import log_state  # noqa: E402

TIP = (-7.3284173, 60.6500015, -1347.3238525)   # the Space portal dome's tip in the hub levels (docs/game.md)
HUB = 1
HAND_JUMP, HAND_SLAM = 2, 18
ACTOR_FLAGS, ACTOR_INST, ACTOR_FACES, ACTOR_STATE_P = 0x28, 0xC8, 0xE0, 0xD4
MOON_STATE = []
CELL = {0: "GREY", 1: "LAND", 2: "CLIP", 3: "NO_Z", 4: "VOID"}


def boot_to_hub(emu, sym, level=HUB):
    """From power on to a hub level, through the level select (a debug screen: gameInfo.mode 2)."""
    gi = sym("gameInfo")
    fc = lambda: emu.r32(sym("frameCount"))  # noqa: E731
    yield from wait_for(emu, lambda: emu.r8(gi + 1) == trace.PRESENT and fc() >= 200, 4000, "the logos")
    yield from press(emu, "START", frames=6)
    yield from wait_for(emu, lambda: emu.r8(gi + 1) == trace.FLYTHRU and emu.r8(gi) == 4 and fc() >= 60, 3000, "the title")
    emu.w8(gi, 2)
    yield 200
    emu.w32(sym("selectedLevel"), level)
    emu.hold("START")
    yield 40
    emu.release()
    yield from wait_for(emu, lambda: emu.r8(gi + 1) == level and emu.r8(gi) == 4 and fc() >= 30, 3000, "the hub")
    emu.w8(sym("settings") + layout.SETTINGS_HUD, 0)
    emu.w8(sym("settings") + layout.SETTINGS_TRAINER_HOLD, 1)  # the result hold off: these tests read the live views


def read_faces(r, near, radius):
    """The collision faces of the landscape and the static platforms around NEAR (x, z), as the model's Face objects,
    read from RAM in the order that CheckCollisions walks them."""
    e, sym = r.emu, r.sym
    out = []

    def actor_faces(a, tag):
        inst, faces = e.r32(a + ACTOR_INST), e.r32(a + ACTOR_FACES)
        if not inst or not faces:
            return
        mesh = e.r32(e.r32(inst + 0xC) + 0x14)
        n = e.r16(mesh)
        pos = (e.rf(a + ACTOR_POS), e.rf(a + ACTOR_POS + 8))
        if math.hypot(pos[0] - near[0], pos[1] - near[1]) > radius + e.rf(a + 0x30):
            return
        for j in range(n):
            raw = bytes(e.read(faces + 0x78 * j, 0x78))
            fl = struct.unpack(">29f", raw[:0x74])
            d = dict(plane=list(fl[0:4]), v=[list(fl[4 + 3 * k:7 + 3 * k]) for k in range(3)],
                     edgeDir=[list(fl[13 + 3 * k:16 + 3 * k]) for k in range(3)], edgeLen=list(fl[22:25]),
                     proj=list(fl[25:29]), terrain=raw[0x74], axis=[raw[0x75], raw[0x76]])
            out.append(moonpound.Face(d, "%s%08X" % (tag, a), j))

    head = sym("collisionList")
    a, n = e.r32(head + 8), 0
    while a != head and a and n < 2000:
        actor_faces(a, "L")
        a, n = e.r32(a + 8), n + 1
    head = sym("platformList") + 4
    p, n = e.r32(head), 0
    while p != head and p and n < 500:
        if not e.r32(p + 0x14) & 0x84:
            actor_faces(p + 0x80, "P")
        p, n = e.r32(p), n + 1
    return out


def hand_state(r):
    """What the hook sees: the glove's position, velocity and first sphere's radius."""
    e = r.emu
    obj = e.r32(r.hand + 0xD8)
    return dict(pos=tuple(r.vec(r.hand + ACTOR_POS)), vel=tuple(r.vec(r.hand + ACTOR_VEL)), rad=e.rf(obj + 0x58 + 0x18),
                mode=r.mode(), ground=e.r8(obj + 0x188), lock=e.r16s(r.sym("handControl") + 8),
                atime=e.r16s(r.sym("handControl") + 0x1A), yaw=e.rf(r.cam + ACTOR_ROT + 4))


def put_hand(r, pos, vel, mode=HAND_JUMP):
    """Puts the glove (and the ball with it) at POS in the given action, between two game frames."""
    e = r.emu
    for a in (r.hand, r.ball):
        for off in (ACTOR_POS, ACTOR_POS + 12, 0x4C):
            r.setvec(a + off, pos)
        r.setvec(a + ACTOR_VEL, vel)
    e.w16(e.r32(r.hand + ACTOR_STATE_P) + 4, mode)


def step_len(r, buttons=(), x=0, y=0):
    """Like step(), for frames that may be long: waits for the frame counter to change and returns how far it moved."""
    f = r.fc()
    r.emu.hold(*buttons, x=x, y=y)
    for _ in range(300):
        if r.fc() != f:
            return r.fc() - f
        yield 1
    r.emu.fail("the game frame counter does not advance")


def model_moon(r, faces, cands, st):
    """The model's strip and aim for the state that the hook saw."""
    e = r.emu
    g = e.rf(r.sym("gravity"))
    friction = e.rf(e.r32(r.hand + 0xD0) + 0x18)
    moonpound_trainer.GRAVITY = g
    moonpound_trainer.M_JUMP = (1 - (1 - friction) / 4) * 0.7
    moonpound_trainer.M_SLAM = moonpound_trainer.M_JUMP * 0.5
    strip = moonpound_trainer.strip(st["pos"], st["vel"], faces, n=8, r0=st["rad"])
    guide = moonpound_trainer.guide(st["pos"], st["vel"], faces, cands, r0=st["rad"])
    return strip, guide


def moon_setup(r, offset=(0.9, -0.6), y=118.0, vy=8.0):
    """In the hub, the controls on, the glove in JUMP above the dome's tip. Returns the model's faces and candidates."""
    emu = r.emu
    emu.w8(r.run + RUN_TRAINERS, TR_MOON)
    for _ in range(40):
        yield from r.next_frame()
    for _ in range(300):
        if emu.r16s(r.sym("handControl") + 0x24) <= 0:
            break
        yield from r.next_frame()
    faces = read_faces(r, (TIP[0], TIP[2]), 200)
    cands = cone.candidates(faces)
    yield from r.next_frame()
    yield from warm_up(r, (TIP[0] + offset[0], y, TIP[2] + offset[1]), (0.0, vy, 0.0))
    return faces, cands


WARM = 14   # game frames that the aim needs to copy the faces and find the holes (they are found over several frames)


def warm_up(r, pos, vel):
    """Holds the glove in JUMP at POS for WARM game frames, then leaves it there. The aim has then built its buffer."""
    for _ in range(WARM):
        put_hand(r, pos, vel)
        yield from step_len(r, ())
        if os.environ.get("TRAINERS_DEBUG"):
            v = read_view(r)["moon"]
            print("warm", v["live"], "counts", v["counts"], "strip", v["strip_counts"], "faces", v["faces"], "cands", v["cands"], "flags", v["flags"], "builds", v["rebuilds"])
    put_hand(r, pos, vel)


def advance_state(S):
    pos, vel = moonpound_trainer.advance_jump(S["pos"], S["vel"])
    return dict(S, pos=pos, vel=vel)


def moon_check(r):
    """(d) The Moon Pound aim at the hub's Space portal."""
    emu = r.emu
    faces, cands = yield from moon_setup(r)
    print(f"trainers2: hub level {HUB}: {len(faces)} faces near the dome, {len(cands)} hole candidates in the model")
    yield from step(r, ())  # the hook of this frame ran before the glove was moved
    # The emulator runs on while Python works inside a frame. The model is run after the frames are recorded.
    rec = []
    for i in range(12):
        before = hand_state(r)
        adv = yield from step_len(r, ())
        rec.append((before, read_view(r)["moon"], adv))
    if os.environ.get('TRAINERS_DEBUG'):
        for b, v, a in rec:
            print('rec', [round(x, 2) for x in b['pos']], [round(x, 2) for x in b['vel']], 'view hover', round(v['hover'], 3), 'adv', a, 'live', v['live'])
    rows, bad_cells, grey_cells, worst = 0, 0, 0, dict(col=0.0, target=0.0, dist=0.0, zone=0.0)
    seen_clip = False
    for i, (before, v, adv) in enumerate(rec):
        if adv != 1:
            continue  # a long game frame: the next two ran together and the state in between was not sampled
        if not v["live"]:
            emu.fail(f"the Moon Pound aim is not live in JUMP above the dome (why {v['why']}, mode {before['mode']})")
        # The view belongs to the state that its frame started from. The harness samples the glove between video
        # frames. That state is found among the samples (and the passes after them) by the slam's hover height.
        g = r.emu.rf(r.sym("gravity"))
        match = None
        for later in [x[0] for x in rec]:
            st = later
            for _ in range(3):
                hover = st["pos"][1] + 3 * st["vel"][1] - 3 * g   # the slam's hover when Z is read two passes on
                if abs(hover - v["hover"]) < 0.01:
                    match = st
                    break
                st = advance_state(st)
            if match:
                break
        if match is None:
            emu.fail(f"frame {i}: the hover height {v['hover']:.3f} fits no state of the glove's jump")
        before = match
        strip, guide = model_moon(r, faces, cands, before)
        names = [CELL[c] for c in v["cell"]]
        if os.environ.get("TRAINERS_DEBUG"):
            print(i, names, strip, "col", [round(x, 3) for x in v["col"]], [round(x, 3) for x in guide["column"]],
                  "dist %.3f/%.3f" % (v["dist"], guide.get("dist", -1)), "zone", v["zone"], "phase %.2f" % v["phase"],
                  "calls", v["calls"], "iters", v["iters"], "sims", v["sims"], "counts", v["counts"], "faces", v["faces"], "cands", v["cands"],
                  "flags", v["flags"], "model phase", round(guide.get("phase_height", -1), 3), "before", [round(x, 3) for x in before["pos"]], [round(x, 3) for x in before["vel"]], adv, "target", [round(x, 3) for x in v["target"]], [round(x, 3) for x in guide["centre"]])
        # A cell that the frame had no time for is grey and the view says so (MOWF_SLOW); the others follow the model.
        for a, b in zip(names, strip):
            if a == "GREY":
                grey_cells += 1
                if not v["flags"] & 0x01:
                    emu.fail(f"frame {i}: a cell is grey with no slow flag (flags {v['flags']:#x})")
            else:
                bad_cells += a != b
        seen_clip |= "CLIP" in names
        worst["col"] = max(worst["col"], math.hypot(v["col"][0] - guide["column"][0], v["col"][1] - guide["column"][1]))
        if guide["target"] is None or not v["has"]:
            emu.fail("the aim finds no hole near the dome's tip")
        worst["target"] = max(worst["target"], math.hypot(v["target"][0] - guide["centre"][0],
                                                          v["target"][1] - guide["centre"][1]))
        if math.dist(v["vtx"], TIP) > 0.01 or math.dist(guide["target"], TIP) > 0.01:
            emu.fail(f"the aim names the vertex {v['vtx']}, not the dome's tip")
        if not v["zone"]:
            worst["dist"] = max(worst["dist"], abs(v["dist"] - guide["dist"]))
        elif guide.get("zone_dist") is not None:
            worst["zone"] = max(worst["zone"], abs(v["dist"] - guide["zone_dist"]))
        rows += 1
    if rows < 6:
        emu.fail(f"only {rows} frames were compared with the model")
    print(f"trainers2: {rows} frames: {bad_cells} of {rows * 8 - grey_cells} strip cells differ from the model ({grey_cells} "
          f"were left grey for time); the column differs by "
          f"{worst['col']:.4f}, the hole's centre by {worst['target']:.4f}, the distance by {worst['dist']:.4f}, "
          f"the searched distance by {worst['zone']:.3f}")
    r.picture("trainers2_moon")
    if grey_cells > rows * 8 // 2:
        emu.fail(f"{grey_cells} of {rows * 8} cells were left grey for time")
    if bad_cells > 2 or worst["col"] > 0.01 or worst["target"] > 0.02 or worst["dist"] > 0.02 or worst["zone"] > 0.1:
        emu.fail("the Moon Pound aim does not follow the model")


def clip_start(r, state, y, vy):
    """Loads the saved hub state and puts the glove in JUMP at (the tip + (0.9, -0.6), y) with lift vy. Two game frames
    later the view of the aim is for the state that the second frame started from."""
    emu = r.emu
    emu.load(state)
    yield 3
    for _ in range(3):
        yield from r.next_frame()
    yield from warm_up(r, (TIP[0] + 0.9, float(y), TIP[2] - 0.6), (0.0, vy, 0.0))


def clip_record(r, state, y, vy, n=10):
    """A run with no Z: the states that the frames started from, and the view of the second frame."""
    yield from clip_start(r, state, y, vy)
    states, view = [], None
    for k in range(n):
        states.append(hand_state(r))
        yield from step_len(r, ())
        if k == 1:
            view = read_view(r)["moon"]
    return states, view


def clip_run(r, state, y, vy, follow, z_cell):
    """The same start. With FOLLOW the glove is moved by the aim's shift after the second frame. Z is pressed so that it
    is read on the cell Z_CELL (1 to 7). Returns the glove's action and height after each following frame."""
    yield from clip_start(r, state, y, vy)
    yield from step_len(r, ())      # frame j + 1: the hook ran before the glove was moved
    yield from step_len(r, ())      # frame f: the first one with a view of the new state
    view = read_view(r)["moon"]
    if follow:
        r.shift_all(view["dx"], view["dz"])
    path = []
    for k in range(1, z_cell + 24):
        yield from step_len(r, ("Z",) if k == z_cell else ())
        path.append((r.mode(), r.vec(r.hand + ACTOR_POS)[1]))
        if follow and k == 2:
            r.picture('trainers2_moon_inside')
    r.emu.release()
    return path


def clip_check(r):
    """(d) Following the aim: the glove falls through the dome's tip. Not following: it lands on the dome."""
    emu = r.emu
    emu.w8(r.run + RUN_TRAINERS, TR_MOON)
    for _ in range(40):
        yield from r.next_frame()
    for _ in range(300):
        if emu.r16s(r.sym("handControl") + 0x24) <= 0:
            break
        yield from r.next_frame()
    faces = read_faces(r, (TIP[0], TIP[2]), 200)
    near = [f for f in faces if math.hypot(f.v[0][0] - TIP[0], f.v[0][2] - TIP[2]) < 70]
    rad = hand_state(r)["rad"]
    friction = emu.rf(emu.r32(r.hand + 0xD0) + 0x18)
    moonpound_trainer.GRAVITY = emu.rf(r.sym("gravity"))
    moonpound_trainer.M_JUMP = (1 - (1 - friction) / 4) * 0.7
    moonpound_trainer.M_SLAM = moonpound_trainer.M_JUMP * 0.5
    state = emu.save("moon_hub")
    MOON_STATE.append(state)
    yield 3
    # Look for a start from which the shift that the aim shows makes one of the later cells clip. The states are the
    # game's own, from runs with no Z. The model plays the cells; the aim's shift is the program's.
    found = None
    for vy in (0.0, 2.0, -2.0):
        for y in range(100, 124, 3):
            states, view = yield from clip_record(r, state, y, vy)
            if not view["live"] or not view["has"]:
                continue
            for c in range(1, 8):
                st = states[1 + c]  # the state that frame f + c starts from: its Z pass is frame f + c + 1
                moved = (st["pos"][0] + view["dx"], st["pos"][1], st["pos"][2] + view["dz"])
                verdict = moonpound_trainer.simulate(moved, st["vel"], near, rad)["verdict"]
                plain = moonpound_trainer.simulate(st["pos"], st["vel"], near, rad)["verdict"]
                if verdict == "CLIP" and plain == "LAND":
                    found = (y, vy, c, view)
                    if os.environ.get("TRAINERS_DEBUG"):
                        sim = moonpound_trainer.simulate(moved, st["vel"], near, rad)
                        print("state", [round(x, 3) for x in st["pos"]], [round(x, 3) for x in st["vel"]], "rad", st["rad"],
                              "lock", st["lock"], "mode", st["mode"])
                        print("model heights", [round(p[1], 3) for p in sim["P"]], "col", sim["column"], sim["passes"][-1]["crossed"],
                              sim["passes"][-1]["contacts"])
                    break
            if found:
                break
        if found:
            break
    if not found:
        emu.fail("no start was found from which the aim leads to a clip")
    y, vy, c, view = found
    print(f"trainers2: start at height {y} with lift {vy}; the aim shows {view['dx']:+.3f} {view['dz']:+.3f} "
          f"(distance {view['dist']:.2f}, phase {view['phase']:.2f}); the model says Z on cell {c} clips after that shift")
    results = {}
    for name, follow in (("following the aim", True), ("not following", False)):
        path = yield from clip_run(r, state, y, vy, follow, c)
        slam = any(m == HAND_SLAM for m, _ in path)
        low = min(h for _, h in path)
        results[name] = (low, slam)
        print(f"trainers2: {name}: slam {'yes' if slam else 'no'}, lowest height {low:.1f}")
        mp = [l for l in log_state(emu, r.sym)[3] if l[1].startswith("MP ")]
        want = "CLIP" if follow else "LAND"
        if not mp or want not in mp[0][1]:
            emu.fail(f"{name}: the trainer log's newest slam line is {mp[:1]}, not {want}")
        print(f"trainers2: {name}: the trainer log says {mp[0][1]!r}")
        if os.environ.get("TRAINERS_DEBUG"):
            print([(m, round(h, 1)) for m, h in path])
    if not (results["following the aim"][1] and results["following the aim"][0] < TIP[1] - 20):
        emu.fail("the glove did not fall through the dome after following the aim")
    if not (results["not following"][1] and results["not following"][0] > TIP[1]):
        emu.fail("the glove did not land on the dome when the aim was not followed")


def moon_quiet_check(r, state):
    """(d) Grey cells while A is held and the lift runs; grey cells while the action lock runs; silent in water, with the
    Hercules power-up and with the trainer off."""
    emu = r.emu
    hc = r.sym("handControl")

    def fresh(buttons=(), pokes=None):
        yield from clip_start(r, state, 110, 8.0)
        yield from step_len(r, ())
        for _ in range(2):
            if pokes:
                pokes()
            yield from step_len(r, buttons)
        return read_view(r)["moon"]

    v = yield from fresh()
    if not v["live"] or v["cell"][0] == 0:
        emu.fail(f"the strip is grey with no lock and no A: {v['cell']}")
    v = yield from fresh(pokes=lambda: emu.w16(hc + 8, 5))
    grey = [c == 0 for c in v["cell"]]
    # The cells that the frame had no time for are grey too, and the view says so; the aim's cell (3) is always played.
    if grey[:3] != [True] * 3 or grey[3] or (any(grey[4:]) and not v["flags"] & 0x01):
        emu.fail(f"an action lock of 5 greys the cells 0 to 2 only: {v['cell']} flags {v['flags']:#x}")
    v = yield from fresh(("A",), pokes=lambda: emu.w16(hc + 0x1A, 2))
    if not all(c == 0 for c in v["cell"]) or not v["flags"] & 0x08:
        emu.fail(f"A held while the lift runs greys every cell: {v['cell']} flags {v['flags']:#x}")
    v = yield from fresh(pokes=lambda: emu.w8(emu.r32(r.hand + ACTOR_STATE_P) + 9, 1))
    if v["live"] or v["why"] != 2:
        emu.fail(f"the aim is not silent in water: live {v['live']} why {v['why']}")
    player = r.sym("player")
    v = yield from fresh(pokes=lambda: emu.w32(player + 0x1C + 4 * 4, 100))
    emu.w32(player + 0x1C + 4 * 4, 0)
    if v["live"] or v["why"] != 3:
        emu.fail(f"the aim is not silent with the Hercules power-up: live {v['live']} why {v['why']}")
    emu.w8(r.run + RUN_TRAINERS, 0)
    d0 = r.diag()["hook_calls"]
    yield from fresh()
    if r.diag()["hook_calls"] != d0:
        emu.fail("the hook counted calls with the trainer off")
    emu.w8(r.run + RUN_TRAINERS, TR_MOON)
    print("trainers2: Moon Pound aim: grey for A and for the action lock; silent in water, with Hercules and when off")


def moon_stick_check(r, state):
    """(d) The stick that the aim shows moves the column onto the hole: one tap, then the column six frames later."""
    emu = r.emu
    yield from clip_start(r, state, 100, 14.0)
    yield from step_len(r, ())
    yield from step_len(r, ())
    before = read_view(r)["moon"]
    if not before["has"] or abs(before["stick"][0]) > 79 or abs(before["stick"][1]) > 79:
        emu.fail(f"no stick advice to test: {before['stick']}")
    yield from step_len(r, (), x=int(round(before["stick"][0])), y=int(round(before["stick"][1])))
    for _ in range(6):
        yield from step_len(r, ())
    after = read_view(r)["moon"]
    emu.release()
    moved = (after["col"][0] - before["col"][0], after["col"][1] - before["col"][1])
    want = (before["dx"], before["dz"])
    err = math.hypot(moved[0] - want[0], moved[1] - want[1])
    print(f"trainers2: stick {before['stick'][0]:+.1f} {before['stick'][1]:+.1f}: the column moved {moved[0]:+.3f} {moved[1]:+.3f}, "
          f"the aim said {want[0]:+.3f} {want[1]:+.3f} (off by {err:.3f}); now {after['dist']:.3f} from the hole")
    if err > 0.3 * math.hypot(*want):
        emu.fail("the stick that the aim shows does not move the column onto the hole")


def run(emu, sym, args):
    r = Rig(emu, sym)
    yield from boot_to_hub(emu, sym)
    yield from moon_check(r)
    yield from clip_check(r)
    yield from moon_quiet_check(r, MOON_STATE[0])
    yield from moon_stick_check(r, MOON_STATE[0])
