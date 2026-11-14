"""Loads what tests/sdrecording.py saved (run that first), in another emulator run (a power-off in between).

1. In another level: the best times are empty. A damaged BESTS.GRB (one byte changed) is refused and the times stay
   empty. The good file loads: the three times are back.
2. Load recording from card, through the menu: the level is Atlantis 1, the recording holds the same game frames and
   the same input as before, and Play recording gives the same game on every game frame as the first run's playback.
3. A recording file whose header says more than RECORDING_MAX game frames (30 minutes) is refused before the game's
   memory changes, with its own message. A recording file with a damaged byte is refused before the game's memory changes. The card code writes nothing in
   any of this (the test changes bytes of the image itself)."""
# The ROM with the test mailbox for a cart (make testrom).
TEST_BUILD = True

import json
import os
import shutil

import layout
import trace
from menunav import goto as menu_goto
from sdutil import Mailbox, named_file_place, sha
from stateutil import hud_message
from tools import C_ALL, press, wait_for

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
FIRST = os.path.join(ROOT, "build", "tests", "sdrecording")
ACTION_PLAY = 11
ACTION_WARP = 1
RUN_WARP_LEVEL = 4
RECORDING_BUFFER = 0x807DC000
OTHER_LEVEL = 30
RECORDING_MAX = 36000     # practice/include/recording.h
HEADER_FRAMES = 24        # the header's game frames (u32, big-endian)


def run(emu, sym, args):
    image = os.path.join(args.out, "sd.img")
    shutil.copy(os.path.join(FIRST, "sd.img"), image)
    box = Mailbox(sym, image)
    box.start(emu)
    try:
        yield from scenario(emu, sym, args, box, image)
    finally:
        box.stop()


def scenario(emu, sym, args, box, image):
    first = json.load(open(os.path.join(FIRST, "recording.json")))
    n, inputs = first["frames"], bytes.fromhex(first["inputs"])
    want = {f: {tuple(s) for s in states} for f, states in first["trace"]}
    bests = {int(k): v for k, v in first["bests"].items()}
    gi, fc_addr = sym("gameInfo"), sym("frameCount")
    hand, ball, player = sym("hand"), sym("ball"), sym("player")
    requests = sym("requests")
    rec = sym("recording")
    mode = lambda: emu.r8(gi)  # noqa: E731
    level = lambda: emu.r8(gi + 1)  # noqa: E731
    fc = lambda: emu.r32(fc_addr)  # noqa: E731
    done = lambda: emu.r32(sym("cardDone"))  # noqa: E731
    playing = lambda: emu.r8(rec + 1)  # noqa: E731
    count = lambda: emu.r32(rec + 4)  # noqa: E731
    times = lambda: {k: emu.r16(sym("practiceBests") + 2 * k) for k in range(30) if emu.r16(sym("practiceBests") + 2 * k)}  # noqa: E731

    def state():
        s = [emu.r32(sym("rseed")), emu.r32(player + 0x18)]
        for actor in (hand, ball):
            s += [emu.r32(actor + o) for o in (0x34, 0x38, 0x3C, 0x58, 0x5C, 0x60)]
        return tuple(s)

    def menu(*labels):
        emu.release()
        yield 30
        for attempt in range(4):
            yield from press(emu, *C_ALL, frames=20)
            if emu.r8(sym("menuOpen")):
                break
            print(f"menu: attempt {attempt + 1} did not open it")
            yield 40
        if not emu.r8(sym("menuOpen")):
            emu.fail("the menu did not open")
        yield from menu_goto(emu, sym, *labels)

    def flip(name, offset):
        """Changes one byte of a file on the card (the first run's copy is not touched)."""
        lba, _ = named_file_place(image, name)
        with open(image, "r+b") as f:
            f.seek(lba * 512 + offset)
            b = f.read(1)
            f.seek(lba * 512 + offset)
            f.write(bytes([b[0] ^ 0xFF]))
        box.switch(image)

    def patch(name, offset, data):
        """Writes DATA into a file on the card; returns the bytes it replaced."""
        lba, _ = named_file_place(image, name)
        with open(image, "r+b") as f:
            f.seek(lba * 512 + offset)
            old = f.read(len(data))
            f.seek(lba * 512 + offset)
            f.write(data)
        box.switch(image)
        return old

    for i in layout.SETTINGS_BINDINGS:
        emu.w8(sym("settings") + i, 0)
    emu.w8(sym("sdBackend"), 1)
    start_hash = sha(image)
    yield from wait_for(emu, lambda: level() == trace.PRESENT and fc() >= 200, 4000, "the logos")
    yield from press(emu, "START", frames=6)
    yield from wait_for(emu, lambda: level() == trace.FLYTHRU and mode() == 4 and fc() >= 30, 3000, "the title")
    emu.w8(sym("run") + RUN_WARP_LEVEL, OTHER_LEVEL)
    emu.w16(requests, emu.r16(requests) | 1 << ACTION_WARP)
    yield from wait_for(emu, lambda: level() == OTHER_LEVEL and mode() == 4 and 40 <= fc() < 400, 4000, "another level")

    def load_bests():
        before = done()
        yield from menu("Settings", "Load best times from card")
        yield from wait_for(emu, lambda: done() > before, 3000, "the load of the best times")
        yield 3
        yield from press(emu, "B")
        yield from press(emu, "B")
        yield from press(emu, *C_ALL)

    # 1. The best times.
    if times():
        emu.fail("the best times are not empty after a boot")
    flip("BESTS.GRB", 10)
    yield from load_bests()
    if times() or hud_message(emu, sym) != "the best times file is not usable":
        emu.fail(f"a damaged file: times {times()}, \"{hud_message(emu, sym)}\"")
    flip("BESTS.GRB", 10)
    yield from load_bests()
    if times() != bests or hud_message(emu, sym) != "loaded the best times from the card":
        emu.fail(f"the good file: times {times()}, \"{hud_message(emu, sym)}\"")
    print(f"sdrecording load: the best times are back: {times()}")

    # 2 (first). A recording file with a damaged byte in its runs: refused, the game goes on in the level.
    def load_recording():
        before = done()
        yield from menu("Savestate", "SD card", "Load recording from card")
        yield from wait_for(emu, lambda: done() > before, 6000, "the load of the recording")
        yield 3
        box.check(emu)
        yield 10

    # The header says RECORDING_MAX + 1 game frames: refused at once, the level and the buffer stay.
    marker = RECORDING_BUFFER + RECORDING_MAX * 4
    emu.w32(marker, 0xDEADBEEF)
    old = patch("RECORD.GRC", HEADER_FRAMES, (RECORDING_MAX + 1).to_bytes(4, "big"))
    yield from load_recording()
    if (level() != OTHER_LEVEL or count() != 0 or emu.r32(marker) != 0xDEADBEEF
            or hud_message(emu, sym) != "the recording file is longer than 30 minutes: not loaded"):
        emu.fail(f"a recording file of too many frames: level {level()}, {count()} frames, \"{hud_message(emu, sym)}\"")
    patch("RECORD.GRC", HEADER_FRAMES, old)
    print("sdrecording load: a recording file longer than 30 minutes is refused")

    _, size = named_file_place(image, "RECORD.GRC")
    flip("RECORD.GRC", size - 9)
    yield from load_recording()
    if level() != OTHER_LEVEL or count() != 0 or hud_message(emu, sym) != "the recording file is not usable":
        emu.fail(f"a damaged recording file: level {level()}, {count()} frames, \"{hud_message(emu, sym)}\"")
    flip("RECORD.GRC", size - 9)

    # 2. The recording file.
    yield from load_recording()
    message = hud_message(emu, sym)
    ticks, wait = emu.r32(sym("cardLastTicks")), emu.r32(sym("cardLastWait"))
    print(f"sdrecording load: level {level()}, {count()} frames, {ticks / 46875:.0f} ms of which {wait / 46875:.0f} ms "
          f"waiting for the cart; \"{message}\"")
    if level() != trace.ATLANTIS1 or count() != n or not message.startswith("loaded the recording: "):
        emu.fail(f"the recording file: level {level()}, {count()} frames, \"{message}\"")
    if bytes(emu.read(RECORDING_BUFFER, n * 4)) != inputs:
        emu.fail("the loaded recording holds other input")
    emu.release()
    emu.w16(requests, emu.r16(requests) | 1 << ACTION_PLAY)
    yield from wait_for(emu, lambda: playing() == 1, 2000, "the playback to start")
    seen = {}
    while playing():
        seen.setdefault(fc(), set()).add(state())
        yield 1
    bad = [f for f in sorted(seen) if f in want and not seen[f] & want[f]]
    common = [f for f in seen if f in want]
    print(f"sdrecording load: playback on {len(seen)} game frames, {len(common)} also seen in the first run, {len(bad)} differ")
    if bad:
        emu.fail(f"the playback differs from the first run at game frame {bad[0]}")
    if len(common) < n - 12:
        emu.fail(f"only {len(common)} of {n} game frames were seen in both runs")

    if box.writes != 0:
        emu.fail(f"{box.writes} sectors were written by the loads")
    print("sdrecording load: ok")
