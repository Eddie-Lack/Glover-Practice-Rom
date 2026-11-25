"""Shared steps for tests that start in a level: from power-on to the title, and from the title to a level with the
practice menu's Warp, Go (the way a player starts practice on the PRESS START title)."""
import menunav
import trace
from tools import C_ALL, press, wait_for


def title(emu, sym):
    """From power-on to the title flythrough (PRESS START), as in tools.py."""
    gi, fc = sym("gameInfo"), sym("frameCount")
    yield from wait_for(emu, lambda: emu.r8(gi + 1) == trace.PRESENT and emu.r32(fc) >= 200, 4000, "the logos")
    yield from press(emu, "START", frames=6)
    yield from wait_for(emu, lambda: emu.r8(gi + 1) == trace.FLYTHRU and emu.r8(gi) == 4 and emu.r32(fc) >= 30, 3000,
                        "the title flythrough")


def warp(emu, sym, level=trace.ATLANTIS1):
    """Warp, Go to LEVEL (its number in the Warp menu's list), with the menu, from the page the menu opens on.
    Waits until the level runs."""
    gi, fc = sym("gameInfo"), sym("frameCount")
    if not emu.r8(sym("menuOpen")):
        yield from press(emu, *C_ALL)
    yield from menunav.top(emu, sym)
    yield from menunav.goto(emu, sym, "Warp", "Level", enter=False)
    emu.w8(sym("run") + 4, level)  # Run.warpLevel
    yield from menunav.goto(emu, sym, "Go")
    yield from wait_for(emu, lambda: emu.r8(gi + 1) == level and emu.r8(gi) == 4 and emu.r32(fc) >= 100, 4000,
                        f"level {level} after the warp")


def level(emu, sym, number=trace.ATLANTIS1):
    yield from title(emu, sym)
    yield from warp(emu, sym, number)
