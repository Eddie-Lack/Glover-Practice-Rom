"""Plays a fixed route and records the game's state on every video frame, for faithful.py.

The route: the logos without input, Start to the title screen, the level select (reached
by writing gameInfo.mode, as the game's unused TitleScreen did), Atlantis 1 with fixed pseudo-random input, then
the end of the level (newLevExitTimer = 1 makes CheckForExits call ExitLevel), the screens after it, and the
start of Atlantis 2.

Each step waits for the game to reach a state (a level, a mode, a frame count), not for a fixed video frame.
In a level, the input is chosen by the level's game frame (frameCount), as in a recording. So the game gets the
same input on the same game frame even when a load takes a video frame more or less.

While a level runs, every RAM_EVERY game frames, it also records the lower 4 MB of RAM (ramcheck.py).

With --features off (the default) the script turns off every practice feature on the first frame, if the ROM is
the practice ROM: every HUD item, and the Easy ball controls (settings.ballNormal). The in-game timer fix stays
on; this route plays no level with a bonus timer, so it changes nothing here."""
import json
import os
import pickle
import random
import struct

import numpy

import layout
import ramcheck

ATLANTIS1 = 10
ATLANTIS2 = 11
PRESENT = 43
FLYTHRU = 44
LEVEL_EXIT = 1
LEVEL_FRAMES = 620      # game frames of Atlantis 1 before its end is forced
NEXT_LEVEL_FRAMES = 120  # game frames of Atlantis 2 to record
RAM_EVERY = 50  # game frames between two RAM checks
LIMIT = 12000  # video frames; the route ends well before

BUTTONS = ["A", "B", "Z", "L", "R", "CUP", "CDOWN", "CLEFT", "CRIGHT", "UP", "DOWN", "LEFT", "RIGHT"]


def level_input(seed=1):
    """{game frame: (buttons, stick x, stick y)}: a new random input every 1 to 8 game frames."""
    rng = random.Random(seed)
    out = {}
    f = 25
    while f < LEVEL_FRAMES:
        buttons = rng.sample(BUTTONS, rng.choice([0, 0, 1, 1, 2]))
        x = rng.choice([0, 0, -80, 80, rng.randint(-80, 80)])
        y = rng.choice([0, 80, 80, -80, rng.randint(-80, 80)])
        out[f] = (buttons, x, y)
        f += rng.randint(1, 8)
    return out


FIELDS = ["frameCount", "mode", "level", "exitFlags", "rseed", "levelFrames"] + [
    f"{a}.{p}.{c}" for a in ("hand", "ball") for p in ("pos", "vel") for c in "xyz"]


# The two frame buffers (cfb0, cfb1: 320 by 240, 16 bits a pixel). Each video frame's sample also holds a coarse
# picture of each: the mean red, green and blue of 8 by 6 blocks of 40 by 40 pixels, in 1/8ths of a 5-bit level.
# Text, a missing object or a moved camera change some blocks by several levels. The draw thread's timing can move
# a texture by a fraction of a texel (docs/testing.md), which changes a block by much less than a level.
FRAME_BUFFERS = ("cfb0", "cfb1")
FRAME_BUFFER_SIZE = 320 * 240 * 2
BLOCK = 40


def picture(raw):
    """The coarse picture of a frame buffer, as 144 numbers (8 by 6 blocks, red, green, blue)."""
    # The core keeps RDRAM in 32-bit words of the host's byte order: each word holds two big-endian pixels.
    words = numpy.frombuffer(raw, dtype="<u4")
    pixels = numpy.empty(words.size * 2, dtype=numpy.uint32)
    pixels[0::2] = words >> 16
    pixels[1::2] = words & 0xFFFF
    pixels = pixels.reshape(240, 320)
    out = []
    for shift in (11, 6, 1):
        level = ((pixels >> shift) & 31).astype(numpy.float32)
        blocks = level.reshape(240 // BLOCK, BLOCK, 320 // BLOCK, BLOCK).mean(axis=(1, 3))
        out += [int(round(x * 8)) for x in blocks.ravel()]
    return out


def unpack(field, value):
    if ".pos." in field or ".vel." in field:
        return struct.unpack(">f", value.to_bytes(4, "big"))[0]
    return value


def run(emu, sym, args):
    gi = sym("gameInfo")
    hand, ball, player = sym("hand"), sym("ball"), sym("player")
    frame_count, rseed = sym("frameCount"), sym("rseed")
    settings = sym("settings") if sym.has("settings") else None
    inputs = level_input()
    masked = ramcheck.masked_ranges(sym)
    buffers = [sym(name) for name in FRAME_BUFFERS]
    trace = []
    ram = {}
    step = "flythrough"
    wait = 0  # video frames into the current step
    held = None

    def hold(*buttons, x=0, y=0):
        nonlocal held
        if held != (buttons, x, y):
            emu.hold(*buttons, x=x, y=y)
            held = (buttons, x, y)

    for f in range(1, LIMIT + 1):
        mode, level, fc = emu.r8(gi), emu.r8(gi + 1), emu.r32(frame_count)
        wait += 1
        if f == 1 and settings is not None:
            # The random input holds the stick fully in a direction with D-pad presses, which fires the
            # default bindings (a reset, a savestate). The route turns them off in both runs.
            for i in layout.SETTINGS_BINDINGS:
                emu.w8(settings + i, 0)
        if f == 1 and args.features == "off" and settings is not None:
            emu.w8(settings + layout.SETTINGS_BALL_NORMAL, 1)
            for i in range(layout.HUD_COUNT):
                emu.w8(settings + layout.SETTINGS_HUD + layout.HUD_PLACE_SIZE * i, 0)
        if step == "flythrough":
            # Start during the flythrough would open the title menu, and from there the level select does not
            # start a level. So Start comes during the logos.
            if level == PRESENT and mode == 4 and fc >= 200:
                step, wait = "start", 0
        elif step == "start":
            # One short Start skips the logos. The title screen is the flythrough (level FLYTHRU, gameInfo.mode
            # 4); the level select opens from it.
            hold("START") if wait <= 6 else hold()
            if level == FLYTHRU and mode == 4 and fc >= 60:
                emu.w8(gi, 2)  # gameInfo.mode: the level select
                step, wait = "select", 0
        elif step == "select":
            if wait == 200:
                emu.w32(sym("selectedLevel"), ATLANTIS1)
            if 400 <= wait < 440:
                hold("START")
            elif wait >= 440:
                hold()
            if level == ATLANTIS1 and mode == 4 and fc < 5:
                step, wait = "level", 0
        elif step == "level":
            if fc in inputs:
                b, x, y = inputs[fc]
                hold(*b, x=x, y=y)
            if fc >= LEVEL_FRAMES:
                hold()
                emu.w8(gi + 0x11, LEVEL_EXIT)  # gameInfo.exitType
                emu.w16(sym("newLevExitTimer"), 1)
                step, wait = "exit", 0
        elif step == "exit":
            # The end of level screen: A now and then.
            hold("A") if wait % 300 < 10 and wait > 300 else hold()
            if level == ATLANTIS2 and mode == 4 and fc >= 1:
                hold()
                step, wait = "next", 0
        elif step == "next":
            if fc >= NEXT_LEVEL_FRAMES:
                break
        state = [fc, mode, level, emu.r8(gi + 0x54), emu.r32(rseed), emu.r32(player + 0x18)]
        for actor in (hand, ball):
            # pos and vel, as raw words: a float compares exactly
            state += [emu.r32(actor + o) for o in (0x34, 0x38, 0x3C, 0x58, 0x5C, 0x60)]
        for a in buffers:
            state += picture(emu.raw(a, FRAME_BUFFER_SIZE))
        if trace and fc != trace[-1][0] and fc % RAM_EVERY == 0 and mode == 4 and step in ("level", "next"):
            ram[f"{level}:{fc}"] = ramcheck.snapshot(emu, masked)
        trace.append(state)
        if f % 1000 == 0:
            emu.shot("f%05d" % f)
        yield 1
    else:
        emu.fail(f"the route did not finish in {LIMIT} video frames (step {step})")
    with open(os.path.join(args.out, "trace.json"), "w") as fp:
        json.dump(trace, fp)
    with open(os.path.join(args.out, "ram.pickle"), "wb") as fp:
        pickle.dump(ram, fp)
