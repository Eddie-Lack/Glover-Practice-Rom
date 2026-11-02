"""Memory corruption tools (practice/include/corrupt.h) in Atlantis 1.

1. Off at the start: nothing running, no marker (the test turns the timer off, so the marker is the only text).
2. The memory view (Inspect, Memory): Z marks the word at its cursor as a poke (once); Start opens the pokes page;
   right changes the mode to freeze. B and the second B go back.
3. Freeze: a word that the game never reads (titleMenuSelected) is written again after the test clears it, the
   marker is drawn, the write count rises. Off: it stays as the test leaves it. The marker stays (something was
   written), also after a reset of the level, until "Clear all pokes" is used or a state is loaded.
4. Once: "Apply once pokes" in the menu writes each once poke in order (a word, then a byte over it); the writes
   count, an unaligned address is aligned, and a poke on the practice segment, below RDRAM or past it is not
   written.
5. Random (the low 8 bits) and flip (one bit of the low 4), every game frame and every 5th.
6. Flip one random bit, for each region (glove, ball, level's actors, a range of 16 bytes from the memory view's
   address): with the game stopped (frame advance), exactly one bit of the region is different afterwards, and
   it is the bit that the menu reports. The test puts the bit back. It sets the tool's seed so that an actor's bit
   is in its numbers: a flip in a list link or a pointer can hang the game.
7. Clear all pokes empties the list. The game keeps running."""
import random

import layout
import trace
from tools import C_ALL, press, wait_for

# Corrupt (practice/include/corrupt.h)
POKE_SIZE = 0x10
P_ADDRESS, P_VALUE, P_COUNT, P_MODE, P_SIZE, P_BITS, P_PERIOD = 0x00, 0x04, 0x08, 0x0C, 0x0D, 0x0E, 0x0F
C_RNG, C_SEEDED = 0x80, 0x96
C_FLIPS, C_LAST_ADDRESS, C_LAST_BIT, C_REGION, C_RANGE, C_REQUESTS, C_ACTIVE, C_DIRTY = (
    0x88, 0x8C, 0x90, 0x91, 0x92, 0x93, 0x94, 0x95)
OFF, ONCE, FREEZE, RANDOM, FLIP = range(5)
REGION_GLOVE, REGION_BALL, REGION_ACTORS, REGION_RANGE = range(4)
RUN_FRAME_ADVANCE = 0x00  # Run.frameAdvance
PRACTICE_TEXT_FRAMES = 0x08  # Practice.textFrames
ACTOR_SIZE = 0xE4
SAFE_FIRST, SAFE_END = 0x30, 0xC8  # an actor's numbers (radius, place, speed, rotation); the rest are links and flags
ACTION_SAVE_STATE, ACTION_LOAD_STATE = 8, 9
TIMER321 = None  # the memory view's first address


def xorshift(x):
    """The tool's generator (practice/src/corrupt.c)."""
    x ^= (x << 13) & 0xFFFFFFFF
    x ^= x >> 17
    x ^= (x << 5) & 0xFFFFFFFF
    return x & 0xFFFFFFFF


def safe_seed(region, actors, rng):
    """A seed for which the tool flips a bit in the numbers of an actor, not in a pointer or a flag: a flip in a
    list link or a pointer hangs the game, which is what the tool is for but not what this test wants. The ranges
    are not actors; any seed does."""
    while True:
        seed = rng.getrandbits(32) | 1
        if region == REGION_RANGE:
            return seed
        x = xorshift(seed)
        if region == REGION_ACTORS:
            x = xorshift(x)  # the pick of the actor came first
        byte = x % ((ACTOR_SIZE - 12) * 8) // 8
        byte += 12 if byte >= 0x10 else 0
        if SAFE_FIRST <= byte < SAFE_END:
            return seed


def run(emu, sym, args):
    gi = sym("gameInfo")
    fc = lambda: emu.r32(sym("frameCount"))  # noqa: E731
    mode = lambda: emu.r8(gi)  # noqa: E731
    level = lambda: emu.r8(gi + 1)  # noqa: E731
    cor = sym("corrupt")
    run_ = sym("run")
    poke = lambda i, off: cor + POKE_SIZE * i + off  # noqa: E731
    text_frames = lambda: emu.r32(sym("practice") + PRACTICE_TEXT_FRAMES)  # noqa: E731
    quiet = sym("titleMenuSelected")  # a word that nothing reads or writes in a level
    timer321 = sym("timer321")

    def set_poke(i, address, value, mode_, size=4, bits=None, period=0):
        emu.w32(poke(i, P_ADDRESS), address)
        emu.w32(poke(i, P_VALUE), value)
        emu.w32(poke(i, P_COUNT), 0)
        emu.w8(poke(i, P_MODE), mode_)
        emu.w8(poke(i, P_SIZE), size)
        emu.w8(poke(i, P_BITS), bits or size * 8)
        emu.w8(poke(i, P_PERIOD), period)

    def shot(name):
        """Both frame buffers: one of them is the frame being drawn."""
        emu.fb_png(name, sym("cfb_16_b"))
        emu.fb_png(name + "_0", sym("cfb_16_a"))

    def marker_drawn(frames=30):
        before = text_frames()
        yield frames
        return text_frames() - before >= 2

    def open_corruption_menu():
        """The menu, Inspect (6th item; the menu opens on it after the first time), its last item."""
        yield from press(emu, *C_ALL)
        if not emu.r8(sym("menuOpen")):
            emu.fail("the menu did not open")
        for b in (("DOWN",) * 5 if not state["opened"] else ()) + ("A", "UP", "A"):
            yield from press(emu, b)
        state["opened"] = True

    state = {"opened": False}

    yield from wait_for(emu, lambda: level() == trace.PRESENT and fc() >= 200, 4000, "the logos")
    yield from press(emu, "START", frames=6)
    yield from wait_for(emu, lambda: level() == trace.FLYTHRU and mode() == 4 and fc() >= 30, 3000, "the title")
    yield from press(emu, *C_ALL)
    for b in ("A", "DOWN", "DOWN", "A"):  # Warp, Go
        yield from press(emu, b)
    yield from wait_for(emu, lambda: level() == trace.ATLANTIS1 and mode() == 4 and fc() >= 100, 3000, "Atlantis 1")
    emu.w8(sym("settings") + layout.SETTINGS_HUD, 0)  # the timer off: no text unless the marker is drawn
    yield 60

    # 1. Off.
    if emu.r8(cor + C_ACTIVE) or emu.r8(cor + C_DIRTY) or emu.r8(cor + C_REQUESTS):
        emu.fail("the corruption tools are not off at the start")
    if (yield from marker_drawn()):
        emu.fail("text is drawn with the tools off")

    # 2. The memory view: Z marks, Start opens the pokes page, right changes the mode.
    yield from press(emu, *C_ALL)
    for b in ["DOWN"] * 5 + ["A", "DOWN", "A"]:  # Inspect, Memory (2nd item)
        yield from press(emu, b)
    state["opened"] = True
    yield from press(emu, "Z")
    if emu.r32(poke(0, P_ADDRESS)) != timer321 or emu.r8(poke(0, P_MODE)) != ONCE or emu.r8(poke(0, P_SIZE)) != 4:
        emu.fail(f"Z marked {emu.r32(poke(0, P_ADDRESS)):#x} mode {emu.r8(poke(0, P_MODE))}, not timer321 once")
    if emu.r32(poke(0, P_VALUE)) != emu.r32(timer321):
        emu.fail("the poke does not hold the word's value")
    yield 10
    shot("poke_words")
    yield from press(emu, "START")
    yield 10
    shot("poke_page")
    yield from press(emu, "RIGHT")
    if emu.r8(poke(0, P_MODE)) != FREEZE or emu.r8(cor + C_ACTIVE) != 1:
        emu.fail(f"right did not make the poke freeze: mode {emu.r8(poke(0, P_MODE))}, active {emu.r8(cor + C_ACTIVE)}")
    yield from press(emu, "CUP")  # bits 32 -> 1
    if emu.r8(poke(0, P_BITS)) != 1:
        emu.fail(f"C up gave {emu.r8(poke(0, P_BITS))} bits, not 1")
    yield from press(emu, "CDOWN")  # back to 32
    yield from press(emu, "R")
    if emu.r8(poke(0, P_PERIOD)) != 1:
        emu.fail("R did not step the period")
    yield from press(emu, "L")
    yield from press(emu, "LEFT")  # freeze -> once
    yield from press(emu, "LEFT")  # -> off
    yield from press(emu, "B")  # back to the words
    yield from press(emu, "B")  # closes the view
    yield from press(emu, *C_ALL)
    if emu.r8(sym("menuOpen")):
        emu.fail("the menu did not close")
    # Poke 0 is off and still sits at timer321: the game thread must not write it.
    emu.w32(poke(0, P_ADDRESS), 0)
    emu.w32(poke(0, P_VALUE), 0)
    emu.w8(cor + C_ACTIVE, 0)

    # 3. Freeze.
    emu.w32(quiet, 0)
    set_poke(0, quiet, 0x00C0FFEE, FREEZE)
    emu.w8(cor + C_ACTIVE, 1)
    yield 30
    if emu.r32(quiet) != 0x00C0FFEE:
        emu.fail(f"the frozen word is {emu.r32(quiet):#x}")
    emu.w32(quiet, 0)
    yield 12
    if emu.r32(quiet) != 0x00C0FFEE:
        emu.fail("the frozen word was not written again")
    if emu.r32(poke(0, P_COUNT)) < 3:
        emu.fail(f"the frozen poke counts {emu.r32(poke(0, P_COUNT))} writes")
    if not (yield from marker_drawn()):
        emu.fail("no marker with a poke running")
    emu.w8(poke(0, P_MODE), OFF)
    emu.w8(cor + C_ACTIVE, 0)
    emu.w32(quiet, 0)
    yield 30
    if emu.r32(quiet) != 0:
        emu.fail("the word was written with the poke off")
    if not (yield from marker_drawn()):
        emu.fail("the marker went out while the level still holds a write")
    # A reset of the level (stick up + D-pad up) keeps the marker: a write to a global or the save outlives it.
    yield from press(emu, "UP", x=0, y=80)
    yield from wait_for(emu, lambda: fc() < 20, 2000, "the level to restart")
    yield from wait_for(emu, lambda: level() == trace.ATLANTIS1 and mode() == 4 and fc() >= 60, 3000, "Atlantis 1")
    if not emu.r8(cor + C_DIRTY) or not (yield from marker_drawn()):
        emu.fail("the reset of the level cleared the marker")
    # "Clear all pokes" clears it.
    yield from open_corruption_menu()
    for b in ("DOWN",) * 7 + ("A",):
        yield from press(emu, b)
    yield from press(emu, *C_ALL)
    yield 30
    if emu.r8(cor + C_DIRTY) or (yield from marker_drawn()):
        emu.fail("the marker stays after Clear all pokes")

    # 4. Once pokes, through the menu's "Apply once pokes".
    emu.w32(quiet, 0)
    set_poke(1, quiet, 0x12345678, ONCE)
    set_poke(2, quiet, 0xAB, ONCE, size=1)
    set_poke(3, quiet + 2, 0xBEEF, OFF, size=2)  # off: not written
    set_poke(4, 0x80400100, 0x11111111, ONCE)  # the practice segment
    set_poke(5, sym("practice_BSS_END") - 4, 0x22222222, ONCE)  # its last word
    set_poke(6, 0x7FFFFFFC, 0x33333333, ONCE)  # below RDRAM
    set_poke(7, 0x80800000, 0x44444444, ONCE)  # past RDRAM
    practice_before = emu.read(0x80400100, 4), emu.read(sym("practice_BSS_END") - 4, 4)
    yield 10
    if emu.r32(quiet) != 0:
        emu.fail("a once poke was written without a request")
    yield from open_corruption_menu()
    yield from press(emu, "DOWN")  # Apply once pokes
    yield from press(emu, "A")  # the menu closes
    yield 20
    if emu.r32(quiet) != 0xAB345678:
        emu.fail(f"the once pokes left {emu.r32(quiet):#x}, not 0xAB345678")
    if emu.r32(poke(1, P_COUNT)) != 1 or emu.r32(poke(2, P_COUNT)) != 1 or emu.r32(poke(3, P_COUNT)) != 0:
        emu.fail("the once pokes' counts are wrong")
    for i in (4, 5, 6, 7):
        if emu.r32(poke(i, P_COUNT)) != 0:
            emu.fail(f"poke {i} (outside what is allowed) counts a write")
    if (emu.read(0x80400100, 4), emu.read(sym("practice_BSS_END") - 4, 4)) != practice_before:
        emu.fail("a poke changed the practice segment")
    if not emu.r8(cor + C_DIRTY) or not (yield from marker_drawn()):
        emu.fail("no marker after a once poke")
    # A state load clears the marker (the load puts back what the writes changed).
    requests = sym("requests")
    for action in (ACTION_SAVE_STATE, ACTION_LOAD_STATE):
        emu.w16(requests, emu.r16(requests) | 1 << action)
        yield 30
    if emu.r8(cor + C_DIRTY):
        emu.fail("the marker stays after a state load")
    # An unaligned address is made aligned.
    emu.w32(quiet, 0)
    set_poke(1, quiet + 2, 0xCAFEBABE, ONCE)
    emu.w32(poke(2, P_ADDRESS), 0)
    emu.w8(cor + C_REQUESTS, 1)
    yield 20
    if emu.r32(quiet) != 0xCAFEBABE or emu.r32(poke(1, P_ADDRESS)) != quiet:
        emu.fail(f"the unaligned poke left {emu.r32(quiet):#x} at {emu.r32(poke(1, P_ADDRESS)):#x}")

    # 5. Random and flip.
    for i in range(1, 8):
        emu.w32(poke(i, P_ADDRESS), 0)
        emu.w8(poke(i, P_MODE), OFF)
    emu.w32(quiet, 0)
    set_poke(0, quiet, 0xFFFF0000, RANDOM, bits=8)
    emu.w8(cor + C_ACTIVE, 1)
    seen = set()
    for _ in range(90):
        yield 1
        v = emu.r32(quiet)
        if v & 0xFFFFFF00 != 0xFFFF0000 and v & 0xFFFFFF00 != 0:
            emu.fail(f"random changed more than the low 8 bits: {v:#x}")
        seen.add(v & 0xFF)
    if len(seen) < 8 or emu.r32(poke(0, P_COUNT)) < 15:
        emu.fail(f"random gave {len(seen)} values in {emu.r32(poke(0, P_COUNT))} writes")
    # The first write keeps the value's high bits: 0xFFFF0000 stays above the low 8 bits.
    emu.w32(quiet, 0xFFFF0000)
    yield 6
    if emu.r32(quiet) & 0xFFFFFF00 != 0xFFFF0000:
        emu.fail(f"random did not keep the other bits: {emu.r32(quiet):#x}")
    emu.w8(poke(0, P_MODE), OFF)
    emu.w32(quiet, 0)
    set_poke(0, quiet, 0, FLIP, bits=4, period=0)
    emu.w8(cor + C_ACTIVE, 1)
    seen = set()
    for _ in range(90):
        yield 1
        v = emu.r32(quiet)
        if v & ~0xF:
            emu.fail(f"flip changed a bit above the low 4: {v:#x}")
        seen.add(v)
    if len(seen) < 3 or emu.r32(poke(0, P_COUNT)) < 15:
        emu.fail(f"flip gave {sorted(seen)} in {emu.r32(poke(0, P_COUNT))} writes")
    fast = emu.r32(poke(0, P_COUNT))
    # Every 5th game frame: about a fifth of the writes.
    set_poke(0, quiet, 0, FLIP, bits=4, period=2)
    f0 = fc()
    yield 150
    slow, frames = emu.r32(poke(0, P_COUNT)), fc() - f0
    print(f"corrupt: flip every game frame {fast} writes in 90 video frames; every 5th: {slow} in {frames} game frames")
    if not frames // 5 - 2 <= slow <= frames // 5 + 2:
        emu.fail(f"every 5th game frame gave {slow} writes in {frames} game frames")
    emu.w8(poke(0, P_MODE), OFF)
    emu.w8(cor + C_ACTIVE, 0)

    # 6. Flip one random bit, with the game stopped.
    emu.w32(poke(0, P_ADDRESS), 0)
    emu.w8(poke(0, P_MODE), OFF)
    emu.w8(run_ + RUN_FRAME_ADVANCE, 1)
    yield 30

    def actor_addresses():
        head = sym("actorList")
        out, p = [], emu.r32(head)
        while p != head and len(out) < 400:
            out.append(p)
            p = emu.r32(p)
        return out

    def snapshot(region):
        if region == REGION_GLOVE:
            return [(sym("hand"), ACTOR_SIZE)]
        if region == REGION_BALL:
            return [(sym("ball"), ACTOR_SIZE)]
        if region == REGION_ACTORS:
            return [(a, ACTOR_SIZE) for a in actor_addresses()]
        return [(timer321 & ~15, 16)]

    flips = {}
    rng = random.Random(1)
    for region in (REGION_GLOVE, REGION_BALL, REGION_ACTORS, REGION_RANGE, REGION_ACTORS, REGION_GLOVE):
        yield from press(emu, *C_ALL)
        if not emu.r8(sym("menuOpen")):
            emu.fail("the menu did not open")
        yield 4
        areas = snapshot(region)
        before = [emu.read(a, n) for a, n in areas]
        count0 = emu.r32(cor + C_FLIPS)
        # Inspect, Memory corruption, Flip region (3rd item) until it is the region, then Flip one random bit.
        for b in ("A", "UP", "A", "DOWN", "DOWN"):
            yield from press(emu, b)
        for _ in range(4):
            if emu.r8(cor + C_REGION) == region:
                break
            yield from press(emu, "A")
        if emu.r8(cor + C_REGION) != region:
            emu.fail(f"the menu did not set the region {region}")
        if region == REGION_GLOVE and not flips:
            yield 5
            shot("corrupt_menu")
        emu.w32(cor + C_RNG, safe_seed(region, len(areas), rng))
        emu.w8(cor + C_SEEDED, 1)
        for b in ("DOWN", "DOWN", "A"):
            yield from press(emu, b)
        yield 20
        if emu.r32(cor + C_FLIPS) != count0 + 1:
            emu.fail(f"region {region}: {emu.r32(cor + C_FLIPS) - count0} flips, not 1")
        after = [emu.read(a, n) for a, n in areas]
        diffs = []
        for (a, n), b0, b1 in zip(areas, before, after):
            for k in range(n):
                x = b0[k] ^ b1[k]
                for bit in range(8):
                    if x >> bit & 1:
                        diffs.append((a + k, bit))
        if len(diffs) != 1:
            where = emu.r32(cor + C_LAST_ADDRESS)
            base = next((a for a, n in areas if a <= where < a + n), None)
            emu.fail(f"region {region}: {len(diffs)} bits differ: {diffs[:6]}; the tool flipped {where:#x} bit "
                     f"{emu.r8(cor + C_LAST_BIT)} (offset {where - base if base else None} in its area, "
                     f"{len(areas)} areas), now {emu.read(where, 1)[0]:#x}")
        if diffs[0] != (emu.r32(cor + C_LAST_ADDRESS), emu.r8(cor + C_LAST_BIT)):
            emu.fail(f"region {region}: the bit is {diffs[0]}, the menu says "
                     f"{emu.r32(cor + C_LAST_ADDRESS):#x} bit {emu.r8(cor + C_LAST_BIT)}")
        if region != REGION_RANGE:
            # The draw thread writes an actor's distNext, distPrev and distSq again each frame: not flipped.
            offset = (diffs[0][0] - next(a for a, n in areas if a <= diffs[0][0] < a + n))
            if 0x10 <= offset < 0x1C:
                emu.fail(f"region {region}: the flip is in the distance list's fields (offset {offset:#x})")
        flips[region] = flips.get(region, 0) + 1
        if not emu.r8(cor + C_DIRTY):
            emu.fail("no marker state after a flip")
        # Put the bit back.
        emu._put(diffs[0][0], bytes([emu.read(diffs[0][0], 1)[0] ^ (1 << diffs[0][1])]))
        if emu.r8(sym("menuOpen")):
            emu.fail("the menu stayed open after the flip")
    print(f"corrupt: one bit flipped for each of the regions, {flips}")
    emu.w8(run_ + RUN_FRAME_ADVANCE, 0)
    yield 30

    # 7. Clear all pokes (the menu's last item); the game goes on.
    set_poke(3, quiet, 5, ONCE)
    emu.w8(cor + C_ACTIVE, 0)
    yield from press(emu, *C_ALL)
    for b in ("A", "UP", "A") + ("DOWN",) * 7 + ("A",):
        yield from press(emu, b)
    yield from press(emu, *C_ALL)
    if any(emu.r32(poke(i, P_ADDRESS)) for i in range(8)):
        emu.fail("Clear all pokes left a poke")
    f1 = fc()
    yield 120
    if fc() - f1 < 20 or level() != trace.ATLANTIS1 or mode() != 4:
        emu.fail(f"the game did not keep running: game frame {f1} to {fc()}, level {level()}, mode {mode()}")
