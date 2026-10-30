"""Chaos inputs (practice/include/chaos.h) in Atlantis 1, through the Game menu's fourth last item.

0. The test sets the generator's seed after the boot (chaos.rng and chaos.seeded). With seed 25 the first 700 rolls
   at "rare" decide on no change, so the hand-made changes of step 2 are the only ones and the counts are exact.
1. Off at the start: nothing counted, no marker (the test turns the timer off, so the marker is the only text).
2. Set to "rare" in the menu, then the two input changes by hand: a drop that the test arms (the next press of A
   does not reach the game, and the tool counts it), and a press that the test asks for (the game sees B pressed,
   though the test never presses B, and the input display's real input does not).
3. Set to "often" in the menu. The marker is drawn. A is tapped now and then. Within some seconds the tool has
   added presses, taken away presses and nudged the glove (its counters), the game has seen presses of buttons that
   the test never pressed, and the game keeps running in the same level.
4. While a recording is made (set by hand), the counters stand still.
5. Off again: the counters stop, the marker goes, and the game goes on."""
import layout
import trace
from menunav import goto, to_item
from tools import C_ALL, press, wait_for

CONT_A = 0x8000
RUN_CHAOS = 0x45  # Run.chaos
C_RNG, C_FRAMES, C_EVENTS, C_ADDED, C_DROPPED, C_POS, C_VEL, C_PENDING, C_ARMED = (
    0x00, 0x04, 0x08, 0x0C, 0x10, 0x14, 0x18, 0x1C, 0x1E)
PAD_BUTTON = 0x1A  # Pad.button[14]
BUTTON_A, BUTTON_B = 0, 1
CONT_B = 0x4000
C_SEEDED = 0x1F
# xorshift32 from this seed makes no change in its first 700 rolls at the "rare" rate (5 in 1000), which covers the
# game frames of step 2.
SEED = 25
PRACTICE_TEXT_FRAMES = 0x08  # Practice.textFrames
REC_RECORDING = 0x00  # Recording.recording
MAX_FRAMES = 9000


def run(emu, sym, args):
    gi = sym("gameInfo")
    fc = lambda: emu.r32(sym("frameCount"))  # noqa: E731
    mode = lambda: emu.r8(gi)  # noqa: E731
    level = lambda: emu.r8(gi + 1)  # noqa: E731
    run_ = sym("run")
    ch = sym("chaos")
    stat = lambda off: emu.r32(ch + off)  # noqa: E731
    pad = lambda b: emu.r8(sym("pad") + PAD_BUTTON + b)  # noqa: E731
    held = lambda: emu.r16(sym("input"))  # noqa: E731
    text_frames = lambda: emu.r32(sym("practice") + PRACTICE_TEXT_FRAMES)  # noqa: E731

    yield from wait_for(emu, lambda: level() == trace.PRESENT and fc() >= 200, 4000, "the logos")
    yield from press(emu, "START", frames=6)
    yield from wait_for(emu, lambda: level() == trace.FLYTHRU and mode() == 4 and fc() >= 30, 3000, "the title")
    yield from press(emu, *C_ALL)
    for b in ("A", "DOWN", "DOWN", "A"):  # Warp, Go
        yield from press(emu, b)
    yield from wait_for(emu, lambda: level() == trace.ATLANTIS1 and mode() == 4 and fc() >= 100, 3000, "Atlantis 1")
    emu.w8(sym("settings") + layout.SETTINGS_HUD, 0)  # the timer off: no text unless the marker is drawn
    yield 60
    # ChaosGameFrame seeds from the CPU counter on the first game frame. A set seed makes the run repeatable.
    emu.w32(ch + C_RNG, SEED)
    emu.w8(ch + C_SEEDED, 1)

    opened = [False]

    def set_chaos(rights):
        """Game, Chaos inputs, RIGHT presses from 'off' to the level."""
        yield from press(emu, *C_ALL)
        if not emu.r8(sym("menuOpen")):
            emu.fail("the menu did not open")
        yield from goto(emu, sym, "Game")
        yield from to_item(emu, sym, "Chaos inputs")
        for b in ("RIGHT",) * rights:
            yield from press(emu, b)
        opened[0] = True
        for b in ("B",):
            yield from press(emu, b)
        yield from press(emu, *C_ALL)
        yield 10

    def marker_drawn(frames=30):
        before = text_frames()
        yield frames
        return text_frames() - before >= 2

    # 1. Off.
    if emu.r8(run_ + RUN_CHAOS) or stat(C_FRAMES) or stat(C_EVENTS):
        emu.fail("chaos is not off at the start")
    if (yield from marker_drawn()):
        emu.fail("text is drawn with chaos off")

    # 2. Rare, and the two input changes by hand.
    yield from set_chaos(1)
    if emu.r8(run_ + RUN_CHAOS) != 1:
        emu.fail(f"the menu set chaos to {emu.r8(run_ + RUN_CHAOS)}, not 1 (rare)")
    if not (yield from marker_drawn()):
        emu.fail("no marker with chaos on")
    yield 60
    dropped0, added0 = stat(C_DROPPED), stat(C_ADDED)
    seen = 0
    emu.w8(ch + C_ARMED, 1)
    emu.hold("A")  # held for 30 video frames: this press is taken away
    for _ in range(30):
        yield 1
        seen += pad(BUTTON_A) != 0
    emu.release()
    # The press is hidden until A is let go; the armed drop is spent.
    if stat(C_DROPPED) != dropped0 + 1 or emu.r8(ch + C_ARMED):
        emu.fail(f"the armed drop did not act: dropped {stat(C_DROPPED) - dropped0}, armed {emu.r8(ch + C_ARMED)}")
    if seen:
        emu.fail("the game saw the press that chaos took away")
    yield 30
    emu.hold("A")
    yield 30
    if not pad(BUTTON_A):
        emu.fail("the game did not see the next press of A")
    emu.release()
    yield 30
    sawb = 0
    emu.w16(ch + C_PENDING, CONT_B)
    for _ in range(40):
        yield 1
        sawb |= pad(BUTTON_B) != 0
        if held() & CONT_B:
            emu.fail("the input display's real input shows B")
    if not sawb or stat(C_ADDED) != added0 + 1 or emu.r16(ch + C_PENDING):
        emu.fail(f"the asked-for press did not reach the game: saw {sawb}, added {stat(C_ADDED) - added0}")
    yield 60

    # 3. Often.
    yield from set_chaos(2)  # rare -> sometimes -> often
    if emu.r8(run_ + RUN_CHAOS) != 3:
        emu.fail(f"the menu set chaos to {emu.r8(run_ + RUN_CHAOS)}, not 3 (often)")
    if not (yield from marker_drawn()):
        emu.fail("no marker with chaos at often")
    base = [stat(o) for o in (C_FRAMES, C_EVENTS, C_ADDED, C_DROPPED, C_POS, C_VEL)]
    f0 = fc()
    saw_other = set()
    for i in range(MAX_FRAMES):
        if i % 24 == 0:
            emu.hold("A")
        elif i % 24 == 8:
            emu.release()
        yield 1
        for b in range(14):
            if b != BUTTON_A and pad(b):
                saw_other.add(b)
        if (stat(C_ADDED) > base[2] + 2 and stat(C_DROPPED) > base[3] and stat(C_POS) + stat(C_VEL) > base[4] + base[5]
                and saw_other and i > 1200):
            break
    emu.release()
    got = [stat(o) - b for o, b in zip((C_FRAMES, C_EVENTS, C_ADDED, C_DROPPED, C_POS, C_VEL), base)]
    print(f"chaos: often, {fc() - f0} game frames in {i + 1} video frames: rolled {got[0]}, events {got[1]}, added "
          f"{got[2]}, dropped {got[3]}, nudges {got[4]} (place) and {got[5]} (speed); the game saw buttons "
          f"{sorted(saw_other)} that the test never pressed")
    if got[2] < 3 or got[3] < 1 or got[4] + got[5] < 1:
        emu.fail(f"chaos did too little in {i + 1} video frames: {got}")
    if not saw_other:
        emu.fail("the game saw no press of a button that the test never pressed")
    if not 0.01 <= got[1] / max(got[0], 1) <= 0.12:
        emu.fail(f"{got[1]} events in {got[0]} game frames is not about one in twenty")
    if level() != trace.ATLANTIS1 or mode() != 4 or fc() - f0 < 100:
        emu.fail("the game did not keep running in Atlantis 1")
    if not (yield from marker_drawn()):
        emu.fail("no marker after the run")

    # 3b. While a recording is made, chaos does nothing (a nudge would not be in the recording).
    rec = sym("recording")
    emu.w8(rec + REC_RECORDING, 1)
    yield 10
    held_back = [stat(o) for o in (C_FRAMES, C_EVENTS, C_ADDED, C_DROPPED, C_POS, C_VEL)]
    yield 300
    still = [stat(o) for o in (C_FRAMES, C_EVENTS, C_ADDED, C_DROPPED, C_POS, C_VEL)]
    emu.w8(rec + REC_RECORDING, 0)
    if still != held_back:
        emu.fail(f"chaos acted while a recording was made: {held_back} to {still}")
    yield 120
    if stat(C_FRAMES) <= still[0]:
        emu.fail("chaos did not go on after the recording")

    # 4. Off again.
    yield from set_chaos(1)  # often -> off (wraps)
    if emu.r8(run_ + RUN_CHAOS) != 0:
        emu.fail(f"the menu set chaos to {emu.r8(run_ + RUN_CHAOS)}, not 0")
    yield 20
    after = [stat(o) for o in (C_FRAMES, C_EVENTS, C_ADDED, C_DROPPED, C_POS, C_VEL)]
    f1 = fc()
    yield 400
    if [stat(o) for o in (C_FRAMES, C_EVENTS, C_ADDED, C_DROPPED, C_POS, C_VEL)] != after:
        emu.fail("chaos went on counting after it was turned off")
    if fc() - f1 < 40:
        emu.fail("the game stopped")
    if (yield from marker_drawn()):
        emu.fail("the marker is drawn with chaos off")
    if emu.r16(ch + C_PENDING) or emu.r8(ch + C_ARMED):
        emu.fail("a change is still waiting after chaos went off")
