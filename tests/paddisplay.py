"""The HUD menu's "Controller display" (practice/src/paddisplay.c): the game's demo controller display during play,
in Atlantis 1.

1. Off (the start): Atlantis 1 starts without the display's overlays (controllerOL[0] has no bitmaps; the game
   never frees the last overlay's, so only the first one tells).
2. Controller 1, then Reset level: the overlays are set up, the game's own display (printController) stays off, and
   the picture differs from the one without the display. A held on controller 1 lights the A button's picture; the
   others stay dim. The stick held right moves the stick's picture right of its centre.
3. Controller 2: C-up written into controllerdata[1] (the emulator has no controller in port 2) lights C-up, and A
   on controller 1 no longer lights A.
4. Off: the overlays are no longer drawn (A on controller 2 does not light A), and the next reset frees them."""
import trace
from menunav import goto
from tools import C_ALL, press, wait_for

OVERLAY_SIZE = 0x74
OL_X, OL_R, OL_BITMAPS = 0x0C, 0x20, 0x24
A, Z, CUP, R, STICK = 2, 4, 5, 10, 9
CONT_A, CONT_CUP = 0x8000, 0x0008
LIT, DIM = 255, 100


def run(emu, sym, args):
    gi = sym("gameInfo")
    fc = lambda: emu.r32(sym("frameCount"))  # noqa: E731
    mode = lambda: emu.r8(gi)  # noqa: E731
    level = lambda: emu.r8(gi + 1)  # noqa: E731
    ol = sym("controllerOL")
    bitmaps = lambda i: emu.r32(ol + i * OVERLAY_SIZE + OL_BITMAPS)  # noqa: E731
    colour = lambda i: emu.r8(ol + i * OVERLAY_SIZE + OL_R)  # noqa: E731
    xpos = lambda i: emu.rf(ol + i * OVERLAY_SIZE + OL_X)  # noqa: E731
    rcdelay = emu.r16(sym("rcdelay"))

    def choose(steps):
        """HUD, Controller display, RIGHT STEPS times, then close the menu."""
        yield from press(emu, *C_ALL)
        if not emu.r8(sym("menuOpen")):
            emu.fail("the menu did not open")
        yield from goto(emu, sym, "HUD", "Controller display", enter=False)
        for _ in range(steps):
            yield from press(emu, "RIGHT")
        yield from press(emu, *C_ALL)
    
    def reset():
        yield from press(emu, *C_ALL)
        yield from goto(emu, sym, "Warp", "Reset level")
        yield from wait_for(emu, lambda: mode() != 4, 2000, "the reset")
        yield from wait_for(emu, lambda: mode() == 4 and fc() >= 60, 3000, "Atlantis 1 after the reset")

    def pad2(bits, frames):
        for _ in range(frames):
            emu.w16(sym("controllerdata") + 6, bits)
            yield 1
        emu.w16(sym("controllerdata") + 6, 0)

    yield from wait_for(emu, lambda: level() == trace.PRESENT and fc() >= 200, 4000, "the logos")
    yield from press(emu, "START", frames=6)
    yield from wait_for(emu, lambda: level() == trace.FLYTHRU and mode() == 4 and fc() >= 30, 3000, "the title")
    yield from press(emu, *C_ALL)
    for b in ("A", "DOWN", "DOWN", "A"):  # Warp, Go
        yield from press(emu, b)
    yield from wait_for(emu, lambda: level() == trace.ATLANTIS1 and mode() == 4 and fc() >= 60, 3000, "Atlantis 1")

    # 1. Off.
    if bitmaps(0):
        emu.fail("the display's overlays are set up with the option off")
    emu.shot("off")
    plain = bytes(emu.raw(sym("cfb_16_a"), 320 * 240 * 2))

    # 2. Controller 1.
    yield from choose(1)
    if bitmaps(0):
        emu.fail("the overlays were set up before the level started again")
    yield from reset()
    if not bitmaps(0) or not bitmaps(10):
        emu.fail("the overlays were not set up at the level start")
    if emu.r8(sym("printController")):
        emu.fail("the game's own display (printController) is on")
    yield 20
    emu.shot("controller1")
    if bytes(emu.raw(sym("cfb_16_a"), 320 * 240 * 2)) == plain:
        emu.fail("the picture did not change")
    emu.hold("A")
    yield 15
    if colour(A) != LIT or colour(Z) != DIM or colour(CUP) != DIM:
        emu.fail(f"with A held: A {colour(A)}, Z {colour(Z)}, C-up {colour(CUP)}")
    emu.release()
    yield 3 * (rcdelay + 4)
    if colour(A) != DIM:
        emu.fail(f"A is {colour(A)} after its release")
    emu.hold(x=80, y=0)
    yield 12
    centre = xpos(0) + emu.r16s(sym("rcxoff"))
    if xpos(STICK) <= centre + 1:
        emu.fail(f"the stick's picture is at x {xpos(STICK)}, not right of its centre {centre}")
    emu.release()
    yield 12

    # 3. Controller 2.
    yield from choose(1)
    yield from pad2(CONT_CUP, 15)
    if colour(CUP) != LIT:
        emu.fail(f"C-up on controller 2 is {colour(CUP)}")
    yield 3 * (rcdelay + 4)
    emu.hold("A")
    yield 15
    if colour(A) != DIM:
        emu.fail("A on controller 1 lights the display for controller 2")
    emu.release()
    yield 12

    # 4. Off.
    yield from choose(1)
    yield from pad2(CONT_A, 15)
    if colour(A) != DIM:
        emu.fail("the display is still drawn with the option off")
    yield from reset()
    if bitmaps(0):
        emu.fail("the overlays are still there after a reset with the option off")
