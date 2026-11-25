"""The places of the trainer panels and the trainer log on the screen (practice/src/trainers.c, "Drawing"), in Atlantis 1.

With every trick trainer on, the log at 10 lines and the hold at 3 s:

1. Idle (the glove holds the ball and stands still): no panel is drawn.
2. An MBZ-L B attempt, then an L mash run on the diagonal D-pad (the line keeper may latch too): on every game frame the
   boxes that the draw thread used (trainerLayout) lie in the area that the game's HUD leaves free (x 16 to 304, y 44 to
   192), and no two of them overlap: panels, log lines, or a panel and a log line. Panels show while the attempts go on
   and while their results are held, more than one at a time.
3. The log moved (run.logX and logY): its lines end at the new right edge and start at the new top, one under the
   other.

Picture: trainerlayout.png (needs --gfx angrylion)."""
import os
import struct
import sys

import layout as offsets

sys.path.insert(0, os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "tools", "tricks"))
from trainers import ACTOR_POS, RUN_TRAINERS, Rig, boot_to_atlantis, get_joined, mk_plan, step  # noqa: E402,F401

TR_TRICKS = 0x4F                                           # L mash, line keeper, MBZ-L, Moon Pound aim, wall clip
SETTINGS_TRAINER_LOG, SETTINGS_TRAINER_HOLD = 0x4E, 0x4F    # 5: 10 lines; 4: 3 s
BOXES = 18
AREA = (16, 44, 304, 192)


def layout(emu, sym):
    """(frames, panel boxes, log line boxes) of the draw thread's last frame."""
    base = sym("trainerLayout")
    raw = bytes(emu.read(base, 8 + BOXES * 8))
    frames, panels, lines = struct.unpack_from(">IBB", raw, 0)
    boxes = [struct.unpack_from(">4h", raw, 8 + 8 * i) for i in range(panels + lines)]
    return frames, boxes[:panels], boxes[panels:]


def overlap(a, b):
    return a[0] < b[2] and b[0] < a[2] and a[1] < b[3] and b[1] < a[3]


def check_boxes(emu, sym, where):
    """Fails when a box leaves the free area or two boxes overlap. Returns the number of panels."""
    _, panels, lines = layout(emu, sym)
    every = panels + lines
    for b in every:
        if b[0] < AREA[0] or b[1] < AREA[1] or b[2] > AREA[2] or b[3] > AREA[3] or b[0] >= b[2] or b[1] >= b[3]:
            emu.fail(f"{where}: the box {b} is outside the free area {AREA}; panels {panels}, log {lines}")
    for i in range(len(every)):
        for j in range(i + 1, len(every)):
            if overlap(every[i], every[j]):
                emu.fail(f"{where}: the boxes {every[i]} and {every[j]} overlap; panels {panels}, log {lines}")
    return len(panels)


def drawn(r):
    """Waits until the draw thread drew a frame after the game frame that just ran."""
    emu, sym = r.emu, r.sym
    f0 = layout(emu, sym)[0]
    for _ in range(20):
        yield 1
        if layout(emu, sym)[0] - f0 >= 2:
            return
    emu.fail("the trainers drew nothing for 20 video frames")


def run(emu, sym, args):
    r = Rig(emu, sym)
    yield from boot_to_atlantis(emu, sym)
    yield 60
    settings = sym("settings")
    emu.w8(settings + SETTINGS_TRAINER_LOG, 5)
    emu.w8(settings + SETTINGS_TRAINER_HOLD, 4)
    yield from get_joined(r)
    emu.w8(r.run + RUN_TRAINERS, TR_TRICKS)
    for _ in range(10):
        yield from step(r, ())
    yield from drawn(r)
    _, panels, lines = layout(emu, sym)
    if panels:
        emu.fail(f"idle trainers draw panels: {panels}")
    print(f"trainerlayout: idle, no panel ({len(lines)} log lines)")

    c0 = r.yaw0
    r.hold_camera(c0)
    pos = r.vec(r.ball + ACTOR_POS)
    home = [pos[0], pos[2]]
    most, frames, shot = 0, 0, False

    def stepper(buttons, where):
        nonlocal most, frames, shot
        yield from step(r, buttons)
        r.hold_camera(c0)
        p = r.vec(r.ball + ACTOR_POS)
        if abs(p[0] - home[0]) > 40 or abs(p[2] - home[1]) > 40:
            r.shift_all(home[0] - p[0], home[1] - p[2])
        yield from drawn(r)
        n = check_boxes(emu, sym, where)
        frames += 1
        if n > most:
            most = n
        if n >= 2 and not shot:
            shot = True
            r.picture("trainerlayout")

    # Z armed on the ball, then a perfect B attempt (as tests/trainerlog.py), then an L mash run on the diagonal.
    for b in [("L",)] + [("UP",)] * 12 + [("UP", "L", "Z")] + [("UP", "Z")] * 5:
        yield from stepper(b, "set-up")
    for k, b in enumerate(mk_plan(16, "B", 8, range(7), 7)):
        yield from stepper(b, f"MBZ-L frame {k}")
    for k in range(40):
        press = ("L",) if k < 24 and k % 2 == 0 else ()
        yield from stepper(("UP", "RIGHT") + press, f"L mash frame {k}")
    emu.release()
    _, panels, lines = layout(emu, sym)
    print(f"trainerlayout: {frames} game frames checked, up to {most} panels at once; at the end {len(panels)} panels "
          f"and {len(lines)} log lines")
    if most < 2:
        emu.fail(f"never more than {most} panel at once: the test did not cover two panels")

    # 3. The log moved: right edge x 224, top y 104.
    if not lines:
        emu.fail("no log lines at the end: the moved log cannot be checked")
    emu.w8(r.run + offsets.RUN_LOG_X, 112)
    emu.w8(r.run + offsets.RUN_LOG_Y, 52)
    yield from step(r, ())
    yield from drawn(r)
    _, _, moved = layout(emu, sym)
    if len(moved) < len(lines):
        emu.fail(f"the moved log has {len(moved)} lines, not {len(lines)}")
    for k, b in enumerate(moved):
        if b[2] != 224 or b[1] != 104 + 9 * k:
            emu.fail(f"line {k} of the moved log is at {b}, not ending at x 224 at y {104 + 9 * k}")
    print(f"trainerlayout: the moved log draws {len(moved)} lines from x 224, y 104")
    emu.w8(r.run + offsets.RUN_LOG_X, 152)
    emu.w8(r.run + offsets.RUN_LOG_Y, 22)
    emu.w8(r.run + RUN_TRAINERS, 0)
    emu.w8(settings + SETTINGS_TRAINER_HOLD, 2)
    emu.w8(settings + SETTINGS_TRAINER_LOG, 2)
    yield 3
