"""The settings save: in the menu, the L switch of the Game menu on, Ball controls to Normal, Menu opacity 25%, Menu
position right and top, then Settings, Save. The emulator's EEPROM file must then hold the settings in blocks 56 to 63
(bytes 0x1C0 to 0x1FF), with Normal ball controls, the L switch and the menu look, and the game's own blocks must hold
its save."""
import glob
import os

import layout
import trace
from menunav import goto, to_item

C_ALL = ("CUP", "CDOWN", "CLEFT", "CRIGHT")


def press(emu, *buttons):
    emu.hold(*buttons)
    yield 8
    emu.release()
    yield 8


def run(emu, sym, args):
    gi = sym("gameInfo")
    # The title flythrough is a demo playback, which replaces the controller's data: skip the logos with Start
    # and open the menu on the title menu, as in menu.py.
    while not (emu.r8(gi + 1) == trace.PRESENT and emu.r32(sym("frameCount")) >= 200):
        yield 1
    yield from press(emu, "START")
    yield 600
    yield from press(emu, *C_ALL)
    if not emu.r8(sym("menuOpen")):
        emu.fail("the menu did not open")
    # Game, Controller 2, "Controller 2: L marks all done": on.
    yield from goto(emu, sym, "Game", "Controller 2", "Controller 2: L marks all done")
    for _ in range(2):
        yield from press(emu, "B")
    if not emu.r8(sym("settings") + layout.SETTINGS_DEBUG_PAD_L):
        emu.fail("the L switch did not turn on")
    yield from to_item(emu, sym, "Ball controls")
    yield from press(emu, "RIGHT")     # Normal
    yield from goto(emu, sym, "Settings")
    if [emu.r8(sym("settings") + o) for o in (layout.SETTINGS_MENU_OPACITY, layout.SETTINGS_MENU_ACROSS,
                                              layout.SETTINGS_MENU_DOWN)] != [5, 2, 2]:
        emu.fail("the menu look does not start at 85%, centre, middle")
    yield from to_item(emu, sym, "Menu opacity")
    for _ in range(3):
        yield from press(emu, "LEFT")  # 75%, 50%, 25%
    yield from to_item(emu, sym, "Menu position across")
    yield from press(emu, "RIGHT")     # right
    yield from to_item(emu, sym, "Menu position down")
    yield from press(emu, "LEFT")      # top
    if [emu.r8(sym("settings") + o) for o in (layout.SETTINGS_TRAINER_LOG, layout.SETTINGS_TRAINER_HOLD)] != [2, 2]:
        emu.fail("the trainer log does not start at 5 lines and a 1 s hold")
    yield from to_item(emu, sym, "Trainer log length")
    yield from press(emu, "RIGHT")     # 6
    yield from to_item(emu, sym, "Trainer result hold")
    yield from press(emu, "LEFT")      # off
    yield from goto(emu, sym, "Save settings")
    yield 120
    status = emu.r8(sym("settingsStatus"))
    if status != 1:
        emu.fail(f"the settings status is {status}, not 1 (saved)")
    files = glob.glob(os.path.join(args.out, "save", "*.eep"))
    if not files:
        emu.fail("the emulator wrote no EEPROM file")
    data = open(files[0], "rb").read()
    block = data[0x1C0:0x200]
    if block[:2] != layout.SAVED_MAGIC or block[layout.SAVED_FLAGS] & 0x81 != 0x81:
        emu.fail(f"blocks 56 to 63 hold {block[:8].hex()}")
    if list(block[layout.SAVED_MENU_LOOK:layout.SAVED_MENU_LOOK + 3]) != [2, 3, 1]:
        emu.fail(f"the saved menu look is {list(block[layout.SAVED_MENU_LOOK:layout.SAVED_MENU_LOOK + 3])}, not [2, 3, 1]")
    if block[layout.SAVED_TRAINER] != 0x13:
        emu.fail(f"the saved trainer settings are {block[layout.SAVED_TRAINER]:#x}, not 0x13 (6 lines, hold off)")
    if not any(data[8:0x1C0]):
        emu.fail("the game's own save blocks are empty")
