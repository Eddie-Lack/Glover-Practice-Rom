"""Controller Pak storage, a pak whose first index table is damaged and whose backup is good, with a note of another
game on it. The check finds the pak fine and offers a repair. A save reads the backup, works, and writes both tables
again: the pak then passes the strict checks of pakfile.py and the other note is as it was."""
import layout
import pakfile
from cpakutil import activate, foreign_state, mpk, open_view, pak, settle, status, wait_idle

PAKS = "1"


def prepare(path, args):
    p = pakfile.format_pak()
    pakfile.add_note(p, b"NABE", b"01", "OTHER GAME", bytes(range(256)) * 2 + b"\x55" * 100)
    p[pakfile.PAGE + 1] ^= 0x5A
    pakfile.write_pak(path, p)


def run(emu, sym, args):
    c = sym("cpak")
    p0 = pakfile.read_pak(mpk(args))
    p0[pakfile.PAGE:2 * pakfile.PAGE] = p0[2 * pakfile.PAGE:3 * pakfile.PAGE]
    base = foreign_state(p0)
    yield from open_view(emu, sym)
    yield from wait_idle(emu, sym)
    if status(emu, sym) != layout.CPAK_EMPTY:
        emu.fail(f"the check gave status {status(emu, sym)}, not {layout.CPAK_EMPTY}")
    if emu.r8(c + layout.CPAK_CAN_FORMAT) or not emu.r8(c + layout.CPAK_CAN_REPAIR):
        emu.fail("the pak should be offered a repair and no format")
    if emu.r16(c + layout.CPAK_FREE_PAGES) != 123 - 3 or emu.r8(c + layout.CPAK_FREE_NOTES) != 15:
        emu.fail("the check should read the backup: 120 free pages and 15 free notes")
    yield from activate(emu, sym, layout.ROW_SAVE, True)
    if status(emu, sym) != layout.CPAK_SAVED:
        emu.fail(f"the save gave status {status(emu, sym)}")
    yield from settle(emu)
    p = pak(emu, args)
    try:
        pakfile.check_format(p)
    except pakfile.PakError as e:
        emu.fail(f"after the save: {e}")
    if foreign_state(p) != base:
        emu.fail("the save changed the other note")
    if emu.r8(c + layout.CPAK_CAN_REPAIR):
        emu.fail("the save wrote both index tables, but the repair is still offered")
    yield from activate(emu, sym, layout.ROW_CHECK)
    if status(emu, sym) != layout.CPAK_FOUND or emu.r8(c + layout.CPAK_CAN_REPAIR):
        emu.fail("the check after the save should find the note and no damage")
