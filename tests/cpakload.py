"""Controller Pak storage, a load in a second emulator. Run after cpak.py, with its save folder:

    harness.py cpakload --save-from build/tests/cpak/save

The new emulator boots with the settings at their defaults, then the Settings menu's Controller Pak view loads
the practice note that cpak.py saved. The settings, the Hi Scores choice, the best times, the trainer switches and
the recording must be those that were saved, and the pak file must not change.

Picture: cpakload_loaded.png (needs --gfx angrylion)."""
import layout
import pakfile
from cpakutil import RECORDING_BUFFER, cursor_to, mpk, open_view, press, status, wait_idle
from cpak import RUN_TRAINERS, SETTINGS_HUD_X, make_recording

PAKS = "1"


def run(emu, sym, args):
    c = sym("cpak")
    settings = sym("settings")
    before = open(mpk(args), "rb").read()
    yield from open_view(emu, sym)
    yield from wait_idle(emu, sym)
    if status(emu, sym) != layout.CPAK_FOUND or not emu.r8(c + layout.CPAK_HAS_NOTE):
        emu.fail(f"the check gave status {status(emu, sym)}, not {layout.CPAK_FOUND}")
    if emu.r8(settings + layout.SETTINGS_BALL_NORMAL) or emu.r8(sym("run") + RUN_TRAINERS):
        emu.fail("the settings did not start as the defaults")
    yield from cursor_to(emu, sym, layout.ROW_LOAD)
    yield from press(emu, "A")
    yield from wait_idle(emu, sym)
    if status(emu, sym) != layout.CPAK_LOADED:
        emu.fail(f"the load gave status {status(emu, sym)}, not {layout.CPAK_LOADED}")
    if not emu.r8(settings + layout.SETTINGS_BALL_NORMAL) or not emu.r8(settings + layout.SETTINGS_DEBUG_PAD_L):
        emu.fail("the saved switches did not load")
    if emu.r8(settings + SETTINGS_HUD_X) != 77 or emu.r16(settings + layout.SETTINGS_BIND_BUTTONS + 6) != 0x1234:
        emu.fail("the saved HUD place and binding did not load")
    if [emu.r8(settings + layout.SETTINGS_TRAINER_LOG), emu.r8(settings + layout.SETTINGS_TRAINER_HOLD)] != [4, 4]:
        emu.fail("the saved trainer log settings did not load from the pak")
    bests = [emu.r16(sym("practiceBests") + 2 * i) for i in range(30)]
    if bests[0] != 1234 or bests[7] != 4321 or bests[29] != 65535 or sum(1 for b in bests if b) != 3:
        emu.fail(f"the best times are {bests}")
    if emu.r8(sym("bestTimesState")) != 1:
        emu.fail("the Hi Scores choice did not load")
    if emu.r8(sym("run") + RUN_TRAINERS) != 5:
        emu.fail(f"the trainer switches are {emu.r8(sym('run') + RUN_TRAINERS)}, not 5")
    rec = make_recording()
    if emu.r32(sym("recording") + 4) != len(rec):
        emu.fail(f"the recording has {emu.r32(sym('recording') + 4)} frames, not {len(rec)}")
    data = emu.read(RECORDING_BUFFER, len(rec) * 4)
    for i, (button, x, y) in enumerate(rec):
        if tuple(data[i * 4:i * 4 + 4]) != (button >> 8, button & 255, x & 255, y & 255):
            emu.fail(f"frame {i} of the recording differs")
    yield 10
    if args.gfx == "angrylion":
        emu.fb_png("cpakload_loaded", sym("cfb1"))
        emu.fb_png("cpakload_loaded_0", sym("cfb0"))
    yield 60
    if open(mpk(args), "rb").read() != before:
        emu.fail("a load changed the pak file")
