"""Frame advance, frame step and slow motion count whole game frames.

RunGame calls the practice code for every message of the main queue, and only some messages run a game frame
(docs/game.md). In Atlantis 1, reached with Warp, Go from the PRESS START title:

1. Frame advance on (stick down + D-pad up): the game stops. Ten frame steps (stick down + D-pad right), each a
   short press with a gap, run exactly ten game frames. The step binding held for 100 video frames repeats.
2. Slow motion (stick down + D-pad left steps through its choices): over 600 video frames, 1/2, 1/3 and 1/4 run
   1/2, 1/3 and 1/4 of the game frames that full speed runs (within one game frame).
3. Frame advance turned on during a level reset's fade: the load goes on, and the game stops on a game frame of
   the new level, where a step runs one game frame."""
import startlevel
from tools import press, wait_for

DOWN = dict(x=0, y=-80)
UP = dict(x=0, y=80)


def run(emu, sym, args):
    fc, fc2 = sym("frameCount"), sym("frameCount2")
    gi = sym("gameInfo")
    run_ = sym("run")
    frame = lambda: emu.r32(fc)  # noqa: E731
    frame2 = lambda: emu.r32(fc2)  # noqa: E731

    yield from startlevel.level(emu, sym)

    # 1. Frame advance and steps.
    yield from press(emu, "UP", **DOWN)
    yield 30
    if not emu.r8(run_):
        emu.fail("frame advance did not turn on")
    stopped = frame()
    yield 60
    if frame() != stopped:
        emu.fail(f"frame advance did not stop the game ({stopped} to {frame()})")
    for n in range(1, 11):
        yield from press(emu, "RIGHT", **DOWN)
        yield 12
        if frame() != stopped + n:
            emu.fail(f"after {n} frame steps the game ran {frame() - stopped} game frames, not {n}")
    print("10 frame steps ran 10 game frames")
    before = frame()
    emu.hold("RIGHT", **DOWN)
    yield 100
    emu.release()
    yield 20
    held = frame() - before
    print(f"the step binding held for 100 video frames ran {held} game frames")
    if held < 3:
        emu.fail(f"a held frame step ran {held} game frames: it does not repeat")
    yield from press(emu, "UP", **DOWN)
    yield 20
    if emu.r8(run_):
        emu.fail("frame advance did not turn off")

    # 2. Slow motion.
    counts = []
    for speed in range(4):
        if emu.r8(run_ + 2) != speed:
            emu.fail(f"slow motion is {emu.r8(run_ + 2)}, not {speed}")
        start = frame2()
        yield 600
        counts.append(frame2() - start)
        yield from press(emu, "LEFT", **DOWN)
        yield 10
    print("game frames in 600 video frames, off, 1/2, 1/3, 1/4:", counts)
    for speed in (1, 2, 3):
        want = counts[0] / (speed + 1)
        if abs(counts[speed] - want) > 1.5:
            emu.fail(f"slow motion 1/{speed + 1} ran {counts[speed]} game frames where {want:.1f} were due")

    # 3. Frame advance during a reset's fade.
    yield from press(emu, "UP", **UP)
    yield 6
    yield from press(emu, "UP", **DOWN)
    # The fade-out runs game frames, which stop; the held step goes through them, and the load between runs on.
    emu.hold("RIGHT", **DOWN)
    yield from wait_for(emu, lambda: emu.r8(gi) == 8, 3000, "the reset's load under frame advance")
    emu.release()
    yield from wait_for(emu, lambda: emu.r8(gi) == 4 and emu.r8(gi + 1) == 10, 3000,
                        "the reset's level to start under frame advance")
    yield 60
    stopped = frame()
    yield 60
    if frame() != stopped or emu.r8(gi) != 4:
        emu.fail(f"after the reset, frame advance did not stop the level ({stopped} to {frame()}, "
                 f"mode {emu.r8(gi)})")
    yield from press(emu, "RIGHT", **DOWN)
    yield 12
    if frame() != stopped + 1:
        emu.fail(f"a frame step after the reset ran {frame() - stopped} game frames")
    yield from press(emu, "UP", **DOWN)
    yield 30
