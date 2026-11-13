"""Controller Pak storage with a recording longer than the limit (RECORDING_MAX, practice/include/recording.h: 36000
game frames, 30 minutes). The pak holds a good practice note (right sums) whose recording has 36001 game frames.

A load refuses it with its own status (CPAK_TOO_LONG, "The recording is longer than 30 minutes. Nothing was loaded.")
and changes nothing: the settings and the recording stay as they were, and nothing is written to the recording's
buffer (a marker word behind the limit, and the first words). tests/cpakmax.py loads a note with exactly 36000
game frames."""
import struct

import layout
import pakfile
from cpakutil import RECORDING_BUFFER, cursor_to, open_view, press, status, wait_idle

PAKS = "1"
RECORDING_MAX = 36000     # practice/include/recording.h
SETTINGS_SIZE = 0x54      # sizeof(Settings)
BEST_COUNT = 30
FRAMES = RECORDING_MAX + 1


def make_runs(frames):
    """Runs of 5 bytes (cpak.h): a different button on each, up to 255 game frames each."""
    runs = bytearray()
    left, i = frames, 0
    while left:
        n = min(255, left)
        runs += struct.pack(">HbbB", 0x8000 >> (i % 12), (i % 100) - 50, 0, n)
        left -= n
        i += 1
    return bytes(runs), i


def make_note(frames):
    runs, count = make_runs(frames)
    used = 0x20 + SETTINGS_SIZE + BEST_COUNT * 2 + len(runs)
    data = bytearray(used)
    data[0:4] = pakfile.NOTE_CODE
    data[4], data[5] = 1, 7
    struct.pack_into(">H", data, 6, used)
    struct.pack_into(">H", data, 0x0C, SETTINGS_SIZE)
    struct.pack_into(">H", data, 0x10, BEST_COUNT)
    struct.pack_into(">II", data, 0x14, frames, count)
    data[0x20 + SETTINGS_SIZE + BEST_COUNT * 2:] = runs
    s = int.from_bytes(pakfile.NOTE_CODE, "big")
    for b in data:
        s = (((s << 1) | (s >> 31)) & 0xFFFFFFFF) ^ b
    struct.pack_into(">I", data, 8, s)
    return bytes(data)


def prepare_frames(path, frames):
    p = pakfile.format_pak()
    pakfile.add_note(p, pakfile.NOTE_CODE, pakfile.NOTE_COMPANY, pakfile.NOTE_NAME, make_note(frames))
    pakfile.write_pak(path, p)


def prepare(path, args):
    prepare_frames(path, FRAMES)


def run(emu, sym, args):
    c = sym("cpak")
    settings = sym("settings")
    marker = RECORDING_BUFFER + RECORDING_MAX * 4
    emu._put(RECORDING_BUFFER, b"\xA5" * 16)
    emu.w32(marker, 0xDEADBEEF)
    yield from open_view(emu, sym)
    yield from wait_idle(emu, sym)
    if status(emu, sym) != layout.CPAK_FOUND:
        emu.fail(f"the check gave status {status(emu, sym)}, not {layout.CPAK_FOUND}")
    yield from cursor_to(emu, sym, layout.ROW_LOAD)
    yield from press(emu, "A")
    yield from wait_idle(emu, sym)
    if status(emu, sym) != layout.CPAK_TOO_LONG:
        emu.fail(f"a load of a recording of {FRAMES} game frames gave status {status(emu, sym)}, not {layout.CPAK_TOO_LONG}")
    if emu.r32(sym("recording") + 4) != 0:
        emu.fail(f"the recording has {emu.r32(sym('recording') + 4)} game frames after a refused load")
    if emu.r32(marker) != 0xDEADBEEF or bytes(emu.read(RECORDING_BUFFER, 16)) != b"\xA5" * 16:
        emu.fail("a refused load wrote into the recording's buffer")
    if emu.r8(settings + layout.SETTINGS_BALL_NORMAL) or emu.r32(settings) != 0x47505253:
        emu.fail("the settings changed")
    print(f"cpaklong: a recording of {FRAMES} game frames is refused; nothing changed")
