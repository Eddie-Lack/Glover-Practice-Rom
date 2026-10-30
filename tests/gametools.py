"""The Game menu's tools in Atlantis 1, through the menu:

1. Power-ups, Bowling ball, Give: the bowling ball's power-up time is -1 (until changed).
2. Lives, one more: player.lives goes up by 1.
3. Freeze random numbers: each game frame starts from the frozen seed, so every sample of the seed is a few calls
   of the generator from it. "One call" moves the frozen seed one step; "level start" sets it to 0x6151F; "set" opens the memory view on it. Then
   off: the seed moves on."""
import trace
from menunav import goto, to_item
from tools import C_ALL, press, wait_for

PLAYER_LIVES = 0x10
PLAYER_POWERUPS = 0x1C
BOWLING_BALL = 7
RUN_RNG_FROZEN = 0x0A  # Run.rngFrozen (practice/include/actions.h)
RUN_RNG_VALUE = 0x0C   # Run.rngValue
RNG_LEVEL_START = 0x6151F


def rng_next(r):
    """One step of the game's generator (maths.c, Random)."""
    r = (r ^ (r << 1)) & 0xFFFFFFFF
    if not r & 0x80000000:
        return r + 1
    return 0 if r == 0x80000000 else r


def steps_from(start, value, limit=255):
    for n in range(limit + 1):
        if start == value:
            return n
        start = rng_next(start)
    return None


def run(emu, sym, args):
    gi = sym("gameInfo")
    fc = sym("frameCount")
    player = sym("player")
    rseed = sym("rseed")
    mode = lambda: emu.r8(gi)  # noqa: E731
    level = lambda: emu.r8(gi + 1)  # noqa: E731

    yield from wait_for(emu, lambda: level() == trace.PRESENT and emu.r32(fc) >= 200, 4000, "the logos")
    yield from press(emu, "START", frames=6)
    yield from wait_for(emu, lambda: level() == trace.FLYTHRU and mode() == 4 and emu.r32(fc) >= 30, 3000,
                        "the title flythrough")
    yield from press(emu, *C_ALL)
    for b in ("A", "DOWN", "DOWN", "A"):  # Warp, Go
        yield from press(emu, b)
    yield from wait_for(emu, lambda: level() == trace.ATLANTIS1 and mode() == 4 and emu.r32(fc) >= 100, 3000,
                        "Atlantis 1")

    # 1. Game, Power-ups, 8 to the right: Bowling ball; Give.
    lives = emu.r32(player + PLAYER_LIVES)
    yield from press(emu, *C_ALL)
    yield from goto(emu, sym, "Game", "Power-ups")
    for b in ["RIGHT"] * 8:
        yield from press(emu, b)
    yield from goto(emu, sym, "Give")
    yield from press(emu, "B")
    # 2. Lives, one more.
    yield from to_item(emu, sym, "Lives")
    yield from press(emu, "RIGHT")
    yield from press(emu, *C_ALL)
    yield 30
    if emu.r32(player + PLAYER_POWERUPS + 4 * BOWLING_BALL) != 0xFFFFFFFF:
        emu.fail(f"the bowling ball's power-up is {emu.r32(player + PLAYER_POWERUPS + 4 * BOWLING_BALL):#x}")
    if emu.r32(player + PLAYER_LIVES) != lives + 1:
        emu.fail(f"lives went from {lives} to {emu.r32(player + PLAYER_LIVES)}, not one more")

    # 3. Game, Random numbers, Freeze random numbers.
    yield from press(emu, *C_ALL)
    yield from goto(emu, sym, "Game", "Random numbers", "Freeze random numbers")
    yield from press(emu, *C_ALL)
    yield 10
    frozen = emu.r32(sym("run") + RUN_RNG_VALUE)
    if not emu.r8(sym("run") + RUN_RNG_FROZEN):
        emu.fail("the seed did not freeze")
    for _ in range(60):
        if steps_from(frozen, emu.r32(rseed)) is None:
            emu.fail(f"the seed {emu.r32(rseed):#x} is not a few calls from the frozen {frozen:#x}")
        yield 1
    # One call, then level start.
    for item, want in (("Random numbers: one call", rng_next(frozen)), ("Random numbers: level start", RNG_LEVEL_START)):
        yield from press(emu, *C_ALL)
        yield from goto(emu, sym, "Game", "Random numbers", item)
        yield from press(emu, *C_ALL)
        yield 20
        got = emu.r32(sym("run") + RUN_RNG_VALUE)
        if got != want:
            emu.fail(f"{item} set the frozen seed to {got:#x}, not {want:#x}")
    # Set: the memory view on the frozen value, at its lowest digit. Up adds 1 to that digit, which
    # wraps within it: 0x6151F becomes 0x61510.
    yield from press(emu, *C_ALL)
    yield from goto(emu, sym, "Game", "Random numbers", "Random numbers: set (frozen)")
    for b in ["UP", "B"]:
        yield from press(emu, b)
    yield from press(emu, *C_ALL)
    yield 20
    got = emu.r32(sym("run") + RUN_RNG_VALUE)
    if got != 0x61510:
        emu.fail(f"Random numbers: set gave {got:#x}, not 0x61510")
    frozen = 0x61510
    yield from press(emu, *C_ALL)
    yield from goto(emu, sym, "Game", "Random numbers", "Freeze random numbers")
    yield from press(emu, *C_ALL)
    yield 60
    first = steps_from(frozen, emu.r32(rseed), 100000)
    yield 100
    later = steps_from(frozen, emu.r32(rseed), 100000)
    print(f"gametools: after the freeze, the seed is {first} and then {later} calls from the frozen one")
    if first is None or later is None or later <= first:
        emu.fail("the seed did not move on after the freeze was turned off")
