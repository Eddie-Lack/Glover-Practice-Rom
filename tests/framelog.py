"""The frame log in Atlantis 1. With it on, A is held for a few game frames: the log has one entry for each
game frame of the glove, numbered in order, with A held on consecutive entries and pressed on the first, and the
glove's action changes after the press. It also shows how many controller reads came between two frames of the
glove. Then the view opens (Inspect, Frame log). Writes log0.png and log1.png (needs --gfx angrylion)."""
import struct

import trace
from tools import C_ALL, press, wait_for

RUN_FRAME_LOG = 0x31  # Run.frameLog (practice/include/actions.h)
ENTRY = struct.Struct(">IHHHhhhhBB")  # FrameLogEntry (practice/include/framelog.h)
LOG_SIZE = 64
CONT_A = 0x8000


def read_log(emu, sym):
    base = sym("frameLog")
    count = emu.r32(base + LOG_SIZE * ENTRY.size)
    kept = min(count, LOG_SIZE)
    data = emu.read(base, LOG_SIZE * ENTRY.size)
    entries = []
    for age in range(kept - 1, -1, -1):
        i = (count - 1 - age) % LOG_SIZE
        entries.append(ENTRY.unpack_from(data, i * ENTRY.size))
    return entries


def run(emu, sym, args):
    gi = sym("gameInfo")
    fc = lambda: emu.r32(sym("frameCount"))  # noqa: E731
    mode = lambda: emu.r8(gi)  # noqa: E731
    level = lambda: emu.r8(gi + 1)  # noqa: E731

    yield from wait_for(emu, lambda: level() == trace.PRESENT and fc() >= 200, 4000, "the logos")
    yield from press(emu, "START", frames=6)
    yield from wait_for(emu, lambda: level() == trace.FLYTHRU and mode() == 4 and fc() >= 30, 3000, "the title")
    yield from press(emu, *C_ALL)
    for b in ("A", "DOWN", "DOWN", "A"):
        yield from press(emu, b)
    yield from wait_for(emu, lambda: level() == trace.ATLANTIS1 and mode() == 4 and fc() >= 100, 3000, "Atlantis 1")

    emu.w8(sym("run") + RUN_FRAME_LOG, 1)
    yield 60
    yield from press(emu, "A", frames=25)
    yield 60
    log = read_log(emu, sym)
    frames = [e[0] for e in log]
    if len(log) < 30 or frames != list(range(frames[0], frames[0] + len(frames))):
        emu.fail(f"the log holds {len(log)} entries, not in order: {frames[:10]}")
    held = [i for i, e in enumerate(log) if e[1] & CONT_A]
    presses = [i for i, e in enumerate(log) if e[2] & CONT_A]
    if len(held) < 3 or held != list(range(held[0], held[0] + len(held))):
        emu.fail(f"A is held on entries {held}, not on a run of 3 or more")
    if presses != [held[0]]:
        emu.fail(f"A is pressed on entries {presses}, not only on the first held one ({held[0]})")
    modes_after = {e[4] for e in log[held[0] + 1:held[0] + 6]}
    if modes_after == {log[held[0]][4]}:
        emu.fail(f"the glove's action stayed {log[held[0]][4]} after the press")
    reads = {}
    for e in log:
        reads[e[8]] = reads.get(e[8], 0) + 1
    print(f"framelog: controller reads between two frames of the glove: {sorted(reads.items())}")
    # Every entry after the first saw exactly one read (the first counts from when the log was turned on).
    if any(e[8] != 1 for e in log[1:]):
        emu.fail(f"controller reads between two frames of the glove: {sorted(reads.items())}")

    # The view: Inspect (6th item of the menu), Frame log (4th).
    yield from press(emu, *C_ALL)
    for b in ("DOWN",) * 5 + ("A", "DOWN", "DOWN", "DOWN", "A"):
        yield from press(emu, b)
    yield 10
    if emu.r32(sym("menuView")) != sym("frameLogView"):
        emu.fail("the frame log view did not open")
    if args.gfx == "angrylion":
        emu.fb_png("log0", sym("cfb_16_a"))
        emu.fb_png("log1", sym("cfb_16_b"))
    yield from press(emu, "B")
    yield from press(emu, *C_ALL)
