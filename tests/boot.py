"""The practice ROM boots with the Expansion Pak: the practice segment is loaded and initialised, the overlay hook
runs on every frame, and the settings start as the defaults."""
PRACTICE_MAGIC = 0x50524143
SETTINGS_MAGIC = 0x47505253


def run(emu, sym, args):
    yield 800
    practice = sym("practice")
    if emu.r32(practice) != PRACTICE_MAGIC:
        emu.fail("PracticeInit did not run")
    if emu.r32(practice + 4) < 100:
        emu.fail(f"PracticePrintTimers ran on {emu.r32(practice + 4)} frames only")
    if emu.r32(sym("settings")) != SETTINGS_MAGIC:
        emu.fail("the settings are not set up")
    if emu.r32(sym("osMemSize")) != 0x800000:
        emu.fail(f"osMemSize is {emu.r32(sym('osMemSize')):#x}")
    if emu.r8(sym("menuOpen")):
        emu.fail("the menu is open")
    emu.shot("boot")
