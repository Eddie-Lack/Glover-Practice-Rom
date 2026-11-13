"""The most commands the overlay list (practice/src/practice.c) holds on one frame, in the levels with the most
faces, with every collision view switch on, the practice menu open on each of its pages and every HUD text on.
The list has OVERLAY_SIZE commands for each frame buffer. The peak must stay under 80% of it (a margin of one quarter
over the peak), and the list must never be cut (Practice.overlayFull stays 0). The collision view draws into its own
lists, so only text is in this one. One more pass has every trainer and the frame log on (COMBINED below).
Prints the peak of each level and menu page."""
import layout
import offsets
import trace
from tools import C_ALL, press, wait_for

RUN_WARP_LEVEL = 0x04
RUN_COLL_VIEW = 0x2C     # collView, collRadius, collSpheres
RUN_COLL_ENEMIES = 0x3B
RUN_COLL_EDGES = 0x3D
RUN_COLL_HOLES = 0x4C
RUN_COLL_WIRE, RUN_COLL_INFO = 0x55, 0x56
ACTION_WARP = 1
PRACTICE_OVERLAY_PEAK, PRACTICE_OVERLAY_FULL = 0x14, 0x18
READY = 2
ST_STATE = 0x10
LEVELS = (("Atlantis 3", trace.ATLANTIS1 + 2), ("Carnival 1", trace.ATLANTIS1 + 5),
          ("Pirates 3", trace.ATLANTIS1 + 12), ("Out of this World 3", trace.ATLANTIS1 + 27))
# The combined frame: the last level again, with every trainer and the frame log on from before the level loads. The
# trainers' panels add about 800 commands to the menu's busiest pages. It is more than one practice session shows, so
# the 80% rule does not apply to it: the list must hold it with COMBINED_ROOM commands to spare.
COMBINED = ("Out of this World 3, with every trainer and the frame log", trace.ATLANTIS1 + 27)
# Pages: (name, the rows to go down from the root menu, one for each menu level). Views are items too.
PAGES = (("menu root", ()), ("Warp", (0,)), ("Savestate", (1,)), ("HUD", (2,)), ("Frame advance", (3,)),
         ("Game", (4,)), ("Inspect", (5,)), ("Bindings", (7,)), ("Settings", (8,)),
         ("Collision view", (5, 6)), ("Hide objects", (5, 7)), ("Actors view", (5, 0)), ("Memory view", (5, 1)),
         ("Glove and ball view", (5, 2)), ("Frame log view", (5, 3)), ("Spawn actor view", (5, 8)),
         ("Pokes view", (5, 10, 0)), ("Save file view", (8, 14)), ("Controller Pak view", (8, 15)))
OVERLAY_SIZE = 4800  # keep in step with practice.c
LIMIT = 0.80
COMBINED_ROOM = 150


def run(emu, sym, args):
    gi = sym("gameInfo")
    fc = lambda: emu.r32(sym("frameCount"))  # noqa: E731
    mode = lambda: emu.r8(gi)  # noqa: E731
    level = lambda: emu.r8(gi + 1)  # noqa: E731
    run_addr, settings, practice = sym("run"), sym("settings"), sym("practice")
    stats, requests, menu_open = sym("collStats"), sym("requests"), sym("menuOpen")
    peak = lambda: emu.r32(practice + PRACTICE_OVERLAY_PEAK)  # noqa: E731

    def settle(n=4):
        start = fc()
        yield from wait_for(emu, lambda: fc() >= start + n or emu.r8(menu_open), 400, f"{n} game frames")
        yield 12

    yield from wait_for(emu, lambda: level() == trace.PRESENT and fc() >= 200, 4000, "the logos")
    yield from press(emu, "START", frames=6)
    yield from wait_for(emu, lambda: level() == trace.FLYTHRU and mode() == 4 and fc() >= 30, 3000, "the title")
    yield from press(emu, *C_ALL)
    for b in ("A", "DOWN", "DOWN", "A"):
        yield from press(emu, b)
    yield from wait_for(emu, lambda: level() == trace.ATLANTIS1 and mode() == 4 and fc() >= 100, 3000, "Atlantis 1")

    for i in range(layout.HUD_COUNT):
        emu.w8(settings + layout.SETTINGS_HUD + layout.HUD_PLACE_SIZE * i, 1)
    emu.w8(run_addr + RUN_COLL_VIEW, 1)      # see-through
    emu.w8(run_addr + RUN_COLL_VIEW + 1, 0)  # the whole level
    emu.w8(run_addr + RUN_COLL_VIEW + 2, 1)  # spheres
    for off in (RUN_COLL_ENEMIES, RUN_COLL_EDGES, RUN_COLL_HOLES, RUN_COLL_INFO):
        emu.w8(run_addr + off, 1)
    emu.w8(run_addr + RUN_COLL_WIRE, 1)

    ro = offsets.offsets("actions.h", "Run", ["trainers", "frameLog"])
    worst = combined = 0
    for name, lv in (COMBINED,) + LEVELS:  # not next to the same level: a warp to the level that runs is not seen
        extreme = (name, lv) == COMBINED
        emu.w8(run_addr + ro["trainers"], 0x5F if extreme else 0)
        emu.w8(run_addr + ro["frameLog"], 1 if extreme else 0)
        emu.w8(run_addr + RUN_WARP_LEVEL, lv)
        emu.w16(requests, emu.r16(requests) | 1 << ACTION_WARP)
        yield from wait_for(emu, lambda: level() == lv and mode() == 4 and fc() > 30, 8000, name)
        yield from wait_for(emu, lambda: emu.r8(stats + ST_STATE) == READY, 3000, f"the cache of {name}")
        emu.w32(practice + PRACTICE_OVERLAY_PEAK, 0)
        yield from settle(30)
        print(f"overlay {name}: peak {peak()} commands with the HUD and face info")
        if extreme:
            combined = max(combined, peak())
        else:
            worst = max(worst, peak())
        # The menu, on each page. The cursor of a menu stays where it was left.
        yield from press(emu, *C_ALL, frames=12)
        if not emu.r8(menu_open):
            emu.fail(f"the menu did not open in {name}")
        cursor = {}  # the cursor of each menu, by the path to it; a menu keeps it when left
        for page, path in PAGES:
            here = ()
            for row in path:
                want = cursor.get(here, 0)
                for _ in range(abs(row - want)):
                    yield from press(emu, "DOWN" if row > want else "UP", frames=4)
                cursor[here] = row
                yield from press(emu, "A", frames=4)
                here += (row,)
            emu.w32(practice + PRACTICE_OVERLAY_PEAK, 0)
            yield 20
            print(f"overlay {name}, {page}: peak {peak()} commands")
            if extreme:
                combined = max(combined, peak())
            else:
                worst = max(worst, peak())
            for _ in path:
                yield from press(emu, "B", frames=4)
        yield from press(emu, "B", frames=4)
        if emu.r8(menu_open):
            emu.fail(f"the menu did not close in {name}")
    full = emu.r32(practice + PRACTICE_OVERLAY_FULL)
    print(f"overlay: worst peak {worst} of {OVERLAY_SIZE} commands ({100 * worst // OVERLAY_SIZE}%), cut {full} times")
    print(f"overlay: combined worst peak {combined} of {OVERLAY_SIZE} commands ({100 * combined // OVERLAY_SIZE}%)")
    if combined > OVERLAY_SIZE - COMBINED_ROOM:
        emu.fail(f"the combined frame peaks at {combined} of {OVERLAY_SIZE}: less than {COMBINED_ROOM} commands left")
    if full:
        emu.fail(f"the overlay list was full and cut on {full} frames")
    if worst > OVERLAY_SIZE * LIMIT:
        emu.fail(f"the overlay list peaks at {worst} of {OVERLAY_SIZE}, over {int(LIMIT * 100)}%")
