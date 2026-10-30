"""The instant reset (Warp menu, last item "Reset": load the level / instant).

With the setting off, Reset leaves the level through the game's exit and loads it again, and no state is saved at a
level start. With it on, the state of each level's first game frame is saved into the automatic slot, and Reset loads
it: no fade, and the game frames after it equal those after the level's real start (same random numbers, places and
timers). The constant stick keeps the input steady. The setting is saved with the other settings (bit 15 of the
saved block's hudOn)."""
import glob
import os

import layout
import trace
from stateutil import Verify
from tools import C_ALL, press, wait_for

ACTION_RESET = 0
ACTION_WARP = 1
RUN_WARP_LEVEL = 4
SLOT_SIZE = 24
SLOT_AUTO = 11
SETTINGS_INSTANT = layout.SETTINGS_INSTANT_RESET
HAND_DEATH_TIMER = 0x26  # HandControl.deathTimer (practice/include/game.h)
PRACTICE_LEVEL_STARTS = 0x0C
TRACE = 60
STICK = dict(x=0, y=80)


def run(emu, sym, args):
    gi = sym("gameInfo")
    fc_addr = sym("frameCount")
    hand, ball, player = sym("hand"), sym("ball"), sym("player")
    requests = sym("requests")
    verify = Verify(emu, sym)
    mode = lambda: emu.r8(gi)  # noqa: E731
    level = lambda: emu.r8(gi + 1)  # noqa: E731
    fc = lambda: emu.r32(fc_addr)  # noqa: E731
    starts = lambda: emu.r32(sym("practice") + PRACTICE_LEVEL_STARTS)  # noqa: E731
    loads = lambda: emu.r32(sym("stateLoads"))  # noqa: E731
    saves = lambda: emu.r32(sym("stateSaves"))  # noqa: E731
    auto = lambda: (emu.r32(sym("stateSlots") + SLOT_SIZE * SLOT_AUTO + 4),  # noqa: E731
                    emu.r8(sym("stateSlots") + SLOT_SIZE * SLOT_AUTO + 9))

    def request(action):
        emu.w16(requests, emu.r16(requests) | 1 << action)

    def state():
        s = [emu.r32(sym("rseed")), emu.r32(player + 0x18), emu.r16(sym("timer321"))]
        for actor in (hand, ball):
            s += [emu.r32(actor + o) for o in (0x34, 0x38, 0x3C, 0x58, 0x5C, 0x60)]
        return tuple(s)

    def collect(n):
        """{game frame: {states}} for game frames 1 to N of the level."""
        seen = {}
        for _ in range(n * 12):
            f = fc()
            if f > n:
                return seen
            if mode() != 4:
                emu.fail(f"the game left the level (mode {mode()}) while the first frames were recorded")
            if f >= 1:
                seen.setdefault(f, set()).add(state())
            yield 1
        emu.fail("the level did not run")

    def same(a, b, what):
        common = [f for f in a if f in b]
        bad = [f for f in sorted(common) if not a[f] & b[f]]
        if len(common) < 50:
            emu.fail(f"{what}: only {len(common)} common game frames")
        if bad:
            emu.fail(f"{what}: game frame {bad[0]} differs: {sorted(a[bad[0]])[0]} and {sorted(b[bad[0]])[0]}")

    def goto(lv):
        emu.w8(sym("run") + RUN_WARP_LEVEL, lv)
        request(ACTION_WARP)
        yield from wait_for(emu, lambda: level() == lv and mode() == 4 and 0 <= fc() < 400, 4000, f"level {lv}")

    def menu_to_reset_item():
        yield from press(emu, *C_ALL)
        if not emu.r8(sym("menuOpen")):
            emu.fail("the menu did not open")
        yield from press(emu, "A")  # Warp
        for _ in range(10):
            yield from press(emu, "DOWN")  # to "Reset"

    for i in layout.SETTINGS_BINDINGS:
        emu.w8(sym("settings") + i, 0)
    emu.w8(sym("stateVerify"), 1)
    yield from wait_for(emu, lambda: level() == trace.PRESENT and fc() >= 200, 4000, "the logos")
    yield from press(emu, "START", frames=6)
    yield from wait_for(emu, lambda: level() == trace.FLYTHRU and mode() == 4 and fc() >= 30, 3000, "the title")
    yield from press(emu, *C_ALL)
    for b in ("A", "DOWN", "DOWN", "A"):
        yield from press(emu, b)
    yield from wait_for(emu, lambda: level() == trace.ATLANTIS1 and mode() == 4 and 0 < fc() < 20, 3000, "Atlantis 1")
    emu.hold(**STICK)

    # 1. The setting is off: the level start saves nothing, and Reset loads the level the usual way.
    yield from wait_for(emu, lambda: fc() >= 80, 1000, "game frame 80")
    if saves() != 0 or auto()[0] != 0:
        emu.fail(f"a state was saved at the level start with the setting off ({saves()} saves)")
    s0 = starts()
    request(ACTION_RESET)
    yield from wait_for(emu, lambda: starts() == s0 + 1, 3000, "the normal reset")
    yield from wait_for(emu, lambda: level() == trace.ATLANTIS1 and mode() == 4 and fc() >= 20, 3000, "the level again")
    if loads() != 0 or saves() != 0:
        emu.fail("the normal reset used a state")
    print("instantreset: the setting off: no state at the level start, and the normal reset works")

    # 2. Turn it on in the menu (Warp, last item), and save the settings.
    yield from menu_to_reset_item()
    yield from press(emu, "RIGHT")
    if emu.r8(sym("settings") + SETTINGS_INSTANT) != 1:
        emu.fail("the Reset item did not turn the instant reset on")
    yield from press(emu, "B")  # to the root menu
    yield from press(emu, "B")  # closed
    emu.hold(**STICK)

    # 3. No state belongs to this level yet, so the first Reset is the normal one, and the new level start saves.
    s0 = starts()
    request(ACTION_RESET)
    yield from wait_for(emu, lambda: starts() == s0 + 1, 3000, "the reset without a state")
    yield from wait_for(emu, lambda: level() == trace.ATLANTIS1 and mode() == 4 and saves() == 1, 1500,
                        "the state of the level start")
    size, lv = auto()
    if size == 0 or lv != trace.ATLANTIS1:
        emu.fail(f"the level start state: {size} bytes for level {lv}")
    verify.check("the level start state")
    print(f"instantreset: the level start state of Atlantis 1 took {size} bytes beside its base; "
          f"save {emu.r32(sym('stateSaveTicks'))} ticks")
    first = yield from collect(TRACE)

    # 4. Instant resets: no fade, the same game again.
    for n in range(3):
        yield from wait_for(emu, lambda: fc() >= 120 + 30 * n, 1500, "game frames before the reset")
        l0, s0, v0 = loads(), starts(), saves()
        request(ACTION_RESET)
        left = False
        for _ in range(600):
            if mode() != 4 or emu.r8(gi + 0x54) & 1:
                left = True
            if loads() > l0:
                break
            yield 1
        else:
            emu.fail("the instant reset did not load the state")
        if left:
            emu.fail("the instant reset left the level: it should not fade")
        if starts() != s0 + 1:
            emu.fail("the instant reset did not count as a level start")
        if saves() != v0:
            emu.fail("a state was saved again at the instant reset")
        second = yield from collect(TRACE)
        same(first, second, f"after instant reset {n + 1}")
        print(f"instantreset: instant reset {n + 1}: the game frames 1 to {TRACE} equal the level's first start; "
              f"load {emu.r32(sym('stateLoadTicks'))} ticks ({emu.r32(sym('stateLoadTicks')) / 46875:.0f} ms)")

    # 4c. A second real start in the same level: its state differs from the base a little. It is stored as it is where
    # it differs, with no compression, and the reset to it is as good.
    s0 = saves()
    yield from goto(trace.ATLANTIS1)
    yield from wait_for(emu, lambda: saves() > s0 and auto()[0] > 0, 1500, "the second level start state")
    size, lv = auto()
    verify.check("the second level start state")
    print(f"instantreset: the second level start state took {size} bytes beside its base ({lv}); "
          f"{emu.r32(sym('stateSaveBytes'))} bytes added to the pool, {emu.r32(sym('stateChanged'))} chunks changed, "
          f"quick {emu.r8(sym('stateQuick'))}; save {emu.r32(sym('stateSaveTicks'))} ticks "
          f"({emu.r32(sym('stateSaveTicks')) / 46875:.0f} ms)")
    if size > 21 * 0x4000:
        emu.fail(f"the second level start state took {size} bytes")
    restart = yield from collect(TRACE)
    same(first, restart, "after the second real start")
    yield from wait_for(emu, lambda: fc() >= 100, 1500, "game frames before the reset")
    l0 = loads()
    request(ACTION_RESET)
    yield from wait_for(emu, lambda: loads() > l0, 600, "the instant reset to the second state")
    same(restart, (yield from collect(TRACE)), "after the instant reset to the second level start state")
    ticks = emu.r32(sym("stateLoadTicks"))
    print(f"instantreset: instant reset to the second state: load {ticks} ticks ({ticks / 46875:.0f} ms)")

    # 4b. The reset keeps the game's RAM copy of the save (the files, the best times, the checksums and the Hi Scores
    # table), whatever the state holds. Each byte is changed (xor) before the reset and must be the same after it.
    yield from wait_for(emu, lambda: fc() >= 100, 1500, "game frames before the reset that keeps the save")
    kept = [("saveSlot", 7 * 0x1C), ("bestTimes", 30 * 6), ("eepromCRCS", 8 * 4), ("hiscoreTable", 48)]
    saved, changed = {}, {}
    for name, size in kept:
        saved[name] = emu.read(sym(name), size)
        changed[name] = bytes(b ^ 0x5A for b in saved[name])
        for i, b in enumerate(changed[name]):
            emu.w8(sym(name) + i, b)
    l0 = loads()
    request(ACTION_RESET)
    yield from wait_for(emu, lambda: loads() > l0, 600, "the instant reset that keeps the save")
    for name, size in kept:
        got = emu.read(sym(name), size)
        if got != changed[name]:
            emu.fail(f"the instant reset changed {name}: it must keep the game's copy, not the state's")
        for i, b in enumerate(saved[name]):  # put the real values back
            emu.w8(sym(name) + i, b)
    print("instantreset: the reset kept saveSlot, bestTimes, eepromCRCS and the Hi Scores table (changed before it)")

    # 4d. A reset in a pause, or while Glover dies, is the normal one: the state of the level start would cut off
    # what is going on.
    for what, addr, value in (("a pause", sym("gamePaused"), 1), ("a death", sym("handControl") + HAND_DEATH_TIMER, 30000)):
        yield from wait_for(emu, lambda: level() == trace.ATLANTIS1 and mode() == 4 and fc() >= 40, 1500, "the level")
        l0, s0 = loads(), starts()
        (emu.w8 if what == "a pause" else emu.w16)(addr, value)
        request(ACTION_RESET)
        yield 20
        (emu.w8 if what == "a pause" else emu.w16)(addr, 0)
        if loads() != l0:
            emu.fail(f"the instant reset loaded the level start state during {what}")
        yield from wait_for(emu, lambda: starts() > s0 and level() == trace.ATLANTIS1 and mode() == 4 and fc() >= 5,
                            3000, f"the normal reset during {what}")
        yield from wait_for(emu, lambda: auto()[0] > 0, 600, "the level start state")
    print("instantreset: a reset during a pause or a death is the normal one")

    # 4e. A level start whose state was not saved (the setting was off): the state of an older start in the same level
    # is not loaded.
    emu.w8(sym("settings") + SETTINGS_INSTANT, 0)
    s0, v0 = starts(), saves()
    request(ACTION_RESET)
    yield from wait_for(emu, lambda: starts() > s0 and level() == trace.ATLANTIS1 and mode() == 4 and fc() >= 40, 3000,
                        "the normal reset with the setting off")
    emu.w8(sym("settings") + SETTINGS_INSTANT, 1)
    if saves() != v0:
        emu.fail("a state was saved with the setting off")
    l0, s0 = loads(), starts()
    request(ACTION_RESET)
    yield from wait_for(emu, lambda: starts() > s0 and level() == trace.ATLANTIS1 and mode() == 4 and fc() >= 5, 3000,
                        "the reset to a start with no state")
    if loads() != l0:
        emu.fail("the instant reset loaded the state of an older level start")
    yield from wait_for(emu, lambda: saves() > v0 and auto()[0] > 0, 600, "the level start state")
    print("instantreset: the state of an older level start in the same level is not loaded")

    # 5. Another level gets its own state; the reset there is instant too.
    yield from goto(20)
    yield from wait_for(emu, lambda: auto()[1] == 20 and auto()[0] > 0, 1500, "the state of level 20")
    first20 = yield from collect(TRACE)
    yield from wait_for(emu, lambda: fc() >= 100, 1500, "game frames")
    l0 = loads()
    request(ACTION_RESET)
    yield from wait_for(emu, lambda: loads() > l0, 600, "the instant reset in level 20")
    same(first20, (yield from collect(TRACE)), "after the instant reset in level 20")
    print("instantreset: level 20: the same")

    # 6. Settings: save with the setting on, change to the default, load.
    emu.release()
    yield from press(emu, *C_ALL)
    for _ in range(8):
        yield from press(emu, "DOWN")  # Settings
    yield from press(emu, "A")
    yield from press(emu, "A")         # Save settings
    yield 120
    if emu.r8(sym("settingsStatus")) != 1:
        emu.fail(f"the settings status is {emu.r8(sym('settingsStatus'))}, not 1 (saved)")
    files = glob.glob(os.path.join(args.out, "save", "*.eep"))
    block = open(files[0], "rb").read()[0x1C0:0x200]
    hud_on = block[4] << 8 | block[5]
    if not hud_on & 0x8000:
        emu.fail(f"the saved block has hudOn {hud_on:#06x}: bit 15 (the instant reset) is clear")
    yield from press(emu, "DOWN")
    yield from press(emu, "DOWN")      # Default settings
    yield from press(emu, "A")
    if emu.r8(sym("settings") + SETTINGS_INSTANT) != 0:
        emu.fail("the default settings keep the instant reset on")
    yield from press(emu, "UP")        # Load settings
    yield from press(emu, "A")
    yield 120
    if emu.r8(sym("settings") + SETTINGS_INSTANT) != 1:
        emu.fail("the loaded settings lost the instant reset")
    print("instantreset: the setting is saved in the EEPROM block (hudOn bit 15) and loads again")

    # 7. Off again: Reset leaves through the fade.
    yield from press(emu, "B")         # the Settings menu to the root menu, on Settings
    for _ in range(2):
        yield from press(emu, "DOWN")  # Version, then Warp
    yield from press(emu, "A")
    for _ in range(10):
        yield from press(emu, "DOWN")
    yield from press(emu, "LEFT")
    if emu.r8(sym("settings") + SETTINGS_INSTANT) != 0:
        emu.fail("the Reset item did not turn the instant reset off")
    yield from press(emu, "B")
    yield from press(emu, "B")
    emu.hold(**STICK)
    yield from wait_for(emu, lambda: level() == 20 and mode() == 4 and fc() >= 30, 1500, "level 20")
    l0, s0, v0 = loads(), starts(), saves()
    request(ACTION_RESET)
    seen_exit = False
    for _ in range(3000):
        if emu.r8(gi + 0x54) & 1:
            seen_exit = True
        if starts() > s0 and mode() == 4 and fc() >= 5:
            break
        yield 1
    else:
        emu.fail("the normal reset did not restart the level")
    if not seen_exit or loads() != l0 or saves() != v0:
        emu.fail("with the setting off, Reset must leave through the game's exit and use no state")
    print("instantreset: with the setting off again, Reset loads the level the usual way")
