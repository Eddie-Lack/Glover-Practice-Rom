"""The settings load at boot. Run after settings.py, with its save folder:

    harness.py settingsload --save-from build/tests/settings/save

The saved settings (Normal ball controls, the L switch, Menu opacity 25%, the menu at the right and top) must be in place on the title screen."""
import layout


def run(emu, sym, args):
    yield 300
    settings = sym("settings")
    if emu.r8(sym("settingsStatus")) != 2:
        emu.fail(f"the settings status is {emu.r8(sym('settingsStatus'))}, not 2 (loaded)")
    if emu.r8(settings + layout.SETTINGS_BALL_NORMAL) != 1:
        emu.fail("the saved ball controls setting did not load")
    if emu.r8(settings + layout.SETTINGS_DEBUG_PAD_L) != 1:
        emu.fail("the saved L switch did not load")
    if [emu.r8(settings + layout.SETTINGS_TRAINER_LOG), emu.r8(settings + layout.SETTINGS_TRAINER_HOLD)] != [3, 1]:
        emu.fail("the saved trainer log settings did not load")
    look = [emu.r8(settings + o) for o in (layout.SETTINGS_MENU_OPACITY, layout.SETTINGS_MENU_ACROSS,
                                           layout.SETTINGS_MENU_DOWN)]
    if look != [2, 3, 1]:
        emu.fail(f"the saved menu look loaded as {look}, not [2, 3, 1]")
