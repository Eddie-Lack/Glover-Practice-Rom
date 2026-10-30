"""The practice tools, through the menu and the default bindings:

1. On the title flythrough (a demo playback), the menu opens; Warp, Go starts Atlantis 1 from the title.
2. Stick up + D-pad up resets the level: it starts again.
3. Stick down + D-pad up stops the game (frame advance); stick down + D-pad right runs one game frame; stick
   down + D-pad up lets it go on.
4. Stick up + D-pad right brings Glover back to the last checkpoint (its restart timer starts).
5. In the menu, Game, Cheats, Infinite lives turns cheat bit 8 on."""
import trace

C_ALL = ("CUP", "CDOWN", "CLEFT", "CRIGHT")


def press(emu, *buttons, x=0, y=0, frames=8):
    emu.hold(*buttons, x=x, y=y)
    yield frames
    emu.release()
    yield 8


def wait_for(emu, cond, limit, what):
    for _ in range(limit):
        if cond():
            return
        yield 1
    emu.fail(f"timed out waiting for {what}")


def run(emu, sym, args):
    gi = sym("gameInfo")
    fc = sym("frameCount")
    menu_open = sym("menuOpen")
    mode = lambda: emu.r8(gi)  # noqa: E731
    level = lambda: emu.r8(gi + 1)  # noqa: E731

    yield from wait_for(emu, lambda: level() == trace.PRESENT and emu.r32(fc) >= 200, 4000, "the logos")
    yield from press(emu, "START", frames=6)
    yield from wait_for(emu, lambda: level() == trace.FLYTHRU and mode() == 4 and emu.r32(fc) >= 30, 3000,
                        "the title flythrough")

    # 1. The menu on the title, then Warp, Go (Atlantis 1 is the default level).
    yield from press(emu, *C_ALL)
    if not emu.r8(menu_open):
        emu.fail("the menu did not open on the title flythrough")
    yield from press(emu, "A")         # Warp
    yield from press(emu, "DOWN")      # Difficulty
    yield from press(emu, "DOWN")      # Go
    yield from press(emu, "A")
    yield from wait_for(emu, lambda: level() == trace.ATLANTIS1 and mode() == 4 and emu.r32(fc) >= 100, 3000,
                        "Atlantis 1 after the warp")

    # 2. Reset: the level starts again.
    yield from press(emu, "UP", x=0, y=80)
    yield from wait_for(emu, lambda: emu.r32(fc) < 20, 2000, "the level to restart")
    yield from wait_for(emu, lambda: level() == trace.ATLANTIS1 and mode() == 4 and emu.r32(fc) >= 60, 3000,
                        "Atlantis 1 after the reset")

    # 3. Frame advance.
    yield from press(emu, "UP", x=0, y=-80)
    stopped = emu.r32(fc)
    yield 60
    if emu.r32(fc) != stopped:
        emu.fail(f"frame advance did not stop the game ({stopped} to {emu.r32(fc)})")
    yield from press(emu, "RIGHT", x=0, y=-80)
    yield 20
    if emu.r32(fc) != stopped + 1:
        emu.fail(f"frame step ran {emu.r32(fc) - stopped} game frames, not 1")
    yield from press(emu, "UP", x=0, y=-80)
    yield 30
    if emu.r32(fc) <= stopped + 1:
        emu.fail("the game did not go on after frame advance")

    # 4. The last checkpoint.
    checkpoint = emu.r32(sym("handControl") + 0x510)
    gate = emu.r32(checkpoint + 0x54)
    yield from press(emu, "RIGHT", x=0, y=80)
    yield 5
    timer = emu.r16(gate + 2)
    if not 0 < timer <= 65:
        emu.fail(f"the checkpoint's restart timer is {timer}")

    # 5. A cheat: Game (5th item), Cheats, Infinite lives (4th).
    yield 120
    yield from press(emu, *C_ALL)
    for b in ("DOWN", "DOWN", "DOWN", "DOWN", "A", "A", "DOWN", "DOWN", "DOWN", "A"):
        yield from press(emu, b)
    yield from press(emu, *C_ALL)
    yield 30
    if not emu.r32(sym("cheatActivated")) & 8:
        emu.fail("Infinite lives did not turn on")
