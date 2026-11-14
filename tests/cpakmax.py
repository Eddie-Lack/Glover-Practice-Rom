"""Controller Pak storage with a recording of exactly the limit (RECORDING_MAX: 36000 game frames, 30 minutes). The load
works, the recording holds every game frame, and the marker word behind the last one is not touched. See
tests/cpaklong.py for one game frame more."""
import layout
from cpaklong import RECORDING_MAX, make_runs, prepare_frames
from cpakutil import RECORDING_BUFFER, cursor_to, open_view, press, status, wait_idle

PAKS = "1"


def prepare(path, args):
    prepare_frames(path, RECORDING_MAX)


def run(emu, sym, args):
    marker = RECORDING_BUFFER + RECORDING_MAX * 4
    emu.w32(marker, 0xDEADBEEF)
    yield from open_view(emu, sym)
    yield from wait_idle(emu, sym)
    if status(emu, sym) != layout.CPAK_FOUND:
        emu.fail(f"the check gave status {status(emu, sym)}, not {layout.CPAK_FOUND}")
    yield from cursor_to(emu, sym, layout.ROW_LOAD)
    yield from press(emu, "A")
    yield from wait_idle(emu, sym)
    if status(emu, sym) != layout.CPAK_LOADED:
        emu.fail(f"a load of a recording of {RECORDING_MAX} game frames gave status {status(emu, sym)}")
    if emu.r32(sym("recording") + 4) != RECORDING_MAX:
        emu.fail(f"the recording has {emu.r32(sym('recording') + 4)} game frames, not {RECORDING_MAX}")
    runs, _ = make_runs(RECORDING_MAX)
    want = bytearray()
    for i in range(0, len(runs), 5):
        want += (runs[i:i + 4]) * runs[i + 4]
    if bytes(emu.read(RECORDING_BUFFER, RECORDING_MAX * 4)) != bytes(want):
        emu.fail("the loaded recording differs")
    if emu.r32(marker) != 0xDEADBEEF:
        emu.fail("the load wrote past the last game frame")
    print(f"cpakmax: {RECORDING_MAX} game frames loaded")
