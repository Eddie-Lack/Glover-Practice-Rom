"""The game's pause menu after a warp from the PRESS START title, and Reset level from inside it.

1. Warp, Go from the PRESS START title, as a player starts practice. Start then opens the game's PAUSE menu
   (pauseMode 1): the title's disablePause does not stay set.
2. Reset level (stick up + D-pad up) while PAUSE is open: the level starts again and runs (no black screen), and the
   pause menu is gone.
3. The same with Warp (stick up + D-pad down) to Atlantis 2.
4. On the PRESS START title, Reset level does nothing: the title keeps running."""
import startlevel
import trace
from tools import C_ALL, press, wait_for

UP = dict(x=0, y=80)


def run(emu, sym, args):
    gi, fc = sym("gameInfo"), sym("frameCount")
    frame = lambda: emu.r32(fc)  # noqa: E731

    yield from startlevel.title(emu, sym)
    # 4. Reset on the title.
    title_frame = frame()
    yield from press(emu, "UP", **UP)
    yield 120
    if emu.r8(gi + 1) != trace.FLYTHRU or frame() <= title_frame:
        emu.fail(f"Reset on the title changed it (level {emu.r8(gi + 1)}, frame {title_frame} to {frame()})")

    yield from startlevel.warp(emu, sym)

    def pause():
        yield from press(emu, "START", frames=10)
        yield 60
        if not emu.r8(sym("pauseMode")):
            emu.fail(f"Start did not open the pause menu (disablePause {emu.r8(sym('disablePause'))})")

    # 1 and 2.
    yield from pause()
    yield from press(emu, "UP", **UP)
    yield from wait_for(emu, lambda: emu.r8(gi) != 4 or frame() < 10, 3000, "the reset to start")
    yield from wait_for(emu, lambda: emu.r8(gi) == 4 and emu.r8(gi + 1) == trace.ATLANTIS1 and frame() > 20, 3000,
                        "the level to run again after Reset from the pause menu")
    if emu.r8(sym("pauseMode")) or emu.r8(sym("gamePaused")):
        emu.fail(f"the pause menu is still open after the reset: pauseMode {emu.r8(sym('pauseMode'))} gamePaused {emu.r8(sym('gamePaused'))} frame {frame()}")

    # 3. Warp from the pause menu.
    yield from press(emu, *C_ALL)
    emu.w8(sym("run") + 4, trace.ATLANTIS2)  # Run.warpLevel
    yield from press(emu, *C_ALL)
    yield 30
    yield from pause()
    yield from press(emu, "DOWN", **UP)
    yield from wait_for(emu, lambda: emu.r8(gi) != 4, 3000, "the warp to start")
    yield from wait_for(emu, lambda: emu.r8(gi) == 4 and emu.r8(gi + 1) == trace.ATLANTIS2 and frame() > 20, 4000,
                        "Atlantis 2 to run after Warp from the pause menu")
    if emu.r8(sym("pauseMode")):
        emu.fail("the pause menu is still open after the warp")
