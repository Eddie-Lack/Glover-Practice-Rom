"""Settings saved before the menu look existed load with its defaults. Run after settings.py, with its save folder:

    harness.py settingsold --save-from build/tests/settings/save

prepare() zeroes the three bytes of the block that hold the menu look (they were unused HUD slots) and fixes the
block's checksum. The block must still load (status 2, the other settings in place) and the menu look must be 85%,
centre, middle."""
import glob
import os

import layout

BLOCK = 0x1C0


def checksum(block):
    s = 0x1234
    for b in block[:62]:
        s = ((s << 1 | s >> 15) & 0xFFFF) ^ b
    return s


def prepare(mpk, args):
    path = glob.glob(os.path.join(args.out, "save", "*.eep"))[0]
    data = bytearray(open(path, "rb").read())
    for i in range(3):
        data[BLOCK + layout.SAVED_MENU_LOOK + i] = 0
    data[BLOCK + layout.SAVED_TRAINER] = 0
    data[BLOCK + 62:BLOCK + 64] = checksum(data[BLOCK:BLOCK + 62]).to_bytes(2, "big")
    open(path, "wb").write(data)


def run(emu, sym, args):
    yield 300
    settings = sym("settings")
    if emu.r8(sym("settingsStatus")) != 2:
        emu.fail(f"the old block did not load (status {emu.r8(sym('settingsStatus'))})")
    if emu.r8(settings + layout.SETTINGS_BALL_NORMAL) != 1:
        emu.fail("the old block's ball controls did not load")
    if [emu.r8(settings + layout.SETTINGS_TRAINER_LOG), emu.r8(settings + layout.SETTINGS_TRAINER_HOLD)] != [2, 2]:
        emu.fail("the trainer log settings are not the defaults after an old block")
    look = [emu.r8(settings + o) for o in (layout.SETTINGS_MENU_OPACITY, layout.SETTINGS_MENU_ACROSS,
                                           layout.SETTINGS_MENU_DOWN)]
    if look != [5, 2, 2]:
        emu.fail(f"the menu look is {look}, not the defaults [5, 2, 2]")
