"""The team's early title menu (practice/include/earlytitle.h), opened from the Warp menu (its 9th and 10th items).

1. On the title flythrough: "Early title menu". The game thread calls TitleScreen in place of DoMenu: it runs every
   game frame, gameInfo.mode stays 1, level 7 is loaded and the three entries are drawn (early1_*.png, from both
   frame buffers).
2. "Back to the normal title": the early title stops and the game is in the state that follows the title
   flythrough (mode 1, level FLYTHRU, currentScreen 1). DoMenu runs and draws the normal title (normal1.png).
3. Warp, Go to Atlantis 1, then "Early title menu" from the level: the game's own Quit exit (fade out, free the
   level) leads to the early title.
4. The third entry (a stick down twice, then A) changes the mode to the level select; the early title is off.
   "Back to the normal title" works from there too.
5. The early title from the normal title, then A on the first entry, Continue: a level starts (mode 4), and the
   early title is off.
6. A warp (Go) from the early title starts the level, and the early title's texts are gone from the text list.
7. A request for the early title while the exit for the first one runs is refused (the game still reaches the early
   title once). A state loaded during that exit abandons it: the level goes on, "pending" is cleared, and the
   early title does not show.
"""
import trace
from tools import C_ALL, press, wait_for

ET_ON, ET_PENDING, ET_REQUEST, ET_FRAMES = 0x00, 0x01, 0x02, 0x04
ACTION_SAVE_STATE, ACTION_LOAD_STATE = 8, 9
TEXT_STR = 0x08  # Text.str
LEVEL_EARLY_TITLE = 7  # the level that TitleScreen loads
MODE_MENU, MODE_LEVEL_SELECT, MODE_LEVEL = 1, 2, 4


def run(emu, sym, args):
    gi = sym("gameInfo")
    fc = lambda: emu.r32(sym("frameCount"))  # noqa: E731
    mode = lambda: emu.r8(gi)  # noqa: E731
    level = lambda: emu.r8(gi + 1)  # noqa: E731
    et = sym("earlyTitle")
    et_on = lambda: emu.r8(et + ET_ON)  # noqa: E731
    et_frames = lambda: emu.r32(et + ET_FRAMES)  # noqa: E731
    menu_open = lambda: emu.r8(sym("menuOpen"))  # noqa: E731

    def shot(name):
        if args.gfx == "angrylion":
            emu.fb_png(name, sym("cfb1"))
            emu.fb_png(name + "_0", sym("cfb0"))

    def request(action):
        emu.w16(sym("requests"), emu.r16(sym("requests")) | 1 << action)

    def texts():
        """The strings of the game's text list."""
        head = sym("textList")
        out, p = set(), emu.r32(head)
        while p != head and len(out) < 200:
            raw = emu.read(p + TEXT_STR, 0x40)
            out.add(raw.split(b"\0")[0].decode("latin-1"))
            p = emu.r32(p)
        out.discard("")
        return out

    def warp_menu(item):
        """The Warp menu (1st item of the root menu, which opens on the item where it was left) and its item."""
        yield from press(emu, *C_ALL)
        if not menu_open():
            emu.fail("the menu did not open")
        yield from press(emu, "A")
        for b in ["DOWN"] * item + ["A"]:
            yield from press(emu, b)

    yield from wait_for(emu, lambda: level() == trace.PRESENT and fc() >= 200, 4000, "the logos")
    yield from press(emu, "START", frames=6)
    yield from wait_for(emu, lambda: level() == trace.FLYTHRU and mode() == MODE_LEVEL and fc() >= 30, 3000,
                        "the title flythrough")
    if et_on() or et_frames():
        emu.fail("the early title runs before it was asked for")

    # 1. The early title from the flythrough.
    yield from warp_menu(8)
    yield from wait_for(emu, lambda: et_on() and et_frames() > 10, 600, "the early title")
    yield from wait_for(emu, lambda: fc() > 60, 600, "the early title to run for two seconds")
    if mode() != MODE_MENU or level() != LEVEL_EARLY_TITLE or menu_open():
        emu.fail(f"the early title: mode {mode()}, level {level()}, menu {menu_open()}")
    n0 = et_frames()
    yield 30
    if et_frames() <= n0:
        emu.fail("TitleScreen is not called every game frame")
    for i in range(3):
        shot(f"early1_{i}")
        yield 11
    print(f"titlemenu: the early title runs in mode {mode()} over level {level()}, {et_frames()} calls")

    # 2. Back to the normal title.
    yield from warp_menu(9)
    yield from wait_for(emu, lambda: not et_on() and level() == trace.FLYTHRU and mode() == MODE_MENU, 600,
                        "the normal title")
    n1 = et_frames()
    yield 200
    if et_frames() != n1 or et_on():
        emu.fail("TitleScreen is still called after the way back")
    if emu.r32(sym("currentScreen")) != 1 or mode() != MODE_MENU or level() != trace.FLYTHRU:
        emu.fail(f"currentScreen {emu.r32(sym('currentScreen'))}, mode {mode()}, level {level()} after the way back")
    shot("normal1")

    # 3. From Atlantis 1.
    yield from warp_menu(2)  # Go
    yield from wait_for(emu, lambda: level() == trace.ATLANTIS1 and mode() == MODE_LEVEL and fc() >= 100, 3000,
                        "Atlantis 1")
    n2 = et_frames()
    yield from warp_menu(8)
    yield from wait_for(emu, lambda: et_on() and et_frames() > n2 + 10, 1500, "the early title from the level")
    yield from wait_for(emu, lambda: fc() > 40, 600, "the early title to run")
    if mode() != MODE_MENU or level() != LEVEL_EARLY_TITLE:
        emu.fail(f"the early title from a level: mode {mode()}, level {level()}")
    shot("early2")

    # 4. The third entry: the level select.
    for _ in range(2):
        yield from press(emu, x=0, y=-80, frames=12)
        yield 12
    yield 20
    shot("early3")
    yield from press(emu, "A")
    if emu.r8(sym("titleMenuSelected")) != 2:
        emu.fail(f"the stick chose entry {emu.r8(sym('titleMenuSelected'))}, not the third")
    yield from wait_for(emu, lambda: mode() == MODE_LEVEL_SELECT, 1000, "the level select")
    if et_on():
        emu.fail("the early title is still on in the level select")
    yield 60
    shot("select1")
    n3 = et_frames()
    yield from warp_menu(9)
    yield from wait_for(emu, lambda: level() == trace.FLYTHRU and mode() == MODE_MENU, 600,
                        "the normal title from the level select")
    yield 120
    if et_frames() != n3:
        emu.fail("TitleScreen ran after the way back from the level select")
    shot("normal2")

    # 5. The early title from the normal title, and its first entry.
    yield from warp_menu(8)
    yield from wait_for(emu, lambda: et_on() and fc() > 40, 1500, "the early title again")
    yield from press(emu, "A")
    yield from wait_for(emu, lambda: mode() == MODE_LEVEL and level() < trace.PRESENT and fc() >= 40, 2000,
                        "the level that Continue starts")
    if et_on():
        emu.fail("the early title is still on after Continue")
    print(f"titlemenu: Continue started level {level()}")
    shot("continue1")
    f1 = fc()
    yield 120
    shot("continue2")
    if mode() != MODE_LEVEL or fc() < f1 + 20:
        emu.fail(f"the level that Continue started does not run: mode {mode()}, game frame {f1} to {fc()}")

    # 6. A warp from the early title.
    yield from warp_menu(8)
    yield from wait_for(emu, lambda: et_on() and fc() > 40, 1500, "the early title from the level")
    titles = texts()
    if not titles:
        emu.fail("the early title has no texts in the text list")
    yield from warp_menu(2)  # Go
    yield from wait_for(emu, lambda: level() == trace.ATLANTIS1 and mode() == MODE_LEVEL and fc() >= 60, 3000,
                        "Atlantis 1 from the early title")
    if et_on():
        emu.fail("the early title is on after a warp")
    left = titles & texts()
    if left:
        emu.fail(f"the early title's texts are still in the text list after the warp: {sorted(left)}")
    f2 = fc()
    yield 120
    if mode() != MODE_LEVEL or fc() < f2 + 20:
        emu.fail("the level after the warp from the early title does not run")
    print(f"titlemenu: a warp from the early title started level {level()}; its {len(titles)} texts were cleared")

    # 7. A second request during the exit, and an exit that a state load abandons.
    request(ACTION_SAVE_STATE)
    yield 10
    yield from warp_menu(8)
    yield from wait_for(emu, lambda: emu.r8(et + ET_PENDING) == 1, 300, "the exit for the early title")
    emu.w8(et + ET_REQUEST, 1)
    yield from wait_for(emu, lambda: et_on() and fc() > 40, 1500, "the early title after a second request")
    yield from warp_menu(2)
    yield from wait_for(emu, lambda: level() == trace.ATLANTIS1 and mode() == MODE_LEVEL and fc() >= 60, 3000,
                        "Atlantis 1 again")
    request(ACTION_SAVE_STATE)
    yield 10
    yield from warp_menu(8)
    yield from wait_for(emu, lambda: emu.r8(et + ET_PENDING) == 1, 300, "the second exit for the early title")
    request(ACTION_LOAD_STATE)
    yield 150
    if emu.r8(et + ET_PENDING) or et_on() or mode() != MODE_LEVEL or level() != trace.ATLANTIS1:
        emu.fail(f"after the abandoned exit: pending {emu.r8(et + ET_PENDING)}, on {et_on()}, mode {mode()}, "
                 f"level {level()}")
    f3 = fc()
    yield 120
    if fc() < f3 + 20:
        emu.fail("the level does not run after the abandoned exit")
