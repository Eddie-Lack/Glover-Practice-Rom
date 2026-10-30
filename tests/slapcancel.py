"""A fact of the game, measured with the frame log: the slap's Z cancel. In Atlantis 1 the ball is put at the
glove, which takes it (JOINED). A held for 4 reads starts the slap's windup (PREWHACK). Z pressed during the
windup ends it on the next game frame without a hit: the glove keeps the ball (JOINED), and A is latched until
it is released (hand.c, MaintainAction). Z was not held before the glove took the ball."""
import trace
from framelog import read_log
from tools import C_ALL, press, wait_for

RUN_FRAME_LOG = 0x31
JOINED, PREWHACK = 3, 5
CONT_A, CONT_Z = 0x8000, 0x2000


def run(emu, sym, args):
    gi = sym("gameInfo")
    fc = lambda: emu.r32(sym("frameCount"))  # noqa: E731
    mode = lambda: emu.r8(gi)  # noqa: E731
    level = lambda: emu.r8(gi + 1)  # noqa: E731
    hand, ball = sym("hand"), sym("ball")
    action = lambda: emu.r16(emu.r32(hand + 0xD4) + 4)  # noqa: E731  hand.state->mode

    yield from wait_for(emu, lambda: level() == trace.PRESENT and fc() >= 200, 4000, "the logos")
    yield from press(emu, "START", frames=6)
    yield from wait_for(emu, lambda: level() == trace.FLYTHRU and mode() == 4 and fc() >= 30, 3000, "the title")
    yield from press(emu, *C_ALL)
    for b in ("A", "DOWN", "DOWN", "A"):
        yield from press(emu, b)
    yield from wait_for(emu, lambda: level() == trace.ATLANTIS1 and mode() == 4 and fc() >= 100, 3000, "Atlantis 1")
    emu.w8(sym("run") + RUN_FRAME_LOG, 1)

    # The ball at the glove, until the glove holds it.
    hp = [emu.rf(hand + 0x34 + 4 * i) for i in range(3)]
    for _ in range(60):
        for i, v in enumerate([hp[0], hp[1] + 5, hp[2]]):
            emu.wf(ball + 0x34 + 4 * i, v)  # pos
            emu.wf(ball + 0x40 + 4 * i, v)  # prevPos
        yield 1
        if action() == JOINED:
            break
    yield from wait_for(emu, lambda: action() == JOINED, 100, "the glove to hold the ball")
    yield 20

    emu.hold("A")
    yield from wait_for(emu, lambda: action() == PREWHACK, 100, "the slap's windup")
    yield 7  # into the windup
    emu.hold("A", "Z")
    yield 15
    emu.release()
    yield 20

    log = read_log(emu, sym)
    z = next((i for i, e in enumerate(log) if e[2] & CONT_Z), None)
    if z is None or log[z][4] != PREWHACK:
        emu.fail(f"Z was not pressed during the windup: {[(e[4], hex(e[2])) for e in log[-30:]]}")
    after = log[z + 1]
    print(f"slapcancel: Z pressed on frame {log[z][5]} of the windup; next frame: action {after[4]}, "
          f"latched {after[3]:#x}")
    if after[4] != JOINED or not after[3] & CONT_A:
        emu.fail(f"after Z in the windup the action is {after[4]} and A is not latched ({after[3]:#x})")
