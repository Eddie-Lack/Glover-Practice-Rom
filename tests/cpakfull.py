"""Controller Pak storage on paks with little room. Controller 2's pak has no free page: a save says "no room".
Controller 1's pak has two free pages: a save with a long recording leaves the recording out ("saved, the recording
does not fit") and takes one page; a save with a short recording keeps it and takes two. The emulator writes only the
pak of controller 1 to its file: the writes are checked there."""
import layout
import pakfile
from cpakutil import RECORDING_BUFFER, cursor_to, mpk, open_view, pak, press, status, wait_idle

PAKS = "12"


def prepare(path, args):
    full = pakfile.format_pak()
    pakfile.add_note(full, b"NBIG", b"01", "BIG NOTE", b"\x33" * (123 * 256))
    pakfile.write_pak(path, full, 1)
    near = pakfile.format_pak()
    pakfile.add_note(near, b"NBIG", b"01", "BIG NOTE", b"\x44" * (121 * 256))
    pakfile.write_pak(path, near, 0)


def put_recording(emu, sym, runs):
    """A recording of RUNS runs of 2 frames each."""
    buf = bytearray()
    for i in range(runs):
        buf += bytes([0x80 if i % 2 else 0x40, 0, 5 if i % 3 else 250, 0]) * 2
    emu._put(RECORDING_BUFFER, bytes(buf))
    emu.w32(sym("recording") + 4, runs * 2)


def note_pages(p):
    ours = [e for e in pakfile.note_entries(p).values() if e[0] == pakfile.NOTE_CODE]
    return ours, (len(pakfile.chain(p, ours[0][2])) if ours else 0)


def run(emu, sym, args):
    c = sym("cpak")
    full_before = bytes(pakfile.read_pak(mpk(args), 1))
    yield from open_view(emu, sym)
    yield from wait_idle(emu, sym)
    if status(emu, sym) != layout.CPAK_EMPTY or emu.r16(c + layout.CPAK_FREE_PAGES) != 2:
        emu.fail(f"the check: status {status(emu, sym)}, {emu.r16(c + layout.CPAK_FREE_PAGES)} free pages, not 2")
    put_recording(emu, sym, 300)
    emu.w8(c + layout.CPAK_WITH_RECORDING, 1)
    yield from cursor_to(emu, sym, layout.ROW_SAVE)
    yield from press(emu, "A")
    yield from press(emu, "A")
    yield from wait_idle(emu, sym)
    if status(emu, sym) != layout.CPAK_SAVED_NO_REC:
        emu.fail(f"the save with a recording that does not fit gave status {status(emu, sym)}")
    yield 120
    p = pak(emu, args, 0)
    pakfile.check_format(p)
    ours, pages = note_pages(p)
    parts = pakfile.decode_practice(pakfile.note_data(p, ours[0][2]))["parts"]
    if pages != 1 or parts != 3:
        emu.fail(f"the note has {pages} pages and parts {parts}")

    put_recording(emu, sym, 20)
    yield from press(emu, "A")
    yield from press(emu, "A")
    yield from wait_idle(emu, sym)
    if status(emu, sym) != layout.CPAK_SAVED:
        emu.fail(f"the save with a short recording gave status {status(emu, sym)}")
    yield 120
    p = pak(emu, args, 0)
    pakfile.check_format(p)
    ours, pages = note_pages(p)
    note = pakfile.decode_practice(pakfile.note_data(p, ours[0][2]))
    if pages != 2 or note["parts"] != 7 or len(note["recording"]) != 40 or pakfile.free_pages(p) != 0:
        emu.fail(f"the note has {pages} pages, parts {note['parts']}, {len(note['recording'])} frames")

    # Controller 2: no free page.
    yield from cursor_to(emu, sym, layout.ROW_PORT)
    yield from press(emu, "RIGHT")
    yield from cursor_to(emu, sym, layout.ROW_SAVE)
    yield from press(emu, "A")
    yield from press(emu, "A")
    yield from wait_idle(emu, sym)
    if status(emu, sym) != layout.CPAK_NO_ROOM:
        emu.fail(f"a save to a pak with no free page gave status {status(emu, sym)}")
    if emu.r16(c + layout.CPAK_FREE_PAGES) != 0:
        emu.fail("the full pak should show no free page")
    yield 120
    if bytes(pak(emu, args, 1)) != full_before:
        emu.fail("a save that found no room changed the pak")
