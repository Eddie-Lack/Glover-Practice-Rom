"""Controller Pak storage, a pak whose practice note shares its pages with another game's note (the two table entries
point at the same chain), and a pak with two practice notes (cpakdupe.py). A save and a delete refuse ("the notes
overlap or repeat") and write nothing: the file stays as it was. A load still works from the first practice note."""
import layout
import pakfile
from cpakutil import activate, mpk, open_view, settle, status, wait_idle, pak

PAKS = "1"


def entry(code, company, first, name):
    e = bytearray(32)
    e[0:4], e[4:6] = code, company
    e[6:8] = first.to_bytes(2, "big")
    e[8] = 2
    e[0x10:0x20] = pakfile.encode_name(name)
    return e


def prepare(path, args):
    p = pakfile.format_pak()
    slot = pakfile.add_note(p, b"NABE", b"01", "OTHER GAME", bytes(range(256)) * 2)
    first = pakfile.note_entries(p)[slot][2]
    p[3 * 256 + 5 * 32:3 * 256 + 6 * 32] = entry(pakfile.NOTE_CODE, pakfile.NOTE_COMPANY, first, pakfile.NOTE_NAME)
    pakfile.write_pak(path, p)


def refuses(emu, sym, args):
    """Save and delete say INCONSISTENT and leave the file as it was."""
    c = sym("cpak")
    before = open(mpk(args), "rb").read()
    yield from open_view(emu, sym)
    yield from wait_idle(emu, sym)
    yield from activate(emu, sym, layout.ROW_SAVE, True)
    if status(emu, sym) != layout.CPAK_INCONSISTENT:
        emu.fail(f"the save gave status {status(emu, sym)}, not {layout.CPAK_INCONSISTENT}")
    if emu.r8(c + layout.CPAK_HAS_NOTE):
        yield from activate(emu, sym, layout.ROW_DELETE, True)
        if status(emu, sym) != layout.CPAK_INCONSISTENT:
            emu.fail(f"the delete gave status {status(emu, sym)}, not {layout.CPAK_INCONSISTENT}")
    yield from settle(emu)
    if open(mpk(args), "rb").read() != before:
        emu.fail("a refused operation changed the pak file")


def run(emu, sym, args):
    yield from refuses(emu, sym, args)
