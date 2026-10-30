"""The Inspect menu in Atlantis 1:

1. Actors: the list opens and draws; A opens the second actor's details; A there moves Glover above it. Writes actors.png,
   actor0.png and actor1.png (the two frame buffers) (needs --gfx angrylion).
2. Memory: it opens on timer321. A adds the word to the watch list. R, then the lowest digit up, adds 1 to the
   word.
3. Glove and ball: both pages open. Writes glove0.png, glove1.png, ball0.png and ball1.png."""
import trace
from tools import C_ALL, press, wait_for


def run(emu, sym, args):
    gi = sym("gameInfo")
    fc = sym("frameCount")
    mode = lambda: emu.r8(gi)  # noqa: E731
    level = lambda: emu.r8(gi + 1)  # noqa: E731

    yield from wait_for(emu, lambda: level() == trace.PRESENT and emu.r32(fc) >= 200, 4000, "the logos")
    yield from press(emu, "START", frames=6)
    yield from wait_for(emu, lambda: level() == trace.FLYTHRU and mode() == 4 and emu.r32(fc) >= 30, 3000,
                        "the title flythrough")
    yield from press(emu, *C_ALL)
    for b in ("A", "DOWN", "DOWN", "A"):  # Warp, Go
        yield from press(emu, b)
    yield from wait_for(emu, lambda: level() == trace.ATLANTIS1 and mode() == 4 and emu.r32(fc) >= 60, 3000,
                        "Atlantis 1")

    # 1. Inspect (6th item), Actors.
    yield from press(emu, *C_ALL)
    for b in ["DOWN"] * 5 + ["A", "A"]:
        yield from press(emu, b)
    yield 10
    if args.gfx == "angrylion":
        emu.fb_png("actors", sym("cfb0"))
    yield from press(emu, "DOWN")
    yield from press(emu, "A")
    yield 10
    if args.gfx == "angrylion":
        emu.fb_png("actor0", sym("cfb0"))
        emu.fb_png("actor1", sym("cfb1"))
    second = emu.r32(emu.r32(sym("actorList")))  # actorList.head.next->next
    target = [emu.rf(second + 0x34 + 4 * i) for i in range(3)]
    yield from press(emu, "A")
    yield 20
    hand = [emu.rf(sym("hand") + 0x34 + 4 * i) for i in range(3)]
    print(f"inspect: actor at {target}, Glover at {hand}")
    if abs(hand[0] - target[0]) > 5 or abs(hand[2] - target[2]) > 5:
        emu.fail(f"Glover is at {hand}, not above the actor at {target}")

    # 2. Memory (2nd item). The menu opens at the top, on Inspect.
    yield from press(emu, *C_ALL)
    yield from press(emu, "A")
    timer321 = sym("timer321")
    before = emu.r32(timer321)
    for b in ("DOWN", "A", "A", "R", "LEFT", "UP", "B"):
        yield from press(emu, b)

    # 3. Glove and ball (3rd item), both pages.
    yield from press(emu, "DOWN")
    yield from press(emu, "A")
    yield 10
    if emu.r32(sym("menuView")) != sym("stateView"):
        emu.fail("Glove and ball did not open")
    if args.gfx == "angrylion":
        emu.fb_png("glove0", sym("cfb0"))
        emu.fb_png("glove1", sym("cfb1"))
    yield from press(emu, "RIGHT")
    yield 10
    if emu.r8(sym("statePage")) != 1:
        emu.fail("Glove and ball did not turn to its second page")
    if args.gfx == "angrylion":
        emu.fb_png("ball0", sym("cfb0"))
        emu.fb_png("ball1", sym("cfb1"))
    yield from press(emu, "B")
    yield from press(emu, *C_ALL)
    yield 10
    if emu.r32(sym("watches")) != timer321:
        emu.fail(f"the watch list holds {emu.r32(sym('watches')):#x}, not timer321")
    if emu.r32(timer321) != before + 1:
        emu.fail(f"timer321 went from {before} to {emu.r32(timer321)}, not one more")
    if not emu.r8(sym("menuOpen")) == 0:
        emu.fail("the menu did not close")
