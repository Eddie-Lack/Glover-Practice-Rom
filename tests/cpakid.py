"""Controller Pak storage, a pak whose first copy of the ID block is damaged and whose other three are good, with a
note of another game on it. The pak counts as formatted: the check finds no practice note, offers a repair and no
format (the Format row does nothing). A save works and keeps the other note. The repair asks first (a decline writes
nothing), then writes the good block to all four places."""
import layout
import pakfile
from cpakutil import activate, cursor_to, foreign_state, mpk, open_view, pak, press, settle, status, wait_idle

PAKS = "1"


def prepare(path, args):
    p = pakfile.format_pak()
    pakfile.add_note(p, b"NABE", b"01", "OTHER GAME", bytes(range(256)) * 2 + b"\x55" * 100)
    p[0x20:0x40] = bytes(range(32))
    pakfile.write_pak(path, p)


def run(emu, sym, args):
    c = sym("cpak")
    start = bytes(pakfile.read_pak(mpk(args)))
    base = foreign_state(pakfile.read_pak(mpk(args)))
    yield from open_view(emu, sym)
    yield from wait_idle(emu, sym)
    if status(emu, sym) != layout.CPAK_EMPTY:
        emu.fail(f"the check gave status {status(emu, sym)}, not {layout.CPAK_EMPTY}")
    if emu.r8(c + layout.CPAK_CAN_FORMAT) or not emu.r8(c + layout.CPAK_CAN_REPAIR):
        emu.fail("the pak should be offered a repair and no format")
    yield from cursor_to(emu, sym, layout.ROW_FORMAT)
    yield from press(emu, "A")
    if emu.r8(c + layout.CPAK_CONFIRM):
        emu.fail("Format asked for a confirmation on a pak with good ID blocks")

    yield from activate(emu, sym, layout.ROW_SAVE, True)
    if status(emu, sym) != layout.CPAK_SAVED:
        emu.fail(f"the save gave status {status(emu, sym)}")
    yield from settle(emu)
    p = pak(emu, args)
    if bytes(p[0:0x100]) != start[0:0x100]:
        emu.fail("the save changed the ID area")
    if foreign_state(p) != base:
        emu.fail("the save changed the other note")
    if not emu.r8(c + layout.CPAK_CAN_REPAIR):
        emu.fail("the save cleared the repair offer, but the ID block is still damaged")

    before = bytes(pak(emu, args))
    yield from cursor_to(emu, sym, layout.ROW_REPAIR)
    yield from press(emu, "A")
    if emu.r8(c + layout.CPAK_CONFIRM) != layout.CPAK_REQ_REPAIR:
        emu.fail("Repair did not ask for a confirmation")
    yield from press(emu, "B")
    yield from settle(emu, 60)
    if emu.r8(c + layout.CPAK_CONFIRM) or bytes(pak(emu, args)) != before:
        emu.fail("declining the repair still changed something")
    yield from press(emu, "A")
    yield from press(emu, "A")
    yield from wait_idle(emu, sym)
    if status(emu, sym) != layout.CPAK_REPAIRED:
        emu.fail(f"the repair gave status {status(emu, sym)}")
    yield from settle(emu)
    p = pak(emu, args)
    try:
        pakfile.check_format(p)
    except pakfile.PakError as e:
        emu.fail(f"after the repair: {e}")
    if foreign_state(p) != base:
        emu.fail("the repair changed the other note")
    ours = [e for e in pakfile.note_entries(p).values() if e[0] == pakfile.NOTE_CODE]
    if len(ours) != 1:
        emu.fail("the repair lost the practice note")
    pakfile.decode_practice(pakfile.note_data(p, ours[0][2]))
    yield from activate(emu, sym, layout.ROW_CHECK)
    if status(emu, sym) != layout.CPAK_FOUND or emu.r8(c + layout.CPAK_CAN_REPAIR):
        emu.fail("the check after the repair should find the note and no damage")
