"""Reload refuses a cartridge whose data fails the game's CRC check. Run after saveedit.py, with its changed copy of
the save folder (one byte of file 2 was changed there):

    harness.py saveeditbad --save-from build/tests/saveedit/save_bad

The game blanks file 2 at boot. Reload reads the blocks into buffers of its own and finds the wrong CRC. The game's
copy and the CRC table stay as they were, and the edits stay. The editor says so."""
import layout
import trace
from menunav import goto
from tools import C_ALL, press

STATUS_BAD_CRC = 4


def run(emu, sym, args):
    gi = sym("gameInfo")
    se = sym("saveEdit")
    slots = sym("saveSlot")
    shadow = sym("saveEditSlots")
    while not (emu.r8(gi + 1) == trace.PRESENT and emu.r32(sym("frameCount")) >= 200):
        yield 1
    yield from press(emu, "START")
    yield 600
    # The game's copy differs from the cartridge's: a reload that takes any of the cartridge's data over shows.
    emu.w8(slots + 0x10, 42)
    emu.w16(sym("bestTimes"), 77)
    before = bytes(emu.read(slots, 0xA8))
    bests = bytes(emu.read(sym("bestTimes"), 0xC0))
    crcs = bytes(emu.read(sym("eepromCRCS"), 0x20))
    if before[0x1C] != 13:
        emu.fail(f"file 2 was not blanked at boot (first byte {before[0x1C]})")
    yield from press(emu, *C_ALL)
    yield from goto(emu, sym, "Settings", "Save file")
    # An edit in the editor's copy: the lives of file 1, two up.
    for b in ["DOWN", "RIGHT", "RIGHT"]:
        yield from press(emu, b)
    edited = emu.r8(shadow + 0x10)
    for b in ["DOWN"] * 6 + ["A", "A"]:  # Reload, asked and confirmed
        yield from press(emu, b)
    yield 400
    status = emu.r8(se + layout.SAVEEDIT_STATUS)
    if status != STATUS_BAD_CRC:
        emu.fail(f"the status after the reload is {status}, not {STATUS_BAD_CRC} (CRC check failed)")
    if bytes(emu.read(slots, 0xA8)) != before or bytes(emu.read(sym("bestTimes"), 0xC0)) != bests or \
            bytes(emu.read(sym("eepromCRCS"), 0x20)) != crcs:
        emu.fail("the failed reload changed the game's copy")
    if emu.r8(shadow + 0x10) != edited:
        emu.fail("the failed reload dropped the edits")
    if emu.r8(se + layout.SAVEEDIT_REQUEST):
        emu.fail("the refused reload is still pending")
