"""Controller Pak storage, a save that fails part of the way (the pak is "pulled": CPak.failAt makes every block from
a chosen count on fail). The pak has a note of another game and the practice note. A save with new settings is cut
short after K blocks, for K at the start, in the data, in each index table write, at the note table entry and after.
After each cut the file must be a valid pak: one valid ID block, a valid index table (the backup stands in for a
torn first one), chains that share no page, the other note as it was, and the practice note present and intact,
either as it was or as the save made it. A save that stops before the entry is written leaves the old note.
At the end a load and a normal save work. Some pages may stay marked as used after a cut; that is allowed."""
import layout
import pakfile
from cpakutil import activate, cursor_to, foreign_state, loose_check, mpk, open_view, pak, press, settle, status, wait_idle

PAKS = "1"
SETTINGS_HUD_X = layout.SETTINGS_HUD + 1
# Blocks of a save over an existing note of one page: 13 to identify and read the ID area, 8 for the index table,
# 16 for the note table (37), 8 for the data (45), 16 for the links (61), 1 for the entry (62), then 16 to free
# the old page (78) and 18 to verify (96).
CUTS = [0, 5, 13, 21, 37, 40, 48, 52, 56, 60, 61, 62, 65, 75, 80, 90]
ENTRY_AT = 61  # the block that writes the note table entry


def prepare(path, args):
    p = pakfile.format_pak()
    pakfile.add_note(p, b"NABE", b"01", "OTHER GAME", bytes(range(256)) * 2 + b"\x55" * 100)
    pakfile.write_pak(path, p)


def run(emu, sym, args):
    c = sym("cpak")
    settings = sym("settings")
    base = foreign_state(pakfile.read_pak(mpk(args)))
    yield from open_view(emu, sym)
    yield from wait_idle(emu, sym)

    emu.w8(settings + SETTINGS_HUD_X, 9)
    yield from activate(emu, sym, layout.ROW_SAVE, True)
    if status(emu, sym) != layout.CPAK_SAVED:
        emu.fail(f"the first save gave status {status(emu, sym)}")
    prev = bytes(emu.read(settings, 0x54))
    outcomes = []
    for i, k in enumerate(CUTS):
        emu.w8(settings + SETTINGS_HUD_X, 10 + i)
        new = bytes(emu.read(settings, 0x54))
        emu.w32(c + layout.CPAK_FAIL_AT, emu.r32(c + layout.CPAK_TRANSFERS) + k)
        yield from activate(emu, sym, layout.ROW_SAVE, True)
        emu.w32(c + layout.CPAK_FAIL_AT, 0)
        if status(emu, sym) != layout.CPAK_NO_PAK:
            emu.fail(f"the save cut after {k} blocks gave status {status(emu, sym)}, not {layout.CPAK_NO_PAK}")
        yield from settle(emu)
        try:
            p, ours = loose_check(pak(emu, args))
        except pakfile.PakError as e:
            emu.fail(f"after the cut at {k}: {e}")
        if foreign_state(p) != base:
            emu.fail(f"after the cut at {k}: the other note changed")
        if ours is None:
            emu.fail(f"after the cut at {k}: the practice note is gone")
        try:
            got = pakfile.decode_practice(pakfile.note_data(p, ours[2]))["settings"]
        except pakfile.PakError as e:
            emu.fail(f"after the cut at {k}: the practice note is damaged ({e})")
        outcomes.append((k, "new" if got == new else "old" if got == prev else "other"))
        if got not in (prev, new):
            emu.fail(f"after the cut at {k}: the practice note holds neither the old nor the new settings")
        if k <= ENTRY_AT and got != prev:
            emu.fail(f"after the cut at {k}, before the entry: the practice note is not the old one")
        if k > ENTRY_AT and got != new:
            emu.fail(f"after the cut at {k}, after the entry: the practice note is not the new one")
        prev = got
    print("cpakfail:", outcomes)

    # The pak still works: a load gives the last note back, and a save heals the index tables.
    emu.w8(settings + SETTINGS_HUD_X, 1)
    yield from activate(emu, sym, layout.ROW_LOAD)
    if status(emu, sym) != layout.CPAK_LOADED or bytes(emu.read(settings, 0x54)) != prev:
        emu.fail("the load after the cuts did not give the last note back")
    yield from activate(emu, sym, layout.ROW_SAVE, True)
    if status(emu, sym) != layout.CPAK_SAVED:
        emu.fail(f"the save after the cuts gave status {status(emu, sym)}")
    yield from settle(emu)
    p = pak(emu, args)
    try:
        pakfile.check_format(p)
    except pakfile.PakError as e:
        emu.fail(f"after the last save: {e}")
    if foreign_state(p) != base:
        emu.fail("the other note changed")
