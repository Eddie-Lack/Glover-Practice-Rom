"""Warp with a chosen number of lives, garibs and power-up (Warp menu, Lives, Garibs, Power-up), by the menu's
own inputs:

1. From the title: Lives 7, Garibs 12, Power-up "Power ball", Go. Atlantis 1 starts with 7 lives, 12 garibs and the
   power ball (ball type 8) held, and the run is marked modified (BT_OTHER).
2. From the level: Atlantis 2 with 2 lives, 3 garibs and "Speed up". Atlantis 2 starts with them.
3. With the choices back at "as is", 0 and "none" a warp changes nothing and the run is not marked modified."""
import trace
from menunav import goto, to_item
from tools import C_ALL, press, wait_for

BT_TAINT, BT_OTHER = 0x01, 1 << 7
RUN_WARP_LEVEL, RUN_WARP_LIVES, RUN_WARP_GARIBS, RUN_WARP_POWER = 0x04, 0x49, 0x4A, 0x4B
PLAYER_GARIBS, PLAYER_LIVES, PLAYER_POWERUPS = 0x0C, 0x10, 0x1C
MODE_LEVEL = 4


def run(emu, sym, args):
    gi = sym("gameInfo")
    fc = lambda: emu.r32(sym("frameCount"))  # noqa: E731
    mode = lambda: emu.r8(gi)  # noqa: E731
    level = lambda: emu.r8(gi + 1)  # noqa: E731
    player = sym("player")
    run_ = sym("run")
    taint = lambda: emu.r8(sym("bestTimesState") + BT_TAINT)  # noqa: E731
    power = lambda n: emu.r32(player + PLAYER_POWERUPS + 4 * n)  # noqa: E731

    yield from wait_for(emu, lambda: level() == trace.PRESENT and fc() >= 200, 4000, "the logos")
    yield from press(emu, "START", frames=6)
    yield from wait_for(emu, lambda: level() == trace.FLYTHRU and mode() == MODE_LEVEL and fc() >= 30, 3000, "the title")

    def choose(label, offset, delta, want):
        yield from to_item(emu, sym, label)
        for _ in range(abs(delta)):
            yield from press(emu, "RIGHT" if delta > 0 else "LEFT")
        if emu.r8(run_ + offset) != want:
            emu.fail(f"{label}: the menu chose {emu.r8(run_ + offset)}, not {want}")

    def warp(lives, garibs, powerup, level_step=0):
        """Opens the menu on Warp and moves each choice by the given steps, then Go."""
        yield from press(emu, *C_ALL)
        if not emu.r8(sym("menuOpen")):
            emu.fail("the menu did not open")
        yield from press(emu, "A")  # Warp
        if level_step:
            yield from choose("Level", RUN_WARP_LEVEL, level_step, emu.r8(run_ + RUN_WARP_LEVEL) + level_step)
        yield from choose("Lives", RUN_WARP_LIVES, lives[0], lives[1])
        yield from choose("Garibs", RUN_WARP_GARIBS, garibs[0], garibs[1])
        yield from choose("Power-up", RUN_WARP_POWER, powerup[0], powerup[1])
        yield from goto(emu, sym, "Go")

    def started(want_level, wanted_taint=True):
        yield from wait_for(emu, lambda: level() == want_level and mode() == MODE_LEVEL and fc() >= 5, 3000,
                            f"level {want_level} to start")
        # The power-up comes once the level's in-game timer runs.
        yield from wait_for(emu, lambda: taint() & BT_OTHER or not wanted_taint, 1500, "the warp's gifts")
        yield 10

    # 1. From the title: 7 lives (choice 8), 12 garibs, Power ball (choice 10).
    yield from warp((8, 8), (12, 12), (10, 10))
    yield from started(trace.ATLANTIS1)
    got = (emu.r32(player + PLAYER_LIVES), emu.r32(player + PLAYER_GARIBS), power(8), taint())
    print(f"warpgive: from the title: lives {got[0]}, garibs {got[1]}, power ball {got[2]}, taint {got[3]:#x}")
    if got[0] != 7 or got[1] != 12 or got[2] == 0 or not got[3] & BT_OTHER:
        emu.fail(f"from the title: lives, garibs, power ball, taint = {got}")

    # 2. From the level: Atlantis 2, 2 lives (choice 3), 3 garibs, Speed up (choice 7: from 10, 3 left).
    yield from warp((-5, 3), (-9, 3), (-3, 7), level_step=1)
    yield from started(trace.ATLANTIS2)
    got = (emu.r32(player + PLAYER_LIVES), emu.r32(player + PLAYER_GARIBS), power(5), taint())
    print(f"warpgive: from a level: lives {got[0]}, garibs {got[1]}, speed up {got[2]}, taint {got[3]:#x}")
    if got[0] != 2 or got[1] != 3 or got[2] == 0 or not got[3] & BT_OTHER:
        emu.fail(f"from a level: lives, garibs, speed up, taint = {got}")

    # 3. Everything back to "as is", 0 and "none": nothing changes and the run is kept.
    yield from warp((-3, 0), (-3, 0), (-7, 0), level_step=-1)
    yield from started(trace.ATLANTIS1, wanted_taint=False)
    yield 150
    got = (emu.r32(player + PLAYER_LIVES), emu.r32(player + PLAYER_GARIBS), taint())
    print(f"warpgive: plain warp: lives {got[0]}, garibs {got[1]}, taint {got[2]:#x}")
    if got[0] != 2 or got[1] != 0 or got[2]:
        emu.fail(f"plain warp: lives, garibs, taint = {got}")
