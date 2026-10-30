"""Show the taps that the game missed (practice/include/missedtaps.h), in Atlantis 1, through the Inspect menu's
second last item.

The harness presses buttons per video frame, and the emulator's video frames come in bursts that pass no game time,
so it cannot make a tap that is shorter than the time between two reads of the game. This test uses a pulse
(tests/pulseinput.c, `emu.pulse`): A is pressed for exactly one controller poll, whoever makes it. With the tool off,
the game is the only one to poll, so it sees every pulse. With the tool on, its thread polls about ten times a game
frame and takes nearly all the pulses before the game does.

1. Off: the game sees every pulse; the tool reads and records nothing.
2. On: every pulse is either seen by the game (the input layer's `held` has the button) or recorded as missed
   (MissedTaps.total), and most are recorded as missed. The button's display counter is set. The thread makes many
   reads and skips few turns, a held button is seen as usual, and the game keeps its pace of one game frame in three
   video interrupts.
3. Off again: the reads stop and later pulses are not recorded."""
import layout
import trace
from menunav import goto
from tools import C_ALL, press, wait_for

CONT_A = 0x8000
INPUT_HELD = 0x00  # Input.held (practice/include/input.h)
RUN_MISSED_TAPS = 0x44  # Run.missedTaps
M_READS, M_SKIPPED, M_GAME_READS, M_TOTAL, M_LAST, M_ACTIVE, M_FAILED = 0x00, 0x04, 0x08, 0x0C, 0x10, 0x12, 0x13
M_COUNT, M_SHOW = 0x14, 0x4C  # per button, in the order of padBits: A is 0
TAPS = 30
GAPS = (21, 24, 28, 23, 26)  # video frames between taps: a different phase against the game's reads each time


def run(emu, sym, args):
    gi = sym("gameInfo")
    fc = lambda: emu.r32(sym("frameCount"))  # noqa: E731
    mode = lambda: emu.r8(gi)  # noqa: E731
    level = lambda: emu.r8(gi + 1)  # noqa: E731
    run_ = sym("run")
    mt = sym("missedTaps")
    held = lambda: emu.r16(sym("input") + INPUT_HELD)  # noqa: E731
    stat = lambda off: emu.r32(mt + off)  # noqa: E731
    vi = lambda: emu.r32(sym("__osViIntrCount"))  # noqa: E731

    yield from wait_for(emu, lambda: level() == trace.PRESENT and fc() >= 200, 4000, "the logos")
    yield from press(emu, "START", frames=6)
    yield from wait_for(emu, lambda: level() == trace.FLYTHRU and mode() == 4 and fc() >= 30, 3000, "the title")
    yield from press(emu, *C_ALL)
    for b in ("A", "DOWN", "DOWN", "A"):  # Warp, Go
        yield from press(emu, b)
    yield from wait_for(emu, lambda: level() == trace.ATLANTIS1 and mode() == 4 and fc() >= 100, 3000, "Atlantis 1")
    emu.w8(sym("settings") + layout.SETTINGS_HUD, 0)  # the timer off
    yield 60

    def taps():
        """TAPS pulses of A. Returns how many the game saw (the rises of A in `held`, sampled each video frame), and
        the game frames and video interrupts that passed."""
        seen = 0
        was = bool(held() & CONT_A)
        f0, v0 = fc(), vi()
        for i in range(TAPS):
            emu.pulse("A", polls=1)
            for _ in range(GAPS[i % len(GAPS)] + 60):
                if emu.pulse_left() == 0 and _ >= GAPS[i % len(GAPS)]:
                    break
                yield 1
                now = bool(held() & CONT_A)
                seen += now and not was
                was = now
            else:
                emu.fail("the pulse was not taken by any read")
        return seen, fc() - f0, vi() - v0

    def toggle():
        """Inspect, Show missed taps, A."""
        yield from press(emu, *C_ALL)
        if not emu.r8(sym("menuOpen")):
            emu.fail("the menu did not open")
        yield from goto(emu, sym, "Inspect", "Show missed taps")
        yield from press(emu, "B")
        yield from press(emu, *C_ALL)
        yield 10

    # The menu opens on Warp: Inspect is 5 down. The first call goes there; later calls open on Inspect.
    yield from press(emu, *C_ALL)
    for b in ["DOWN"] * 5:
        yield from press(emu, b)
    yield from press(emu, *C_ALL)
    yield 10

    # 1. Off.
    seen_off, frames_off, vi_off = yield from taps()
    print(f"missedtaps: off, the game saw {seen_off} of {TAPS} pulses; {frames_off} game frames in {vi_off} "
          f"interrupts; reads {stat(M_READS)}, total {stat(M_TOTAL)}")
    if stat(M_READS) or stat(M_GAME_READS) or stat(M_TOTAL) or emu.r8(mt + M_ACTIVE):
        emu.fail("the tool read or recorded something with the switch off")
    if seen_off != TAPS:
        emu.fail(f"the game saw {seen_off} of {TAPS} pulses with the tool off")

    # 2. On.
    yield from toggle()
    if not emu.r8(run_ + RUN_MISSED_TAPS):
        emu.fail("the menu did not turn the switch on")
    yield 30
    if not emu.r8(mt + M_ACTIVE) or stat(M_READS) < 10:
        emu.fail(f"the tool does not read: active {emu.r8(mt + M_ACTIVE)}, reads {stat(M_READS)}")
    reads0, skipped0, game0, total0 = stat(M_READS), stat(M_SKIPPED), stat(M_GAME_READS), stat(M_TOTAL)
    seen_on, frames_on, vi_on = yield from taps()
    missed = stat(M_TOTAL) - total0
    reads = stat(M_READS) - reads0
    skipped = stat(M_SKIPPED) - skipped0
    print(f"missedtaps: on, the game saw {seen_on}, missed {missed}, of {TAPS} pulses; {reads} extra reads, "
          f"{skipped} skipped, {stat(M_GAME_READS) - game0} game reads; {frames_on} game frames in {vi_on} "
          f"interrupts")
    if seen_on + missed != TAPS:
        emu.fail(f"seen {seen_on} + missed {missed} is not the {TAPS} pulses")
    if missed < TAPS * 2 // 3:
        emu.fail(f"only {missed} of {TAPS} pulses recorded as missed")
    if stat(M_COUNT) != stat(M_TOTAL) or not emu.r16(mt + M_LAST) & CONT_A:
        emu.fail("the count of A is not the total, or the last missed tap is not A")
    if reads < vi_on or skipped > reads // 4 or emu.r8(mt + M_FAILED):
        emu.fail(f"{reads} extra reads, {skipped} skipped turns, failed {emu.r8(mt + M_FAILED)} in {vi_on} interrupts")
    pace_off, pace_on = frames_off / vi_off, frames_on / vi_on
    if not 0.9 * pace_off <= pace_on <= 1.1 * pace_off:
        emu.fail(f"the game's pace changed: {pace_off:.3f} game frames an interrupt off, {pace_on:.3f} on")
    # A button held for several game frames is seen as usual.
    emu.hold("A")
    yield 12
    if not held() & CONT_A:
        emu.fail("a held A did not reach the input layer")
    emu.release()
    yield 12
    # The display's counter for the button of a miss is set, and counts down by game reads.
    emu.pulse("A", polls=1)
    yield 1
    shown = 0
    for _ in range(40):
        yield 1
        shown |= emu.r8(mt + M_SHOW) != 0
        if emu.pulse_left() == 0 and shown:
            break
    if not shown:
        emu.fail("no button is shown as missed after a miss")

    # 3. Off again.
    yield from toggle()
    if emu.r8(run_ + RUN_MISSED_TAPS):
        emu.fail("the menu did not turn the switch off")
    yield 30
    reads1, total1 = stat(M_READS), stat(M_TOTAL)
    if emu.r8(mt + M_ACTIVE) or any(emu.r8(mt + M_SHOW + b) for b in range(14)):
        emu.fail("the tool is still active after the switch went off")
    seen_after, _, _ = yield from taps()
    if stat(M_READS) != reads1 or stat(M_TOTAL) != total1 or seen_after != TAPS:
        emu.fail("the tool read or recorded after the switch went off, or the game missed a pulse")
    if level() != trace.ATLANTIS1 or mode() != 4:
        emu.fail("the game left the level")
