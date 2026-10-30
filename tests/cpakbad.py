"""Controller Pak storage with a damaged practice note. The note has the right name and magic but a wrong sum. The
check finds it; a load refuses it ("damaged") and leaves the settings alone."""
import layout
import pakfile
from cpakutil import cursor_to, open_view, press, status, wait_idle

PAKS = "1"


def prepare(path, args):
    p = pakfile.format_pak()
    data = bytearray(pakfile.NOTE_CODE + bytes([1, 3]) + (0x120).to_bytes(2, "big") + bytes(0x120 - 8))
    data[8:12] = b"\x12\x34\x56\x78"
    pakfile.add_note(p, pakfile.NOTE_CODE, pakfile.NOTE_COMPANY, pakfile.NOTE_NAME, bytes(data))
    pakfile.write_pak(path, p)


def run(emu, sym, args):
    c = sym("cpak")
    settings = sym("settings")
    yield from open_view(emu, sym)
    yield from wait_idle(emu, sym)
    if status(emu, sym) != layout.CPAK_FOUND:
        emu.fail(f"the check gave status {status(emu, sym)}, not {layout.CPAK_FOUND}")
    yield from cursor_to(emu, sym, layout.ROW_LOAD)
    yield from press(emu, "A")
    yield from wait_idle(emu, sym)
    if status(emu, sym) != layout.CPAK_BAD_NOTE:
        emu.fail(f"a load of a damaged note gave status {status(emu, sym)}")
    if emu.r8(settings + layout.SETTINGS_BALL_NORMAL) or emu.r32(settings) != 0x47505253:
        emu.fail("the settings changed")
