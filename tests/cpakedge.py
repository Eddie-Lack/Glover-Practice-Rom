"""Controller Pak storage, the cases that must not write. Controller 1 has a pak that is not formatted (all zero),
controller 2 a pak whose 16 notes are all used.

Controller 1: the check says "not formatted"; a save writes nothing; a format asks for a confirmation, and a
decline writes nothing. A confirmed format makes a pak that passes the checks of pakfile.py, and a save to it then
works. Controller 2 (the view's first row): a save finds no free note and writes nothing, and a load finds no
practice note. The pak of controller 2 stays as it was throughout."""
import layout
import pakfile
from cpakutil import cursor_to, mpk, open_view, pak, press, status, wait_idle

PAKS = "12"


def prepare(path, args):
    pakfile.write_pak(path, bytearray(pakfile.PAK_SIZE), 0)
    full = pakfile.format_pak()
    for i in range(16):
        pakfile.add_note(full, b"NG%02d" % i, b"01", "NOTE %d" % i, bytes([i]) * 100)
    pakfile.write_pak(path, full, 1)
    for port in (2, 3):
        pakfile.write_pak(path, pakfile.format_pak(), port)


def run(emu, sym, args):
    c = sym("cpak")
    zero = bytes(pakfile.PAK_SIZE)
    full_before = bytes(pakfile.read_pak(mpk(args), 1))
    yield from open_view(emu, sym)
    yield from wait_idle(emu, sym)
    if status(emu, sym) != layout.CPAK_NOT_FORMATTED or not emu.r8(c + layout.CPAK_CAN_FORMAT):
        emu.fail(f"the check of the unformatted pak gave status {status(emu, sym)}")

    yield from cursor_to(emu, sym, layout.ROW_SAVE)
    yield from press(emu, "A")
    yield from press(emu, "A")
    yield from wait_idle(emu, sym)
    yield 120
    if status(emu, sym) != layout.CPAK_NOT_FORMATTED:
        emu.fail(f"a save to the unformatted pak gave status {status(emu, sym)}")
    if bytes(pak(emu, args)) != zero:
        emu.fail("a save wrote to a pak that is not formatted")

    yield from cursor_to(emu, sym, layout.ROW_FORMAT)
    yield from press(emu, "A")
    if emu.r8(c + layout.CPAK_CONFIRM) != layout.CPAK_REQ_FORMAT:
        emu.fail("Format did not ask for a confirmation")
    yield from press(emu, "B")
    yield 60
    if emu.r8(c + layout.CPAK_CONFIRM) or bytes(pak(emu, args)) != zero:
        emu.fail("declining the format still changed something")
    yield from press(emu, "A")
    yield from press(emu, "A")
    yield from wait_idle(emu, sym)
    if status(emu, sym) != layout.CPAK_FORMATTED:
        emu.fail(f"the format gave status {status(emu, sym)}")
    yield 120
    p = pak(emu, args)
    try:
        pakfile.check_format(p)
    except pakfile.PakError as e:
        emu.fail(f"the formatted pak: {e}")
    if pakfile.note_entries(p) or pakfile.free_pages(p) != 123:
        emu.fail("the formatted pak is not empty")
    if any(p[0x300:0x500]):
        emu.fail("the note table is not clear")

    # The driver's own format is good enough for the driver: a save, then a check from the file.
    yield from cursor_to(emu, sym, layout.ROW_SAVE)
    yield from press(emu, "A")
    yield from press(emu, "A")
    yield from wait_idle(emu, sym)
    if status(emu, sym) != layout.CPAK_SAVED:
        emu.fail(f"a save to the formatted pak gave status {status(emu, sym)}")
    yield 120
    p = pak(emu, args)
    pakfile.check_format(p)
    notes = pakfile.note_entries(p)
    if len(notes) != 1 or pakfile.decode_name(next(iter(notes.values()))[4]) != pakfile.NOTE_NAME:
        emu.fail("the saved note is not on the formatted pak")
    pakfile.decode_practice(pakfile.note_data(p, next(iter(notes.values()))[2]))
    # Format asks only for a pak that is not valid: with a valid one the row does nothing.
    yield from cursor_to(emu, sym, layout.ROW_FORMAT)
    yield from press(emu, "A")
    if emu.r8(c + layout.CPAK_CONFIRM):
        emu.fail("Format asked for a confirmation on a valid pak")

    # Controller 2: every note is used.
    yield from cursor_to(emu, sym, layout.ROW_PORT)
    yield from press(emu, "RIGHT")
    if emu.r8(c + layout.CPAK_PORT) != 1:
        emu.fail("right on the first row did not choose controller 2")
    yield from cursor_to(emu, sym, layout.ROW_CHECK)
    yield from press(emu, "A")
    yield from wait_idle(emu, sym)
    if status(emu, sym) != layout.CPAK_EMPTY or emu.r8(c + layout.CPAK_FREE_NOTES) != 0:
        emu.fail(f"the check of controller 2: status {status(emu, sym)}, {emu.r8(c + layout.CPAK_FREE_NOTES)} free notes")
    yield from cursor_to(emu, sym, layout.ROW_SAVE)
    yield from press(emu, "A")
    yield from press(emu, "A")
    yield from wait_idle(emu, sym)
    if status(emu, sym) != layout.CPAK_NO_ROOM:
        emu.fail(f"a save to the full pak gave status {status(emu, sym)}")
    yield from cursor_to(emu, sym, layout.ROW_LOAD)
    yield from press(emu, "A")
    yield from wait_idle(emu, sym)
    if status(emu, sym) != layout.CPAK_NO_NOTE:
        emu.fail(f"a load from a pak without the note gave status {status(emu, sym)}")
    yield 120
    if bytes(pak(emu, args, 1)) != full_before:
        emu.fail("the pak of controller 2 changed")
    # The pak of controller 1 holds the note, which the operations on controller 2 left alone.
    pakfile.check_format(pak(emu, args, 0))
