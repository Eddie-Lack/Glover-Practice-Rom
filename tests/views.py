"""The full-panel views of the Inspect and Game menus: their keys and what they must not do.

1. On the title, Actors: A on a details page does nothing (go needs a level), and the menu stays open.
2. In Atlantis 1, Game, Random numbers, "Random numbers: set (frozen)": the memory view opens locked on the frozen
   seed (run.rngValue). Up adds 1 to its lowest digit, left and up add 0x10. It is not a poke: no marker. Z, Start,
   R and C right do nothing there: no poke, the view stays on the seed.
3. Memory: C right makes the value a half word. R and up add 1 to the half word at timer321, and the marker is set.
4. An edit in place does not write the practice segment: the pokes page's Start moves the view to a poke on
   corrupt.lastAddress (practice data); R and up leave the word as it was.
5. The pokes page: a C button changes the poke, but the menu's chord rolled in (C right, then C up, then all four)
   changes nothing and closes the menu.
Writes seed.png and halfword.png (needs --gfx angrylion)."""
import trace
from menunav import goto, top
from tools import C_ALL, press, wait_for

RUN_RNG_FROZEN, RUN_RNG_VALUE = 0x0A, 0x0C  # practice/include/actions.h
POKE_SIZE = 0x10
P_ADDRESS, P_VALUE, P_MODE, P_SIZE, P_BITS = 0x00, 0x04, 0x0C, 0x0D, 0x0E
C_LAST_ADDRESS, C_ACTIVE, C_DIRTY = 0x8C, 0x94, 0x95
ONCE = 1


def run(emu, sym, args):
    gi = sym("gameInfo")
    fc = lambda: emu.r32(sym("frameCount"))  # noqa: E731
    mode = lambda: emu.r8(gi)  # noqa: E731
    level = lambda: emu.r8(gi + 1)  # noqa: E731
    menu_open = lambda: emu.r8(sym("menuOpen"))  # noqa: E731
    view = lambda: emu.r32(sym("menuView"))  # noqa: E731
    cor = sym("corrupt")
    run_ = sym("run")
    poke = lambda i, off: cor + POKE_SIZE * i + off  # noqa: E731
    timer321 = sym("timer321")

    def shot(name):
        if args.gfx == "angrylion":
            emu.fb_png(name, sym("cfb_16_b"))
            emu.fb_png(name + "_0", sym("cfb_16_a"))

    yield from wait_for(emu, lambda: level() == trace.PRESENT and fc() >= 200, 4000, "the logos")
    yield from press(emu, "START", frames=6)
    yield from wait_for(emu, lambda: level() == trace.FLYTHRU and mode() == 4 and fc() >= 30, 3000, "the title")

    # 1. Actors on the title: A on a details page does not close the menu.
    yield from press(emu, *C_ALL)
    yield from goto(emu, sym, "Inspect", "Actors")
    yield from press(emu, "A")  # the details page
    yield from press(emu, "A")  # go: not on the title
    yield 10
    if not menu_open() or view() != sym("actorView"):
        emu.fail("A on an actor's details page acted on the title")
    for b in ("B", "B"):
        yield from press(emu, b)
    yield from top(emu, sym)
    yield from goto(emu, sym, "Warp", "Go")
    yield from wait_for(emu, lambda: level() == trace.ATLANTIS1 and mode() == 4 and fc() >= 100, 3000, "Atlantis 1")
    yield 30

    # 2. The seed.
    yield from press(emu, *C_ALL)
    yield from top(emu, sym)
    yield from goto(emu, sym, "Game", "Random numbers", "Random numbers: set (frozen)")
    if view() != sym("memoryView") or not emu.r8(run_ + RUN_RNG_FROZEN):
        emu.fail("the seed's memory view did not open, or the seed is not frozen")
    v0 = emu.r32(run_ + RUN_RNG_VALUE)
    yield from press(emu, "UP")
    v1 = emu.r32(run_ + RUN_RNG_VALUE)
    if v1 != (v0 & ~0xF) | ((v0 + 1) & 0xF):
        emu.fail(f"up changed the seed from {v0:08X} to {v1:08X}, not its lowest digit by 1")
    yield from press(emu, "LEFT")
    yield from press(emu, "UP")
    v2 = emu.r32(run_ + RUN_RNG_VALUE)
    if v2 != (v1 & ~0xF0) | ((v1 + 0x10) & 0xF0):
        emu.fail(f"left and up changed the seed from {v1:08X} to {v2:08X}")
    for b in ("Z", "START", "R", "CRIGHT", "UP"):
        yield from press(emu, b)
    yield 10
    shot("seed")
    if emu.r32(run_ + RUN_RNG_VALUE) != (v2 & ~0xF0) | ((v2 + 0x10) & 0xF0):
        emu.fail(f"the locked view left the seed: {emu.r32(run_ + RUN_RNG_VALUE):08X}")
    if any(emu.r32(poke(i, P_ADDRESS)) for i in range(8)):
        emu.fail("Z in the seed's view made a poke")
    if emu.r8(cor + C_DIRTY) or emu.r8(cor + C_ACTIVE):
        emu.fail("an edit of the seed set the pokes marker")
    yield from press(emu, "B")
    if view():
        emu.fail("B did not close the seed's view")
    yield from press(emu, *C_ALL)
    emu.w8(run_ + RUN_RNG_FROZEN, 0)

    # 3. Memory: a half word.
    yield from press(emu, *C_ALL)
    yield from top(emu, sym)
    yield from goto(emu, sym, "Inspect", "Memory")
    h0 = emu.r16(timer321)
    for b in ("CRIGHT", "R", "UP"):
        yield from press(emu, b)
    yield 10
    shot("halfword")
    h1 = emu.r16(timer321)
    if h1 != (h0 & ~0xF) | ((h0 + 1) & 0xF):
        emu.fail(f"the half word at timer321 went from {h0:04X} to {h1:04X}")
    if emu.r8(cor + C_DIRTY) != 1:
        emu.fail("an edit in place did not set the marker")
    emu.w16(timer321, h0)

    # 4. Not the practice segment.
    target = cor + C_LAST_ADDRESS
    emu.w32(target, 0x80123450)
    emu.w32(poke(0, P_ADDRESS), target)
    emu.w32(poke(0, P_VALUE), 0)
    emu.w8(poke(0, P_MODE), ONCE)
    emu.w8(poke(0, P_SIZE), 4)
    emu.w8(poke(0, P_BITS), 32)
    for b in ("START", "START", "R", "UP"):  # the pokes page, Start on poke 1, edit, up
        yield from press(emu, b)
    if emu.r32(target) != 0x80123450:
        emu.fail(f"an edit in place wrote the practice data: {emu.r32(target):08X}")

    # 5. The pokes page: a C tap changes the bits, the menu's chord does not.
    yield from press(emu, "START")
    yield from press(emu, "CUP")
    if emu.r8(poke(0, P_BITS)) != 1:
        emu.fail(f"C up gave {emu.r8(poke(0, P_BITS))} bits, not 1")
    yield from press(emu, "CDOWN")
    if emu.r8(poke(0, P_BITS)) != 32:
        emu.fail(f"C down gave {emu.r8(poke(0, P_BITS))} bits, not 32")
    emu.hold("CRIGHT")
    yield 5
    emu.hold("CRIGHT", "CUP")
    yield 5
    emu.hold(*C_ALL)
    yield 8
    emu.release()
    yield 10
    if menu_open():
        emu.fail("the menu's chord did not close the menu")
    if (emu.r8(poke(0, P_SIZE)), emu.r8(poke(0, P_BITS))) != (4, 32):
        emu.fail(f"the menu's chord changed the poke: size {emu.r8(poke(0, P_SIZE))}, bits {emu.r8(poke(0, P_BITS))}")
    emu.w32(poke(0, P_ADDRESS), 0)
    emu.w8(poke(0, P_MODE), 0)
