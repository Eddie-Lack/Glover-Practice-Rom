"""Controller Pak storage, a save over the note's own pages that fails part of the way. The pak has only 5 free
pages after another game's note and the practice note of 3 pages, so a save that needs 5 pages has too few free
ones and writes over the old pages: it clears the note table entry first and frees the old pages. The save is cut
(CPak.failAt) in the second index table write that frees them. The status says that the note may be damaged. The
file must be a valid pak (the backup index table stands in for the torn one), the other note unchanged and the
practice note absent. The check offers a repair, which makes the pak pass the strict checks."""
import layout
import pakfile
from cpakutil import RECORDING_BUFFER, activate, effective, foreign_state, loose_check, mpk, open_view, pak, settle, \
    status, wait_idle

PAKS = "1"
CUT = 45  # the last block of the first index table write that frees the old pages


def prepare(path, args):
    p = pakfile.format_pak()
    pakfile.add_note(p, b"NBIG", b"01", "BIG NOTE", b"\x33" * (118 * 256))
    pakfile.write_pak(path, p)


def put_recording(emu, sym, runs):
    buf = bytearray()
    for i in range(runs):
        buf += bytes([0x80 if i % 2 else 0x40, 0, 5 if i % 3 else 250, 0]) * 2
    emu._put(RECORDING_BUFFER, bytes(buf))
    emu.w32(sym("recording") + 4, runs * 2)


def run(emu, sym, args):
    c = sym("cpak")
    base = foreign_state(pakfile.read_pak(mpk(args)))
    yield from open_view(emu, sym)
    yield from wait_idle(emu, sym)
    emu.w8(c + layout.CPAK_WITH_RECORDING, 1)
    put_recording(emu, sym, 100)
    yield from activate(emu, sym, layout.ROW_SAVE, True)
    if status(emu, sym) != layout.CPAK_SAVED or emu.r16(c + layout.CPAK_NOTE_BYTES) != 3 * 256:
        emu.fail(f"the first save: status {status(emu, sym)}, {emu.r16(c + layout.CPAK_NOTE_BYTES)} bytes")
    put_recording(emu, sym, 210)
    emu.w32(c + layout.CPAK_FAIL_AT, emu.r32(c + layout.CPAK_TRANSFERS) + CUT)
    yield from activate(emu, sym, layout.ROW_SAVE, True)
    emu.w32(c + layout.CPAK_FAIL_AT, 0)
    if status(emu, sym) != layout.CPAK_ERROR_NOTE:
        emu.fail(f"the cut save gave status {status(emu, sym)}, not {layout.CPAK_ERROR_NOTE}")
    yield from settle(emu)
    try:
        p, ours = loose_check(pak(emu, args))
    except pakfile.PakError as e:
        emu.fail(f"after the cut: {e}")
    if foreign_state(p) != base:
        emu.fail("the other note changed")
    if ours is not None:
        emu.fail("the practice note should be absent after the cut")
    yield from activate(emu, sym, layout.ROW_CHECK)
    if status(emu, sym) != layout.CPAK_EMPTY or not emu.r8(c + layout.CPAK_CAN_REPAIR):
        emu.fail(f"the check after the cut: status {status(emu, sym)}, repair {emu.r8(c + layout.CPAK_CAN_REPAIR)}")
    yield from activate(emu, sym, layout.ROW_REPAIR, True)
    if status(emu, sym) != layout.CPAK_REPAIRED:
        emu.fail(f"the repair gave status {status(emu, sym)}")
    yield from settle(emu)
    try:
        pakfile.check_format(pak(emu, args))
    except pakfile.PakError as e:
        emu.fail(f"after the repair: {e}")
