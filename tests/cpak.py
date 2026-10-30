"""Controller Pak storage, a save to a fresh pak (practice/include/cpak.h). Run before cpakload.py, which needs this
test's save folder.

The emulator keeps a Controller Pak in controller 1 and formats it. In the Settings menu, Controller Pak: the check
finds no practice note; a save asks for a confirmation (another button declines and writes nothing); A writes it.
Then the emulator's .mpk file is decoded here from the public layout: a valid ID area, two equal index tables, one
note "GLOVER PRACTICE" with a chain of pages, and in its data the settings, the Hi Scores choice, the trainer
switches, the best times and the recording that the test put in RAM.

Pictures: cpak_view.png, cpak_confirm.png and cpak_saved.png (needs --gfx angrylion)."""
import layout
import pakfile
from cpakutil import RECORDING_BUFFER, cursor_to, mpk, open_view, pak, press, settle, status, wait_idle

PAKS = "1"
SETTINGS_HUD_X = layout.SETTINGS_HUD + 1
RUN_TRAINERS = 0x48


def make_recording():
    """About 700 game frames of input with runs of different lengths, one longer than 255 frames."""
    frames = []
    for i in range(40):
        frames += [(0x8000 if i % 3 == 0 else 0x0020, (i * 7) % 100 - 50, 0 if i % 5 else -80)] * (3 + i * 2 % 11)
    frames += [(0x4000, 0, 0)] * 300
    return frames


def run(emu, sym, args):
    c = sym("cpak")
    def shot(name):
        if args.gfx == "angrylion":
            emu.fb_png(name, sym("cfb1"))
            emu.fb_png(name + "_0", sym("cfb0"))
    yield from open_view(emu, sym)
    yield from wait_idle(emu, sym)
    if status(emu, sym) != layout.CPAK_EMPTY:
        emu.fail(f"the check of a fresh pak gave status {status(emu, sym)}, not {layout.CPAK_EMPTY} (no note)")
    if emu.r16(c + layout.CPAK_FREE_PAGES) != 123 or emu.r8(c + layout.CPAK_FREE_NOTES) != 16:
        emu.fail("a fresh pak should show 123 free pages and 16 free notes")
    shot("cpak_view")
    yield from settle(emu)
    if mpk(args) is None:
        emu.fail("the emulator wrote no Controller Pak file")
    before = bytes(pak(emu, args))

    # The state to save.
    settings = sym("settings")
    emu.w8(settings + layout.SETTINGS_BALL_NORMAL, 1)
    emu.w8(settings + layout.SETTINGS_DEBUG_PAD_L, 1)
    emu.w8(settings + SETTINGS_HUD_X, 77)
    emu.w8(settings + layout.SETTINGS_TRAINER_LOG, 4)
    emu.w8(settings + layout.SETTINGS_TRAINER_HOLD, 4)
    emu.w16(settings + layout.SETTINGS_BIND_BUTTONS + 6, 0x1234)
    bests = [0] * 30
    bests[0], bests[7], bests[29] = 1234, 4321, 65535
    for i, v in enumerate(bests):
        emu.w16(sym("practiceBests") + 2 * i, v)
    emu.w8(sym("bestTimesState"), 1)
    emu.w8(sym("run") + RUN_TRAINERS, 0x05)
    rec = make_recording()
    buf = bytearray()
    for button, x, y in rec:
        buf += bytes([button >> 8, button & 255, x & 255, y & 255])
    emu._put(RECORDING_BUFFER, bytes(buf))
    emu.w32(sym("recording") + 4, len(rec))
    emu.w8(c + layout.CPAK_WITH_RECORDING, 1)
    ram_settings = bytes(emu.read(settings, 0x54))

    # A save asks first. B declines: the pak is not touched.
    transfers = emu.r32(c + layout.CPAK_TRANSFERS)
    yield from cursor_to(emu, sym, layout.ROW_SAVE)
    yield from press(emu, "A")
    if emu.r8(c + layout.CPAK_CONFIRM) != layout.CPAK_REQ_SAVE:
        emu.fail("A on Save to the pak did not ask for a confirmation")
    shot("cpak_confirm")
    yield from press(emu, "B")
    yield 20
    if emu.r8(c + layout.CPAK_CONFIRM) or emu.r32(c + layout.CPAK_TRANSFERS) != transfers or emu.r8(c + layout.CPAK_WORKING):
        emu.fail("declining the save still did something")
    yield from press(emu, "A")
    yield from press(emu, "A")
    yield from wait_idle(emu, sym)
    if status(emu, sym) != layout.CPAK_SAVED:
        emu.fail(f"the save gave status {status(emu, sym)}, not {layout.CPAK_SAVED}")
    yield 10
    shot("cpak_saved")
    yield 120

    p = pak(emu, args)
    try:
        pakfile.check_format(p)
    except pakfile.PakError as e:
        emu.fail(str(e))
    notes = pakfile.note_entries(p)
    if len(notes) != 1:
        emu.fail(f"the pak has {len(notes)} notes, not 1")
    code, company, first, st, name = next(iter(notes.values()))
    if code != pakfile.NOTE_CODE or company != pakfile.NOTE_COMPANY or pakfile.decode_name(name) != pakfile.NOTE_NAME:
        emu.fail(f"the note is {code} {company} {pakfile.decode_name(name)!r}")
    pages = pakfile.chain(p, first)
    if len(pages) < 2:
        emu.fail("the note with a recording should need more than one page")
    if pakfile.free_pages(p) != 123 - len(pages):
        emu.fail("the free pages do not match the note's pages")
    try:
        note = pakfile.decode_practice(pakfile.note_data(p, first))
    except pakfile.PakError as e:
        emu.fail(str(e))
    if note["settings"] != ram_settings:
        emu.fail("the saved settings differ from the settings in RAM")
    if note["bests"] != bests:
        emu.fail(f"the saved best times are {note['bests']}")
    if note["showGame"] != 1 or note["trainers"] != 5 or note["parts"] != 7:
        emu.fail(f"showGame {note['showGame']}, trainers {note['trainers']}, parts {note['parts']}")
    if note["recording"] != rec:
        emu.fail("the saved recording differs")
    # The rest of the pak is untouched: all bytes outside the index tables, the note's entry and its pages are as
    # the emulator formatted them (the ID area, the other entries, the free pages).
    slot = next(iter(notes))
    for q in range(pakfile.PAGES):
        if q not in (1, 2, 3, 4, *pages) and bytes(p[q * 256:(q + 1) * 256]) != before[q * 256:(q + 1) * 256]:
            emu.fail(f"the save changed page {q}")
    for s in range(16):
        at = 3 * 256 + s * 32
        if s != slot and bytes(p[at:at + 32]) != before[at:at + 32]:
            emu.fail(f"the save changed the note table entry {s}")
    for q in pages:
        if pakfile.index_entry(before, q) != pakfile.FREE:
            emu.fail("the pages of the note were not free before the save")
    print(f"cpak: note of {len(pages)} pages, {note['used']} bytes; recording {len(rec)} frames")
