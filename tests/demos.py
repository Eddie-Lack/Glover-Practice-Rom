"""The game's demo recordings, from the Warp menu (practice/src/demo.c; its last three items: Demo, Watch demo, Watch
all demos).

1. From the title flythrough: Watch demo with Carnival 2A chosen starts that level as the title does: autoPlayback 2,
   save slot 6, Medium, a recording loaded. The glove moves with the recording's input (the stick is never touched).
2. The practice menu opens in the demo (all four C buttons): the game waits, the demo goes on afterwards. The
   demo does not count for a practice best time.
3. A in the demo leaves it for the title: autoPlayback 0, the recording freed, mode 1.
4. From a level (Atlantis 1 by the Warp menu's Go): Watch demo with Carnival 1 (a recording the title never shows)
   leaves the level and plays it. The Warp menu's Go in the demo starts a normal level (autoPlayback 0, no recording).
5. Watch all demos from a level plays the eight in order (Atlantis 1, Carnival 2A, Pirates 1, Prehistoric 1A,
   Fear 1A, Carnival 1, Space 1, the Space boss cutscene), each started when the one before has ended (the test sets
   exitCount, which the game's own playback code counts down to the end), then the title.
6. Watch all demos, then A during the second one: the demo and Play all stop, and the third does not start.
7. Watch all demos from a level starts with Atlantis 1."""
import trace
from tools import C_ALL, press, wait_for

DEMO_CHOICE, DEMO_REQUEST, DEMO_ALL, DEMO_INDEX, DEMO_START, DEMO_ADVANCE = 0, 1, 2, 3, 4, 5
BT_TAINT, BT_OTHER = 0x01, 1 << 7
ACTOR_POS = 0x34
MODE_MENU, MODE_LEVEL, MODE_SUB_LEVEL = 1, 4, 8
ORDER = [10, 16, 20, 25, 30, 15, 35, 39]
ITEM_DEMO = 11  # the Warp menu's 12th item; Watch demo and Watch all demos follow


def run(emu, sym, args):
    gi = sym("gameInfo")
    fc = lambda: emu.r32(sym("frameCount"))  # noqa: E731
    mode = lambda: emu.r8(gi)  # noqa: E731
    level = lambda: emu.r8(gi + 1)  # noqa: E731
    auto = lambda: emu.r32(sym("autoPlayback"))  # noqa: E731
    keys = lambda: emu.r32(sym("recordKeys"))  # noqa: E731
    slot = lambda: emu.r16(sym("currentSaveSlot"))  # noqa: E731
    demo = lambda off: emu.r8(sym("demo") + off)  # noqa: E731
    hand = sym("hand")
    pos = lambda: [emu.rf(hand + ACTOR_POS + 4 * i) for i in range(3)]  # noqa: E731
    menu_open = lambda: emu.r8(sym("menuOpen"))  # noqa: E731

    def warp_item(item, rights=0):
        """The Warp menu (the root's first item), down to ITEM, then RIGHT RIGHTS times, or A when RIGHTS is 0."""
        yield from press(emu, *C_ALL)
        if not menu_open():
            emu.fail("the menu did not open")
        yield from press(emu, "A")
        for b in ["DOWN"] * item + (["RIGHT"] * rights or ["A"]):
            yield from press(emu, b)

    def playing(lv, what):
        yield from wait_for(emu, lambda: level() == lv and mode() == MODE_LEVEL and auto() == 2 and fc() >= 120,
                            3000, what)

    def at_title():
        return mode() == MODE_MENU and auto() == 0

    top = [0]

    def end_demo():
        """The game's own end: the playback code counts exitCount down and then leaves with the quit exit."""
        top[0] = max(top[0], emu.r32(sym("player") + 0x08))
        emu.w16(sym("exitCount"), 3)
        yield 1

    yield from wait_for(emu, lambda: level() == trace.PRESENT and fc() >= 200, 4000, "the logos")
    yield from press(emu, "START", frames=6)
    yield from wait_for(emu, lambda: level() == trace.FLYTHRU and mode() == MODE_LEVEL and fc() >= 30, 3000,
                        "the title flythrough")
    if emu.r8(sym("demo") + DEMO_ALL) or emu.r8(sym("demo") + DEMO_REQUEST):
        emu.fail("a demo is asked for before the menu did")

    # 1. Carnival 2A (choice 1) from the title.
    yield from warp_item(ITEM_DEMO, rights=1)  
    yield from press(emu, "DOWN")
    yield from press(emu, "A")
    yield from playing(16, "Carnival 2A as a demo")
    if slot() != 6 or emu.r8(sym("saveSlot") + 0) is None or keys() == 0:
        emu.fail(f"the demo's set-up: slot {slot()}, recordKeys {keys():#x}")
    p0 = pos()
    yield 300
    p1 = pos()
    moved = sum(abs(a - b) for a, b in zip(p0, p1))
    if moved < 20:
        emu.fail(f"the glove did not move with the recording ({moved:.1f} units in 300 video frames)")
    print(f"demos: Carnival 2A plays, the glove moved {moved:.0f} units")

    # 2. The menu in the demo.
    if not emu.r8(sym("bestTimesState") + BT_TAINT) & BT_OTHER:
        emu.fail("a demo is not marked as modified for the practice best times")
    yield from press(emu, *C_ALL)
    if not menu_open():
        emu.fail("the menu did not open in the demo")
    f0 = fc()
    yield 60
    if fc() != f0 or auto() != 2:
        emu.fail(f"the game did not wait for the menu (frames {f0} to {fc()}, autoPlayback {auto()})")
    yield from press(emu, *C_ALL)
    yield 60
    if menu_open() or fc() <= f0 or auto() != 2:
        emu.fail("the demo did not go on after the menu closed")

    # 3. A leaves it. The game's quit exit offers the score to the hi-score table.
    scores = emu.read(sym("hiscoreTable"), 0x30)

    yield from press(emu, "A", frames=6)
    yield from wait_for(emu, lambda: at_title() and keys() == 0, 1500, "the title after A")
    if demo(DEMO_ALL) or demo(DEMO_ADVANCE):
        emu.fail("Play all or advance is set after A")
    if emu.read(sym("hiscoreTable"), 0x30) != scores:
        emu.fail("the demo's quit changed the hi-score table")
    yield from wait_for(emu, lambda: mode() == MODE_MENU and fc() >= 60, 3000, "the title to run again")

    # 4. From a level: Atlantis 1 by Go, then Carnival 1 (choice 5).
    yield from warp_item(2)
    yield from wait_for(emu, lambda: level() == 10 and mode() == MODE_LEVEL and auto() == 0 and fc() >= 100, 3000,
                        "Atlantis 1")
    yield from warp_item(ITEM_DEMO, rights=4)  # the choice was left on Carnival 2A: 1 + 4
    if demo(DEMO_CHOICE) != 5:
        emu.fail(f"the demo choice is {demo(DEMO_CHOICE)}, not 5")
    yield from press(emu, "DOWN")
    yield from press(emu, "A")
    yield from playing(15, "Carnival 1 as a demo from a level")
    if slot() != 6 or keys() == 0:
        emu.fail("the demo from a level was not set up")
    p0 = pos()
    yield 700
    if sum(abs(a - b) for a, b in zip(p0, pos())) < 20:
        emu.fail("the glove did not move in Carnival 1")
    # Go in the demo: a normal Atlantis 1 (the Level choice is still Atlantis 1).
    yield from warp_item(2)
    yield from wait_for(emu, lambda: level() == 10 and mode() == MODE_LEVEL and fc() >= 100, 3000, "Go in a demo")
    if auto() != 0 or keys() != 0:
        emu.fail(f"Go in a demo kept the demo: autoPlayback {auto()}, recordKeys {keys():#x}")

    scores = emu.read(sym("hiscoreTable"), 0x30)
    # 5. Watch all demos from the level (7 as well): Atlantis 1 first, then each in order.
    yield from warp_item(ITEM_DEMO + 2)
    seen = []
    yield from playing(ORDER[0], "Atlantis 1, the first of all")
    seen.append(level())
    for want in ORDER[1:]:
        if not demo(DEMO_ALL):
            emu.fail(f"Play all stopped before level {want}")
        yield 60
        yield from end_demo()
        yield from playing(want, f"level {want} after the one before")
        seen.append(level())
    if demo(DEMO_INDEX) != 7:
        emu.fail(f"Play all is at index {demo(DEMO_INDEX)} in the last demo")
    yield 60
    yield from end_demo()
    # The cutscene ends in mode 8 on its way to Space boss 3: the practice code turns that into the title.
    yield from wait_for(emu, lambda: at_title() and not demo(DEMO_ALL) and not demo(DEMO_ADVANCE), 2000,
                        "the title after the last demo")
    yield 200
    if not at_title() or emu.r32(sym('levelSelectCount')) == 0 and mode() != MODE_MENU:
        emu.fail(f"after the cutscene: mode {mode()}, level {level()}")
    if seen != ORDER:
        emu.fail(f"the order was {seen}")
    if emu.read(sym("hiscoreTable"), 0x30) != scores:
        emu.fail("a demo changed the hi-score table")
    print(f"demos: Watch all played {seen}, then the title (the most any demo scored at its end: {top[0]})")

    # 6. Watch all demos from the title; A in the second one stops everything.
    yield from wait_for(emu, lambda: mode() == MODE_MENU and fc() >= 60, 3000, "the title")
    yield from warp_item(ITEM_DEMO + 2)
    yield from playing(ORDER[0], "Atlantis 1 from the title")
    yield 40
    yield from end_demo()
    yield from playing(ORDER[1], "the second demo")
    yield from press(emu, "A", frames=6)
    yield from wait_for(emu, lambda: at_title() and not demo(DEMO_ALL), 1500, "the title after A in Play all")
    yield 400
    if level() in ORDER and mode() == MODE_LEVEL and auto() == 2:
        emu.fail(f"a demo started after A: level {level()}")
    print("demos: A stops Play all")
