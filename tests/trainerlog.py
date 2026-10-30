"""The trainer log and the result hold (practice/include/trainers.h) in Atlantis 1.

1. MBZ-L: an attempt that ends adds a line to the log (newest first, "MB B 7.0>10.4 PERFECT"). A second attempt that
   starts and ends during the hold of the first keeps the first one's result on the panel while it runs, replaces the
   panel's result when it ends, and moves the end of the hold to 20 game frames after its own end (it does not queue a
   second hold). Once the hold is over, a new attempt shows as it runs.
2. L mash: runs of 5 presses make log lines ("LM 5P 4 ON TIME ..."). The panel keeps its view for the hold (off: it
   does not). The log survives a savestate load. The log length setting (off, 5, 6, 8, 10) changes how many lines are
   drawn (counted in the frame buffer, in the colour of the lines). With the MBZ-L panel on, the right side stops at 5.
3. Turning a trainer off takes its lines out of the log.

Picture: trainerlog.png (needs --gfx angrylion)."""
import os
import struct
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "tools", "tricks"))
from trainers import (ACTOR_POS, ACTOR_PREV_POS, ACTOR_VEL, MBV, RUN_NOCLIP, RUN_TRAINERS, Rig, boot_to_atlantis,  # noqa: E402,F401
                      get_joined, mk_plan, step)

TR_LMASH, TR_MBZL = 1, 4
SETTINGS_TRAINER_LOG, SETTINGS_TRAINER_HOLD = 0x4E, 0x4F    # stored from 1: log off, 5, 6, 8, 10; hold off, 1, 2, 3 s
LOG_LINE = 0x24
HOLD_FRAMES = 60                                           # "3 s"
HOLD_L = 20


def log_state(emu, sym):
    base = sym("trainerLog")
    begin, seq, total = struct.unpack(">III", bytes(emu.read(base, 12)))
    lines = []
    for i in range(10):
        raw = bytes(emu.read(base + 0x10 + i * LOG_LINE, LOG_LINE))
        color, trainer = struct.unpack_from(">IB", raw, 0)
        text = raw[5:].split(b"\0")[0].decode("latin-1")
        lines.append((trainer, text, color))
    return begin, seq, total, [l for l in lines if l[0]]


def green_lines(emu, sym, rows=10, x0=158, y0=48):
    """How many of the 9 pixel rows of the log's place hold a pixel of the lines' green (the newest are the first)."""
    colours = [len(set(bytes(emu.read(sym(f"cfb{b}"), 320 * 240 * 2))[::2])) for b in (0, 1)]
    raw = bytes(emu.read(sym(f"cfb{colours.index(max(colours))}"), 320 * 240 * 2))
    shown = 0
    for k in range(rows):
        found = False
        for y in range(y0 + 9 * k, y0 + 9 * k + 8):
            row = raw[(y * 320 + x0) * 2:(y * 320 + 320) * 2]
            for i in range(0, len(row), 2):
                v = (row[i] << 8) | row[i + 1]
                r, g, b = (v >> 11) & 31, (v >> 6) & 31, (v >> 1) & 31
                if g >= 28 and 9 <= r <= 15 and 9 <= b <= 15:
                    found = True
                    break
            if found:
                break
        shown += found
    return shown


def run(emu, sym, args):
    r = Rig(emu, sym)
    yield from boot_to_atlantis(emu, sym)
    yield 60
    yield from get_joined(r)
    settings = sym("settings")
    for name, check in (("mbz", mbz_check), ("lmash", lmash_check)):
        if name in os.environ.get("TRAINERS_ONLY", name).split(","):
            yield from check(r, settings)
    emu.w8(r.run + RUN_TRAINERS, 0)


def hold_until(emu, sym, bit_index):
    return struct.unpack(">i", bytes(emu.read(sym("holdUntil") + 4 * bit_index, 4)))[0]


def mbz_check(r, settings):
    emu, sym = r.emu, r.sym
    emu.w8(settings + SETTINGS_TRAINER_LOG, 2)
    emu.w8(settings + SETTINGS_TRAINER_HOLD, 4)
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

    for b in [("L",)] + [("UP",)] * 12 + [("UP", "L", "Z")] + [("UP", "Z")] * 5:
        yield from stepper(b)
    state = emu.save("log_base")
    yield 3
    emu.hold("UP", "Z")
    emu.load(state)
    yield 3
    # Attempt 1: B perfect (frames 0 to 7 of B, Z 0 to 6, L on 7). Attempt 2 starts on frame 12 after an L that
    # takes the glove back onto the ball (frame 10): B 12 to 19, Z 12 to 18, L on 20 (one frame late).
    a1 = mk_plan(10, "B", 8, range(7), 7)
    gap = [("UP", "L", "Z")] + [("UP", "Z")] * 4
    a2 = mk_plan(30, "B", 8, range(7), 8)
    plan = a1 + gap + a2
    rows = []
    totals = []
    for i, b in enumerate(plan):
        view = yield from stepper(b)
        _, _, total, lines = log_state(emu, sym)
        rows.append((i, view["mbz"][0] if view["mbz"] else None, total, hold_until(emu, sym, 2), r.fc(), r.mode()))
        totals.append(total)
    print("trainerlog: MBZ-L frames (frame, newest attempt verdict/done, log total, hold ends at):")
    for i, a, total, hu, fc, mode in rows:
        print(f"  {i:2d} {'-' if a is None else (a['verdict'], a['done'])} total {total} holdUntil {hu} fc {fc} mode {mode}")
    _, _, total, lines = log_state(emu, sym)
    if total != 2 or len(lines) != 2:
        emu.fail(f"the log holds {total} writes and {len(lines)} lines after two attempts, not 2")
    if not lines[0][1].startswith("MB B ") or not lines[1][1].startswith("MB B "):
        emu.fail(f"the lines are {lines}")
    if "PERFECT" not in lines[1][1] or "PERFECT" in lines[0][1]:
        emu.fail(f"the log is not newest first: {lines[0][1]!r}, then {lines[1][1]!r}")
    print(f"trainerlog: log, newest first: {lines[0][1]!r}, then {lines[1][1]!r}")
    first = next(i for i, t in enumerate(totals) if t >= 1)
    second = next(i for i, t in enumerate(totals) if t >= 2)
    if second <= first:
        emu.fail("the two attempts ended on the same frame")
    # While attempt 2 runs (after it starts, before it ends) the panel holds attempt 1's result.
    start2 = len(a1) + len(gap)
    during = [a for i, a, t, hu, fc, _m in rows if first < i < second and a is not None]
    held = [a for a in during if a["done"] and a["verdict"] == MBV["PERFECT"]]
    if not during or len(held) != len(during):
        emu.fail(f"during attempt 2 the panel did not hold attempt 1: {[(a['verdict'], a['done']) for a in during]}")
    print(f"trainerlog: the panel held attempt 1's result for {len(during)} frames while attempt 2 ran (it began on frame {start2})")
    # When attempt 2 ends its result is on the panel and the hold ends 20 frames after that end.
    a2view = rows[second][1]
    if a2view["verdict"] != MBV["L_LATE"]:
        emu.fail(f"attempt 2's result did not replace the panel's: verdict {a2view['verdict']}")
    hu1, hu2 = rows[first][3], rows[second][3]
    end1 = rows[first][4]
    end2 = rows[second][4]
    if not (HOLD_FRAMES - 2 <= hu1 - end1 <= HOLD_FRAMES + 1 and HOLD_FRAMES - 2 <= hu2 - end2 <= HOLD_FRAMES + 1):
        emu.fail(f"the hold ends {hu1 - end1} and {hu2 - end2} frames after the two ends, not {HOLD_FRAMES}")
    if hu2 - hu1 != end2 - end1 and abs((hu2 - hu1) - (end2 - end1)) > 1:
        emu.fail("the second attempt did not restart the hold")
    print(f"trainerlog: the hold ended {hu1 - end1} frames after attempt 1's end, then {hu2 - end2} after attempt 2's "
          f"(the second moved it from {hu1} to {hu2})")
    r.picture("trainerlog")
    # After the hold: a new attempt shows as it runs (the panel is not held). Wait for the end, then a B attempt with no Z.
    while r.fc() < hu2 + 2:
        yield from stepper(("UP",))
    yield from stepper(("UP", "L", "Z"))
    yield from stepper(("UP", "Z"))
    live = []
    for b in mk_plan(14, "B", 14, (), None):
        view = yield from stepper(b)
        if view["mbz"]:
            live.append((view["mbz"][0]["verdict"], view["mbz"][0]["done"]))
    if (MBV["L_LATE"], 1) in live[1:6]:
        emu.fail(f"after the hold the panel still shows attempt 2: {live}")
    print(f"trainerlog: after the hold the panel follows the new attempt: {live[:8]}")
    # With the hold off a result is not held.
    emu.w8(settings + SETTINGS_TRAINER_HOLD, 1)
    emu.hold("UP", "Z")
    emu.load(state)
    yield 3
    for b in a1 + [("UP", "Z")] * 3:
        yield from stepper(b)
    if hold_until(emu, sym, 2) > r.fc():
        emu.fail("the hold is on with the setting off")
    print("trainerlog: with Trainer result hold off nothing is held")
    emu.w8(settings + SETTINGS_TRAINER_HOLD, 2)
    emu.w8(r.run + RUN_TRAINERS, 0)
    yield 3


def lmash_check(r, settings):
    emu, sym = r.emu, r.sym
    emu.w8(settings + SETTINGS_TRAINER_LOG, 6)       # 10 lines
    emu.w8(settings + SETTINGS_TRAINER_HOLD, 2)
    emu.w8(r.run + RUN_TRAINERS, TR_LMASH)
    yield from get_joined(r)
    runs_done = 0
    strips = []
    # 11 runs of 5 presses, 2 frames apart, 24 frames from the start of one to the start of the next.
    for run_no in range(11):
        for i in range(24):
            view = yield from step(r, ("L",) if i < 10 and i % 2 == 0 else ())
            _, _, total, lines = log_state(emu, sym)
            strips.append((total, bytes(view["lmash"]["strip"])))
        if run_no == 2:
            # savestate load: the log stays
            before = log_state(emu, sym)[2]
            state = emu.save("log_lm")
            yield 3
            emu.load(state)
            yield 6
            if log_state(emu, sym)[2] != before:
                emu.fail("a savestate load changed the log")
            yield from get_joined(r)
            print("trainerlog: the log is the same after a savestate load")
    t0 = next(i for i, (t, _) in enumerate(strips) if t >= 1)
    frozen = all(strips[t0 + k][1] == strips[t0][1] for k in range(HOLD_L - 2))
    moved = strips[t0 + HOLD_L + 6][1] != strips[t0][1]
    if not frozen or not moved:
        emu.fail(f"the L mash panel is {'frozen' if frozen else 'not frozen'} for the hold and "
                 f"{'live' if moved else 'still frozen'} after it")
    print(f"trainerlog: the L mash panel kept its view for {HOLD_L - 2} frames after the run's end and then followed the game")
    _, _, total, lines = log_state(emu, sym)
    print(f"trainerlog: L mash runs {total}; newest lines {[l[1] for l in lines[:3]]}")
    if total < 10 or len(lines) != 10:
        emu.fail(f"{total} writes and {len(lines)} lines after 11 runs")
    if not all(l[1].startswith("LM 5P ") and "ON TIME" in l[1] for l in lines):
        emu.fail(f"an L mash line has an unexpected text: {[l[1] for l in lines]}")
    expected = {1: 0, 2: 5, 3: 6, 4: 8, 5: 10}
    for idx, want in expected.items():
        emu.w8(settings + SETTINGS_TRAINER_LOG, idx)
        for _ in range(4):
            yield from r.next_frame()
        got = green_lines(emu, sym)
        print(f"trainerlog: Trainer log length index {idx}: {got} lines drawn (want {want})")
        if got != want:
            emu.fail(f"the log length index {idx} draws {got} lines, not {want}")
    # With the MBZ-L panel on, the right side stops at 5 lines.
    emu.w8(settings + SETTINGS_TRAINER_LOG, 5)
    emu.w8(r.run + RUN_TRAINERS, TR_LMASH | TR_MBZL)
    emu.w8(sym("clearRequest"), TR_MBZL)      # the menu's toggle: the MBZ-L panel starts empty
    for _ in range(4):
        yield from r.next_frame()
    got = green_lines(emu, sym)
    if got != 5:
        emu.fail(f"with the MBZ-L panel on, {got} lines are drawn, not 5")
    print("trainerlog: with the MBZ-L panel on, 10 lines draw as 5")
    # Turning the trainer off takes its lines out (the menu's toggle writes run.trainers and clearRequest).
    emu.w8(r.run + RUN_TRAINERS, TR_MBZL)
    emu.w8(sym("clearRequest"), TR_LMASH)
    for _ in range(3):
        yield from r.next_frame()
    emu.w8(r.run + RUN_TRAINERS, TR_MBZL | TR_LMASH)
    for _ in range(3):
        yield from r.next_frame()
    if [l for l in log_state(emu, sym)[3] if l[0] == TR_LMASH]:
        emu.fail("the L mash lines are still in the log after the trainer was turned off")
    print("trainerlog: turning a trainer off takes its lines out of the log")
    emu.w8(settings + SETTINGS_TRAINER_LOG, 2)
    emu.w8(r.run + RUN_TRAINERS, 0)
    yield 3
