"""The menu's input, and the quick actions' stick, in Atlantis 1 (reached with Warp, Go from the PRESS START title).

1. The menu opens where it was closed: the same page and cursor.
2. A held right on Warp, Level, then down with no gap moves the cursor one row (the repeat starts again when the
   direction changes). A held down stops at the last row; a new press wraps.
3. Z with right on Level jumps to the first level of the next world, Z with left back to the start of a world.
4. Game, Cheats: three cheats chosen in one visit show "on" at once and all turn on once the menu closes.
5. Savestate, Clear all slots runs only when A is held: a short press keeps the states, a held A clears them. The
   recording's state stays.
6. The menu's chord with C-down or C-up a game frame early: the zoom and the look mode are put back.
7. A diagonal stick (the up-left notch) with D-pad down neither loads a state nor warps: only a stick held in one
   direction counts for a quick action. The stick of a quick action does not reach the game until it is let go.
8. Settings, Status: "saved" goes blank once a setting changes.
9. Bindings: B pressed to back out of a new binding keeps the old one; the menu's own chord is not taken; a
   combination that another action has moves to the new action."""
import layout
import menunav
import startlevel
import trace
from tools import C_ALL, press, wait_for

SLOT_SIZE = 24
SLOT_RECORD = 10
ACTION_SLOW_MOTION = 4
ACTION_HUD = 5
ACTION_RESET = 0
ACTION_RECORD = 10


def run(emu, sym, args):
    gi, fc = sym("gameInfo"), sym("frameCount")
    frame = lambda: emu.r32(fc)  # noqa: E731
    page = lambda: menunav.Page(emu, sym)  # noqa: E731
    run_ = sym("run")
    settings = sym("settings")

    def slot_size(n):
        return emu.r32(sym("stateSlots") + SLOT_SIZE * n + 4)

    def binding(action):
        buttons = emu.r16(settings + layout.SETTINGS_BIND_BUTTONS + 2 * action)
        where = emu.r8(settings + layout.SETTINGS_BIND_WHERE + action // 2) >> (action % 2 * 4) & 0xF
        return buttons, where

    yield from startlevel.level(emu, sym)

    # 1. The menu opens where it was closed (Warp, Go closed it).
    yield from press(emu, *C_ALL, top=False)
    p = page()
    if p.title != "Warp" or p.labels()[p.cursor] != "Go":
        emu.fail(f"the menu opened on {p.title!r} row {p.cursor}, not on Warp, Go")

    # 2. Repeat and direction changes.
    yield from menunav.to_item(emu, sym, "Level")
    emu.hold("RIGHT")
    yield 60
    emu.hold("DOWN")
    yield 8
    emu.release()
    yield 10
    if page().cursor != 1:
        emu.fail(f"held right, then down: the cursor moved to row {page().cursor}, not 1")
    emu.hold("DOWN")
    yield 300
    emu.release()
    yield 10
    p = page()
    if p.cursor != p.count - 1:
        emu.fail(f"a held down stopped on row {p.cursor} of {p.count}: it should stop at the last row")
    yield from press(emu, "DOWN")
    if page().cursor != 0:
        emu.fail("a new press on the last row did not wrap to the first")

    # 3. World jumps on Level.
    emu.w8(run_ + 4, trace.ATLANTIS1)
    yield from menunav.to_item(emu, sym, "Level")
    yield from press(emu, "Z", "RIGHT")
    after = emu.r8(run_ + 4)
    if after != 15:
        emu.fail(f"Z + right on ATLANTIS 1 chose level {after}, not 15 (the next world's first level)")
    yield from press(emu, "Z", "LEFT")
    if emu.r8(run_ + 4) != trace.ATLANTIS1:
        emu.fail(f"Z + left from level 15 chose level {emu.r8(run_ + 4)}, not the start of the world before")
    emu.w8(run_ + 4, trace.ATLANTIS1)

    # 4. Cheats.
    yield from menunav.top(emu, sym)
    yield from menunav.goto(emu, sym, "Game", "Cheats", "Infinite lives")
    yield from menunav.goto(emu, sym, "Low gravity")
    yield from menunav.goto(emu, sym, "Big ball")
    p = page()
    if emu.r32(sym("cheatActivated")):
        emu.fail("a cheat changed while the menu held the game")
    yield from press(emu, *C_ALL)
    yield 60
    want = (1 << 3) | (1 << 8) | (1 << 9)
    if emu.r32(sym("cheatActivated")) & want != want:
        emu.fail(f"three cheats chosen in one visit: cheatActivated {emu.r32(sym('cheatActivated')):#x}")
    yield from press(emu, *C_ALL, top=False)
    yield from menunav.goto(emu, sym, "Infinite lives")
    yield from menunav.goto(emu, sym, "Low gravity")
    yield from menunav.goto(emu, sym, "Big ball")
    yield from press(emu, *C_ALL)
    yield 60

    # 5. Clear all slots needs a held A. A recording gives the recording slot a state.
    emu.w16(sym("requests"), emu.r16(sym("requests")) | 1 << ACTION_RECORD)
    yield 120
    emu.w16(sym("requests"), emu.r16(sym("requests")) | 1 << ACTION_RECORD)
    yield 60
    yield from press(emu, *C_ALL)
    yield from menunav.top(emu, sym)
    yield from menunav.goto(emu, sym, "Savestate", "Save state")
    yield from wait_for(emu, lambda: slot_size(0) != 0, 600, "the save into slot 1")
    if not slot_size(SLOT_RECORD):
        emu.fail("the recording has no state")
    yield from press(emu, *C_ALL, top=False)
    yield from menunav.to_item(emu, sym, "Clear all slots")
    yield from press(emu, "A")
    if not slot_size(0):
        emu.fail("a short A on Clear all slots cleared the slots")
    yield from press(emu, "A", frames=80)
    if slot_size(0):
        emu.fail("a held A on Clear all slots did not clear slot 1")
    if not slot_size(SLOT_RECORD):
        emu.fail("Clear all slots cleared the recording's state")
    yield from press(emu, *C_ALL)
    yield 30

    # 6. The chord's early C buttons.
    zoom = emu.r16(sym("cameraZoom"))
    emu.hold("CDOWN")
    yield 6
    emu.hold(*C_ALL)
    yield 8
    emu.release()
    yield 10
    yield from press(emu, *C_ALL)
    yield 40
    if emu.r16(sym("cameraZoom")) != zoom:
        emu.fail(f"C-down early in the menu chord changed the zoom ({zoom} to {emu.r16(sym('cameraZoom'))})")
    emu.hold("CUP")
    yield 6
    emu.hold(*C_ALL)
    yield 8
    emu.release()
    yield 10
    yield from press(emu, *C_ALL)
    yield 40
    if emu.r32(sym("controlMethod")) != 1:
        emu.fail(f"C-up early in the menu chord left the look mode on (controlMethod {emu.r32(sym('controlMethod'))})")

    # 7. A diagonal stick and the stick of a quick action.
    loads = emu.r32(sym("stateLoads"))
    before = frame()
    yield from press(emu, "DOWN", x=-72, y=62)
    yield 60
    if emu.r32(sym("stateLoads")) != loads or emu.r8(gi + 1) != trace.ATLANTIS1 or frame() < before:
        emu.fail("the up-left notch with D-pad down started a quick action")
    hand = sym("hand")
    x0 = emu.r32(hand + 0x34)
    emu.hold("LEFT", x=0, y=-80)  # slow motion on, then the stick stays held
    yield 60
    emu.hold(x=0, y=-80)
    yield 60
    if emu.r32(hand + 0x34) != x0:
        emu.fail("the stick of a quick action moved Glover before it was let go")
    emu.release()
    yield 10
    for _ in range(3):
        yield from press(emu, "LEFT", x=0, y=-80)
    yield 20
    if emu.r8(run_ + 2):
        emu.fail("slow motion did not go back off")

    # 8. Settings status.
    yield from press(emu, *C_ALL)
    yield from menunav.top(emu, sym)
    yield from menunav.goto(emu, sym, "Settings", "Save settings")
    yield from wait_for(emu, lambda: emu.r8(sym("settingsStatus")) == 1, 600, "Save settings")
    yield from menunav.to_item(emu, sym, "Menu opacity")
    yield from press(emu, "LEFT")
    yield 10
    if emu.r8(sym("settingsStatus")) != 0:
        emu.fail("Status still says saved after a setting changed")
    yield from press(emu, "RIGHT")

    # 9. Bindings.
    yield from menunav.top(emu, sym)
    yield from menunav.goto(emu, sym, "Bindings", "Slow motion")
    old = binding(ACTION_SLOW_MOTION)
    yield from press(emu, "B")
    yield 20
    if binding(ACTION_SLOW_MOTION) != old:
        emu.fail(f"B in a new binding changed it: {old} to {binding(ACTION_SLOW_MOTION)}")
    yield from press(emu, "A")
    yield from press(emu, *C_ALL)
    yield 20
    if binding(ACTION_SLOW_MOTION) != old or not emu.r8(sym("menuOpen")):
        emu.fail("the menu's chord was taken as a binding")
    yield from menunav.to_item(emu, sym, "Hide HUD")
    reset = binding(ACTION_RESET)
    yield from press(emu, "A")
    yield from press(emu, "UP", x=0, y=80)
    yield 20
    if binding(ACTION_HUD) != reset:
        emu.fail(f"Hide HUD did not take Reset level's binding: {binding(ACTION_HUD)}, {reset}")
    if binding(ACTION_RESET)[0] != 0:
        emu.fail("two actions share one combination")
    yield from press(emu, *C_ALL)
