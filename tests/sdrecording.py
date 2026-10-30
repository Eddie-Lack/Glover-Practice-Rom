"""An input recording with its state, and the practice best times, on the SD card (practice/src/card.c), with the test
mailbox in place of a cart.

1. In Atlantis 1 the recording is made with the real inputs of the harness: the stick forward, then to the side. Play
   recording runs it once, and the game frames of the playback are kept for the run that loads the file.
2. The best times are set (three levels) and saved through the menu: Settings, Save best times to card.
3. The recording is saved through the menu: Savestate, SD card, Save recording to card. The reader of
   tests/sdutil.py decodes RECORD.GRC (header, CRC, every chunk of the state, the runs) and finds the same input as
   the game's buffer. BESTS.GRB has its magic, its 30 times and a good CRC. fsck.fat is clean if installed.
tests/sdrecordingload.py loads both files in another emulator run (a power-off in between)."""
# The ROM with the test mailbox for a cart (make testrom).
TEST_BUILD = True

import json
import os
import struct
import zlib

import fatimage
import layout
import trace
from menunav import goto as menu_goto
from sdutil import Mailbox, parse_card_file, read_named_file
from stateutil import hud_message
from tools import C_ALL, press, wait_for

ACTION_RECORD, ACTION_PLAY = 10, 11
RECORDING_BUFFER = 0x80780000
FRAMES = 120
BESTS = {0: 1234, 7: 999, 29: 65535}


def run(emu, sym, args):
    image = os.path.join(args.out, "sd.img")
    fatimage.format_image(image, 64, 32, 1, True)
    box = Mailbox(sym, image)
    box.start(emu)
    try:
        yield from scenario(emu, sym, args, box, image)
    finally:
        box.stop()


def scenario(emu, sym, args, box, image):
    gi, fc_addr = sym("gameInfo"), sym("frameCount")
    hand, ball, player = sym("hand"), sym("ball"), sym("player")
    requests = sym("requests")
    rec = sym("recording")
    mode = lambda: emu.r8(gi)  # noqa: E731
    level = lambda: emu.r8(gi + 1)  # noqa: E731
    fc = lambda: emu.r32(fc_addr)  # noqa: E731
    done = lambda: emu.r32(sym("cardDone"))  # noqa: E731
    recording = lambda: emu.r8(rec)  # noqa: E731
    playing = lambda: emu.r8(rec + 1)  # noqa: E731
    count = lambda: emu.r32(rec + 4)  # noqa: E731

    def state():
        s = [emu.r32(sym("rseed")), emu.r32(player + 0x18)]
        for actor in (hand, ball):
            s += [emu.r32(actor + o) for o in (0x34, 0x38, 0x3C, 0x58, 0x5C, 0x60)]
        return tuple(s)

    def request(action):
        emu.w16(requests, emu.r16(requests) | 1 << action)

    for i in layout.SETTINGS_BINDINGS:
        emu.w8(sym("settings") + i, 0)
    emu.w8(sym("sdBackend"), 1)
    yield from wait_for(emu, lambda: level() == trace.PRESENT and fc() >= 200, 4000, "the logos")
    yield from press(emu, "START", frames=6)
    yield from wait_for(emu, lambda: level() == trace.FLYTHRU and mode() == 4 and fc() >= 30, 3000, "the title")
    yield from press(emu, *C_ALL)
    for b in ("A", "DOWN", "DOWN", "A"):  # Warp, Go
        yield from press(emu, b)
    yield from wait_for(emu, lambda: level() == trace.ATLANTIS1 and mode() == 4 and 40 <= fc() < 400, 3000, "Atlantis 1")

    # 1. Record with real inputs: the stick forward, then forward and right.
    emu.hold(x=0, y=80)
    request(ACTION_RECORD)
    yield from wait_for(emu, lambda: recording() == 1, 2000, "the recording to start")
    yield from wait_for(emu, lambda: count() >= FRAMES // 2, 2000, "half the recording")
    emu.hold(x=60, y=40)
    yield from wait_for(emu, lambda: count() >= FRAMES, 2000, "the recording")
    request(ACTION_RECORD)
    yield from wait_for(emu, lambda: recording() == 0, 2000, "the recording to stop")
    n = count()
    emu.release()
    inputs = bytes(emu.read(RECORDING_BUFFER, n * 4))
    stick = {(inputs[4 * i + 2], inputs[4 * i + 3]) for i in range(n)}
    print(f"sdrecording: {n} game frames recorded, input values (stick x, y) {sorted(stick)}")
    if len(stick) < 2:
        emu.fail("the recording holds one input only")
    request(ACTION_PLAY)
    yield from wait_for(emu, lambda: playing() == 1, 2000, "the playback to start")
    seen = {}
    while playing():
        seen.setdefault(fc(), set()).add(state())
        yield 1
    yield 20
    if len(seen) < n - 8:
        emu.fail(f"the playback was seen on {len(seen)} of {n} game frames")

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

    # 2. The best times, through the menu.
    for k, v in BESTS.items():
        emu.w16(sym("practiceBests") + 2 * k, v)
    before = done()
    yield from menu("Settings", "Save best times to card")
    yield from wait_for(emu, lambda: done() > before, 3000, "the save of the best times")
    yield 3
    box.check(emu)
    if hud_message(emu, sym) != "saved the best times to the card":
        emu.fail(f"the message after the best times: \"{hud_message(emu, sym)}\"")
    yield from press(emu, "B")
    yield from press(emu, "B")
    yield from press(emu, *C_ALL)

    # 3. The recording, through the menu. The menu closes by itself.
    before = done()
    yield from menu("Savestate", "SD card", "Save recording to card")
    yield from wait_for(emu, lambda: done() > before, 6000, "the save of the recording")
    yield 3
    box.check(emu)
    message = hud_message(emu, sym)
    print(f"sdrecording: {emu.r32(sym('cardLastBytes'))} bytes in {emu.r32(sym('cardLastTicks')) / 46875:.0f} ms; \"{message}\"")
    if not message.startswith("saved to card recording: "):
        emu.fail(f"the message after the recording: \"{message}\"")
    if emu.r8(sym("menuOpen")):
        emu.fail("the menu stayed open after Save recording to card")
    yield 10

    data = read_named_file(image, "RECORD.GRC")
    if data is None:
        emu.fail("no RECORD.GRC on the card")
    header, memory = parse_card_file(data)
    runs = data[len(data) - 4 - 5 * header["runs"]:len(data) - 4]
    frames = bytearray()
    for r in range(header["runs"]):
        b0, b1, sx, sy, length = runs[5 * r:5 * r + 5]
        frames += bytes((b0, b1, sx, sy)) * length
    print(f"sdrecording: RECORD.GRC {len(data)} bytes, level {header['level']}, {header['frames']} frames in "
          f"{header['runs']} runs, {header['chunks']} chunks decoded by the reader")
    if header["level"] != trace.ATLANTIS1 or header["frames"] != n or bytes(frames) != inputs:
        emu.fail("RECORD.GRC does not hold the recording that the game has")

    bests = read_named_file(image, "BESTS.GRB")
    if bests is None or len(bests) != 72 or bests[:5] != b"GPBT\x01" or zlib.crc32(bests[:-4]) != struct.unpack(">I", bests[-4:])[0]:
        emu.fail("BESTS.GRB is not as the format says")
    times = struct.unpack(">30H", bests[8:68])
    if {k: v for k, v in enumerate(times) if v} != BESTS:
        emu.fail(f"BESTS.GRB holds {times}")
    print("sdrecording: BESTS.GRB holds the three times, with a good CRC")

    img = fatimage.Image(image)
    try:
        img.check()
    finally:
        img.close()
    result = fatimage.fsck(image, 2048)
    if result is not None and result[0] != 0:
        emu.fail("fsck.fat: " + result[1][-300:])
    json.dump({"frames": n, "inputs": inputs.hex(), "bests": BESTS,
               "trace": [[f, sorted(map(list, s))] for f, s in sorted(seen.items())]},
              open(os.path.join(args.out, "recording.json"), "w"))
    print("sdrecording: ok")
