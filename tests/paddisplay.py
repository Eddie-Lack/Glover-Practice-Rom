"""The HUD menu's "Controller display" (practice/src/paddisplay.c): the game's demo controller display during play,
in Atlantis 1.

1. Off (the start): Atlantis 1 starts without the display's overlays (controllerOL[0] has no bitmaps; the game
   never frees the last overlay's, so only the first one tells).
2. Controller 1, then Reset level: the overlays are set up, the game's own display (printController) stays off, and
   the picture differs from the one without the display. A held on controller 1 lights the A button's picture; the
   others stay dim. The stick held right moves the stick's picture right of its centre.
3. Controller 2: C-up written into controllerdata[1] (the emulator has no controller in port 2) lights C-up, and A
   on controller 1 no longer lights A.
   With the default place and size every picture is where the demos have it, 40 pixels higher. "Controller display
   size" at 125% scales every picture and the gaps between them; the Layout page's "Controller display x" and "y" move
   the whole display by 2 pixels a step.
4. Off: the overlays are no longer drawn (A on controller 2 does not light A), and the next reset frees them."""
import layout
import trace
from menunav import goto
from tools import C_ALL, press, wait_for

OVERLAY_SIZE = 0x74
OL_X, OL_Y, OL_SCALE, OL_R, OL_BITMAPS = 0x0C, 0x10, 0x18, 0x20, 0x24
# The pictures' places in the demos (LoadLevel), as paddisplay.c has them.
PARTS = [(46, 182), (94, 182), (92, 171), (85, 164), (70, 200), (99, 152), (99, 164), (93, 158), (105, 158),
         (160, 150), (103, 142)]
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
    ypos = lambda i: emu.rf(ol + i * OVERLAY_SIZE + OL_Y)  # noqa: E731
    scale = lambda i: emu.r32(ol + i * OVERLAY_SIZE + OL_SCALE)  # noqa: E731
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

    # The place and the size. The stick's picture (9) has its own place.
    for i, (x, y) in enumerate(PARTS):
        if i != STICK and (abs(xpos(i) - x) > 0.01 or abs(ypos(i) - (y - 40)) > 0.01 or scale(i) != 1024):
            emu.fail(f"picture {i} at {xpos(i)}, {ypos(i)} scale {scale(i)}, not at {x}, {y - 40} at 1024")
    yield from press(emu, *C_ALL)
    yield from goto(emu, sym, "HUD", "Controller display size", enter=False)
    yield from press(emu, "RIGHT")
    yield from press(emu, *C_ALL)
    yield 12
    for i, (x, y) in enumerate(PARTS):
        want = (46 + (x - 46) * 1.25, 142 + (y - 182) * 1.25)
        if i != STICK and (abs(xpos(i) - want[0]) > 0.01 or abs(ypos(i) - want[1]) > 0.01 or scale(i) != 1280):
            emu.fail(f"at 125% picture {i} is at {xpos(i)}, {ypos(i)} scale {scale(i)}, not at {want} at 1280")
    if emu.r8(sym("run") + layout.RUN_PAD_SIZE) != 3:
        emu.fail("Controller display size did not choose 125%")
    yield from press(emu, *C_ALL)
    yield from goto(emu, sym, "HUD", "Layout", "Controller display x", enter=False)
    for _ in range(5):
        yield from press(emu, "RIGHT")
    yield from press(emu, "DOWN")
    for _ in range(3):
        yield from press(emu, "LEFT")
    yield from press(emu, *C_ALL)
    yield 12
    if abs(xpos(0) - 56) > 0.01 or abs(ypos(0) - 136) > 0.01 or abs(xpos(1) - (56 + 48 * 1.25)) > 0.01:
        emu.fail(f"moved 5 steps right and 3 up, the pad is at {xpos(0)}, {ypos(0)}, not at 56, 136")
    emu.shot("moved")
    emu.w8(sym("run") + layout.RUN_PAD_X, 23)
    emu.w8(sym("run") + layout.RUN_PAD_Y, 71)
    emu.w8(sym("run") + layout.RUN_PAD_SIZE, 2)
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
