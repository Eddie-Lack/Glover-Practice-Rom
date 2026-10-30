"""Controller Pak storage next to other games' notes. The pak starts with two notes of another game (one of three
pages and one of a single page, on scattered pages) and is saved to, resized and deleted from. Each of them must leave
the other notes alone: their note table entries, their pages and their index entries stay byte for byte as they were.

Order: a save without the recording (one page), a save with a long recording (many pages), a save without it again
(the note shrinks, the pages it gave up are free again), a delete."""
import random

import layout
import pakfile
from cpakutil import RECORDING_BUFFER, cursor_to, foreign_state, mpk, open_view, pak, press, status, wait_idle

PAKS = "12"
FOREIGN = [(b"NABE", b"01", "OTHER GAME ONE", bytes(range(256)) * 2 + b"\x55" * 100),
           (b"NCDJ", b"41", "SAVE 2", b"\xAA" * 200)]
RUN_TRAINERS = 0x48


def prepare(path, args):
    p = pakfile.format_pak()
    for code, company, name, data in FOREIGN:
        pakfile.add_note(p, code, company, name, data)
    pakfile.write_pak(path, p)
    for port in (1, 2, 3):
        pakfile.write_pak(path, pakfile.format_pak(), port)


def run(emu, sym, args):
    c = sym("cpak")
    base = foreign_state(pakfile.read_pak(mpk(args)))
    if len(base) != 2 or len(base[0][1]) != 3:
        emu.fail("the prepared pak does not have the two other notes")

    def verify(what, pages_expected):
        yield 120
        p = pak(emu, args)
        try:
            pakfile.check_format(p)
        except pakfile.PakError as e:
            emu.fail(f"{what}: {e}")
        if foreign_state(p) != base:
            emu.fail(f"{what}: another game's note changed")
        ours = [e for e in pakfile.note_entries(p).values() if e[0] == pakfile.NOTE_CODE]
        if pages_expected == 0:
            if ours:
                emu.fail(f"{what}: the practice note is still there")
            used = 0
        else:
            if len(ours) != 1:
                emu.fail(f"{what}: {len(ours)} practice notes")
            pages = pakfile.chain(p, ours[0][2])
            if len(pages) != pages_expected:
                emu.fail(f"{what}: the note has {len(pages)} pages, not {pages_expected}")
            used = len(pages)
            note = pakfile.decode_practice(pakfile.note_data(p, ours[0][2]))
            return_value[0] = note
        taken = sum(len(v[1]) for v in base.values())
        if pakfile.free_pages(p) != 123 - taken - used:
            emu.fail(f"{what}: {pakfile.free_pages(p)} free pages, not {123 - taken - used}")

    return_value = [None]
    yield from open_view(emu, sym)
    yield from wait_idle(emu, sym)
    if status(emu, sym) != layout.CPAK_EMPTY or emu.r16(c + layout.CPAK_FREE_PAGES) != 123 - 4:
        emu.fail(f"the check: status {status(emu, sym)}, {emu.r16(c + layout.CPAK_FREE_PAGES)} free pages")
    if emu.r8(c + layout.CPAK_FREE_NOTES) != 14:
        emu.fail("the check should show 14 free notes")

    # A long recording: about 2800 frames of changing input.
    rng = random.Random(7)
    rec = []
    while len(rec) < 2800:
        rec += [(rng.choice([0, 0x8000, 0x4000, 0x0020, 0x2000]), rng.randint(-80, 80), rng.randint(-80, 80))] * rng.randint(1, 4)
    buf = bytearray()
    for button, x, y in rec:
        buf += bytes([button >> 8, button & 255, x & 255, y & 255])
    emu._put(RECORDING_BUFFER, bytes(buf))
    emu.w32(sym("recording") + 4, len(rec))

    yield from cursor_to(emu, sym, layout.ROW_SAVE)
    yield from press(emu, "A")
    yield from press(emu, "A")
    yield from wait_idle(emu, sym)
    if status(emu, sym) != layout.CPAK_SAVED:
        emu.fail(f"save 1: status {status(emu, sym)}")
    yield from verify("save without the recording", 1)
    if return_value[0]["parts"] != 3:
        emu.fail("the first save should have no recording")

    yield from cursor_to(emu, sym, layout.ROW_RECORDING)
    yield from press(emu, "A")
    yield from cursor_to(emu, sym, layout.ROW_SAVE)
    yield from press(emu, "A")
    yield from press(emu, "A")
    yield from wait_idle(emu, sym)
    if status(emu, sym) != layout.CPAK_SAVED:
        emu.fail(f"save 2: status {status(emu, sym)}")
    yield 120
    first = [e for e in pakfile.note_entries(pak(emu, args)).values() if e[0] == pakfile.NOTE_CODE][0][2]
    pages = len(pakfile.chain(pak(emu, args), first))
    if pages < 10:
        emu.fail(f"the long recording took {pages} pages")
    yield from verify("save with the recording", pages)
    if return_value[0]["recording"] != rec:
        emu.fail("the long recording differs")

    yield from cursor_to(emu, sym, layout.ROW_RECORDING)
    yield from press(emu, "A")
    yield from cursor_to(emu, sym, layout.ROW_SAVE)
    yield from press(emu, "A")
    yield from press(emu, "A")
    yield from wait_idle(emu, sym)
    yield from verify("save that shrinks", 1)

    yield from cursor_to(emu, sym, layout.ROW_DELETE)
    yield from press(emu, "A")
    if emu.r8(c + layout.CPAK_CONFIRM) != layout.CPAK_REQ_DELETE:
        emu.fail("Delete did not ask for a confirmation")
    yield from press(emu, "A")
    yield from wait_idle(emu, sym)
    if status(emu, sym) != layout.CPAK_DELETED:
        emu.fail(f"delete: status {status(emu, sym)}")
    yield from verify("delete", 0)

    # Controller 2 (the emulator does not write its pak to the file, and the driver reads it back): a save, a change
    # of the settings in RAM, then a load that restores them.
    settings = sym("settings")
    yield from cursor_to(emu, sym, layout.ROW_PORT)
    yield from press(emu, "RIGHT")
    yield from cursor_to(emu, sym, layout.ROW_CHECK)
    yield from press(emu, "A")
    yield from wait_idle(emu, sym)
    if status(emu, sym) != layout.CPAK_EMPTY or emu.r16(c + layout.CPAK_FREE_PAGES) != 123:
        emu.fail(f"the check of controller 2: status {status(emu, sym)}")
    emu.w8(settings + layout.SETTINGS_BALL_NORMAL, 1)
    emu.w8(settings + layout.SETTINGS_DEBUG_PAD_L, 1)
    yield from cursor_to(emu, sym, layout.ROW_SAVE)
    yield from press(emu, "A")
    yield from press(emu, "A")
    yield from wait_idle(emu, sym)
    if status(emu, sym) != layout.CPAK_SAVED:
        emu.fail(f"a save to controller 2 gave status {status(emu, sym)}")
    emu.w8(settings + layout.SETTINGS_BALL_NORMAL, 0)
    emu.w8(settings + layout.SETTINGS_DEBUG_PAD_L, 0)
    yield from cursor_to(emu, sym, layout.ROW_LOAD)
    yield from press(emu, "A")
    yield from wait_idle(emu, sym)
    if status(emu, sym) != layout.CPAK_LOADED or not emu.r8(settings + layout.SETTINGS_BALL_NORMAL) \
            or not emu.r8(settings + layout.SETTINGS_DEBUG_PAD_L):
        emu.fail("the load from controller 2 did not give the saved settings back")
