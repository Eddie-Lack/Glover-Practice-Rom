"""Shared steps of the Controller Pak tests: the menu to the Controller Pak view, the pak files."""
import glob
import os

import layout
import pakfile
import trace
from menunav import goto

C_ALL = ("CUP", "CDOWN", "CLEFT", "CRIGHT")
RECORDING_BUFFER = 0x80780000


def press(emu, *buttons):
    emu.hold(*buttons)
    yield 8
    emu.release()
    yield 8


def mpk(args):
    files = glob.glob(os.path.join(args.out, "save", "*.mpk"))
    return files[0] if files else None


def pak(emu, args, port=0):
    """The pak of controller PORT as the emulator keeps it on disk now."""
    path = mpk(args)
    if path is None:
        emu.fail("the emulator wrote no Controller Pak file")
    return pakfile.read_pak(path, port)


def open_view(emu, sym):
    """From the boot to the Controller Pak view of the Settings menu (Settings, Controller Pak)."""
    gi = sym("gameInfo")
    while not (emu.r8(gi + 1) == trace.PRESENT and emu.r32(sym("frameCount")) >= 200):
        yield 1
    yield from press(emu, "START")
    yield 600
    yield from press(emu, *C_ALL)
    if not emu.r8(sym("menuOpen")):
        emu.fail("the menu did not open")
    yield from goto(emu, sym, "Settings", "Controller Pak")
    if not emu.r32(sym("menuView")):
        emu.fail("the Controller Pak view did not open")


def wait_idle(emu, sym, limit=3000):
    c = sym("cpak")
    for _ in range(limit):
        if not emu.r8(c + layout.CPAK_WORKING) and not emu.r8(c + layout.CPAK_REQUEST):
            return
        yield 1
    emu.fail("the Controller Pak operation did not end")


def cursor_to(emu, sym, row):
    """Moves the view's cursor to ROW with up and down."""
    c = sym("cpak")
    for _ in range(layout.ROW_FORMAT + 2):
        if emu.r8(c + layout.CPAK_CURSOR) == row:
            return
        yield from press(emu, "DOWN")
    emu.fail(f"the cursor did not reach row {row}")


def status(emu, sym):
    return emu.r8(sym("cpak") + layout.CPAK_STATUS)


def activate(emu, sym, row, confirm=False):
    """Presses A on ROW (and A again to confirm), then waits for the operation to end."""
    yield from cursor_to(emu, sym, row)
    yield from press(emu, "A")
    if confirm:
        yield from press(emu, "A")
    yield from wait_idle(emu, sym)


def settle(emu, frames=120):
    """Time for the emulator to write its pak to the file."""
    yield frames


def foreign_state(p):
    """The parts of the pak that belong to notes that are not the practice note: their table entries, pages and index
    entries."""
    state = {}
    for slot, (code, company, first, st, name) in pakfile.note_entries(p).items():
        if code == pakfile.NOTE_CODE:
            continue
        pages = pakfile.chain(p, first)
        state[slot] = (bytes(p[3 * 256 + slot * 32:3 * 256 + slot * 32 + 32]), pages,
                       [bytes(p[q * 256:(q + 1) * 256]) for q in pages], [pakfile.index_entry(p, q) for q in pages])
    return state


def effective(p):
    """The pak as the driver sees it: the backup index table stands in for a first one that is not valid. Raises
    PakError when there is no valid ID block or no valid index table."""
    p = bytearray(p)
    if not any(pakfile.id_valid(bytes(p[off:off + 32])) for off in (0x20, 0x60, 0x80, 0xC0)):
        raise pakfile.PakError("no valid ID block")
    if p[pakfile.PAGE + 1] != pakfile.index_sum(p):
        if p[2 * pakfile.PAGE + 1] != pakfile.index_sum(p, 2 * pakfile.PAGE):
            raise pakfile.PakError("no valid index table")
        p[pakfile.PAGE:2 * pakfile.PAGE] = p[2 * pakfile.PAGE:3 * pakfile.PAGE]
    return p


def loose_check(p):
    """What must hold after any step of a save, also a step that did not finish: one valid ID block, one valid
    index table, and note chains that are valid and share no page. Returns the effective pak and the practice
    note's entry (or None)."""
    p = effective(p)
    seen = {}
    ours = None
    for slot, entry in pakfile.note_entries(p).items():
        for q in pakfile.chain(p, entry[2]):
            if q in seen:
                raise pakfile.PakError(f"page {q} belongs to the notes {seen[q]} and {slot}")
            seen[q] = slot
        if entry[0] == pakfile.NOTE_CODE:
            ours = entry
    return p, ours
