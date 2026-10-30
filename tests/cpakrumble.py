"""Controller Pak storage with a Rumble Pak in the controller: a device that answers the status command and reads as
zeros, but keeps nothing that is written. The driver refuses it ("not a Controller Pak") for the check, a save and
a format, offers no format, and leaves the block it tested as it was."""
import layout
from cpakutil import activate, cursor_to, open_view, press, status, wait_idle

PAKS = ""
RUMBLE = "1"


def run(emu, sym, args):
    c = sym("cpak")
    yield from open_view(emu, sym)
    yield from wait_idle(emu, sym)
    if status(emu, sym) != layout.CPAK_NOT_A_PAK:
        emu.fail(f"the check of a Rumble Pak gave status {status(emu, sym)}, not {layout.CPAK_NOT_A_PAK}")
    if emu.r8(c + layout.CPAK_CAN_FORMAT) or emu.r8(c + layout.CPAK_CAN_REPAIR):
        emu.fail("a Rumble Pak was offered a format or a repair")
    yield from activate(emu, sym, layout.ROW_SAVE, True)
    if status(emu, sym) != layout.CPAK_NOT_A_PAK:
        emu.fail(f"a save to a Rumble Pak gave status {status(emu, sym)}")
    yield from cursor_to(emu, sym, layout.ROW_FORMAT)
    yield from press(emu, "A")
    if emu.r8(c + layout.CPAK_CONFIRM):
        emu.fail("Format asked for a confirmation on a Rumble Pak")
    # Even a format that is asked for by the program finds the device out first.
    emu.w8(c + layout.CPAK_REQUEST, layout.CPAK_REQ_FORMAT)
    emu.w8(c + layout.CPAK_WORKING, 1)
    yield from wait_idle(emu, sym)
    if status(emu, sym) != layout.CPAK_NOT_A_PAK:
        emu.fail(f"a format of a Rumble Pak gave status {status(emu, sym)}")
