"""The edited save loads at boot. Run after saveedit.py, with its save folder:

    harness.py saveeditload --save-from build/tests/saveedit/save

The game's boot checks each file's CRC and blanks a file whose CRC is wrong.
File 1 must hold the edit."""

def run(emu, sym, args):
    yield 600
    slot0 = sym("saveSlot")
    got = (emu.r32(slot0 + 4), emu.r32(slot0 + 8), emu.r8(slot0 + 0x10), emu.r8(slot0 + 0x12), emu.r8(slot0 + 0x13))
    if got != (7, 1, 5, 1, 1):
        emu.fail(f"file 1 after boot: levels, garibs, lives, won, returned = {got}, not (7, 1, 5, 1, 1)")
    if emu.r16s(sym("bestTimes")) < 3:
        emu.fail("the first best time did not load")
