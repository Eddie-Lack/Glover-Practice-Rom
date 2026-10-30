"""The full-game timer, with the game's state written into RAM on the title screen:

1. toStartGame set to 1, as the difficulty screen does: the timer runs, about one video frame for each video
   frame that passes.
2. Six crystals back, seven won, and the exit into the cave running: the timer stops and shows the run done."""
import layout
import trace
from tools import wait_for

HUD_GAME = 9
GAME_INFO_EXIT_TYPE = 0x11
GAME_INFO_EXIT_FLAGS = 0x54
SAVE_SLOT_SIZE = 0x1C
SAVE_SLOT_HUB_TARGET = 0x12
SAVE_SLOT_HUB_LEVEL = 0x13
EXIT_CAVE = 12


def run(emu, sym, args):
    gi = sym("gameInfo")
    timer = sym("gameTimer")
    running = lambda: emu.r8(timer)  # noqa: E731
    done = lambda: emu.r8(timer + 1)  # noqa: E731
    elapsed = lambda: emu.r32(timer + 4)  # noqa: E731

    emu.w8(sym("settings") + layout.SETTINGS_HUD + layout.HUD_PLACE_SIZE * HUD_GAME, 1)
    yield from wait_for(emu, lambda: emu.r8(gi + 1) == trace.PRESENT and emu.r32(sym("frameCount")) >= 100, 4000,
                        "the logos")
    if running():
        emu.fail("the timer runs before a new game")
    emu.w32(sym("toStartGame"), 1)
    yield 20
    if not running():
        emu.fail("the timer did not start")
    vi = sym("__osViIntrCount")
    start, start_vi = elapsed(), emu.r32(vi)
    yield 120
    counted, passed = elapsed() - start, emu.r32(vi) - start_vi
    print(f"gametimer: {counted} video frames counted in {passed}")
    if abs(counted - passed) > 10:
        emu.fail(f"the timer counted {counted} video frames in {passed}")

    slot = sym("saveSlot") + SAVE_SLOT_SIZE * emu.r16(sym("currentSaveSlot"))
    emu.w8(slot + SAVE_SLOT_HUB_TARGET, 7)
    emu.w8(slot + SAVE_SLOT_HUB_LEVEL, 6)
    emu.w8(gi + GAME_INFO_EXIT_TYPE, EXIT_CAVE)
    emu.w8(gi + GAME_INFO_EXIT_FLAGS, emu.r8(gi + GAME_INFO_EXIT_FLAGS) | 1)
    yield 10
    if running() or not done():
        emu.fail(f"the timer did not stop at the cave (running {running()}, done {done()})")
