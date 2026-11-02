"""Savestates on the SD card (practice/src/card.c, sd.c, fat.c), with the test mailbox in place of a cart.

The mailbox is a block of RAM through which the ROM asks for sectors; tests/sdutil.py answers it each video frame from
a disk image (a 64 MB FAT32 card with a partition table). Everything but the SummerCart64 driver itself runs.

1. With no cart (the emulator has none), Check card says so and the ROM touches no mailbox. With the mailbox: a
   card that is not there, a card with no file system, and a formatted card with no folder. None of these write.
2. In Atlantis 1, three states in the pool as tests/slots.py makes them, one with words changed that keep the
   chunk's sums; one more in another level. The first save to the card goes through the Savestate menu.
3. Card files 1, 42, 100 and 7: save, overwrite (42), delete (7, through the menu), Check card through the menu.
4. The image is read by tests/fatimage.py's reader: the folder, the names, the sizes, the header, the CRC, every
   record decoded by tests/sdutil.py's decoder (it shares no code with the ROM), fsck.fat if installed. No sector
   was written before the first Save.
5. With the game stopped (frame advance), a state is saved to the card and decoded again by the reader: it must equal
   the game's memory but for the words that the other threads change.
tests/sdcardload.py then loads the files in another emulator run; it reads build/tests/sdcard/traces.json.
The sizes and the times are printed."""
# The ROM with the test mailbox for a cart (make testrom).
TEST_BUILD = True

import json
import os

import fatimage
import layout
import trace
from menunav import goto as menu_goto
from sdutil import (CARD_NO_CART, CARD_NO_CARD, CARD_NO_FOLDER, CARD_NO_FS, CARD_READY, REQ_CHECK, REQ_LOAD, REQ_SAVE,
                    Mailbox, card_files, chunks, parse_card_file, sha)
from stateutil import Verify, hud_message
from tools import C_ALL, press, wait_for

ACTION_WARP = 1
ACTION_SAVE_STATE = 8
RUN_WARP_LEVEL = 4
RUN_FRAME_ADVANCE = 0
RUN_STEPS = 1
TRACE = 40
STICK = dict(x=0, y=80)
COLLIDE = 0x800FF000
SECOND_LEVEL = 20
# The savestate menu's items (practice/src/menus.c), by index.
ITEM_CHECK, ITEM_SAVE, ITEM_LOAD, ITEM_DELETE = "Check card", "Save slot to card", "Load from card", "Delete on card"


def run(emu, sym, args):
    out = args.out
    image = os.path.join(out, "sd.img")
    blank = os.path.join(out, "blank.img")
    fatimage.format_image(image, 64, 32, 1, True)
    with open(blank, "wb") as f:
        f.truncate(64 << 20)
    box = Mailbox(sym, blank)
    box.start(emu)
    try:
        yield from scenario(emu, sym, args, box, image)
    finally:
        box.stop()


def scenario(emu, sym, args, box, image):
    out = args.out
    gi, fc_addr = sym("gameInfo"), sym("frameCount")
    hand, ball, player = sym("hand"), sym("ball"), sym("player")
    requests = sym("requests")
    verify = Verify(emu, sym)
    mode = lambda: emu.r8(gi)  # noqa: E731
    level = lambda: emu.r8(gi + 1)  # noqa: E731
    fc = lambda: emu.r32(fc_addr)  # noqa: E731
    status = lambda: emu.r8(sym("cardStatus"))  # noqa: E731
    done = lambda: emu.r32(sym("cardDone"))  # noqa: E731

    def state():
        s = [emu.r32(sym("rseed")), emu.r32(player + 0x18)]
        for actor in (hand, ball):
            s += [emu.r32(actor + o) for o in (0x34, 0x38, 0x3C, 0x58, 0x5C, 0x60)]
        return tuple(s)

    def collect(n):
        seen = {}
        start = fc()
        for _ in range(n * 12):
            f = fc()
            if f >= start + n:
                return seen
            if mode() != 4:
                emu.fail(f"the game left the level (mode {mode()})")
            seen.setdefault(f, set()).add(state())
            yield 1
        emu.fail("the level did not run")

    def ram_save(i):
        before = emu.r32(sym("stateSaves"))
        emu.w8(sym("stateSlot"), i)
        emu.w16(requests, emu.r16(requests) | 1 << ACTION_SAVE_STATE)
        yield from wait_for(emu, lambda: emu.r32(sym("stateSaves")) > before, 1500, f"the save into slot {i + 1}")
        yield 3
        verify.check(f"slot {i + 1}")

    def goto(lv):
        emu.w8(sym("run") + RUN_WARP_LEVEL, lv)
        emu.w16(requests, emu.r16(requests) | 1 << ACTION_WARP)
        yield from wait_for(emu, lambda: level() == lv and mode() == 4 and 40 <= fc() < 400, 4000, f"level {lv}")

    def card_op(request, number=None, ram_slot=None):
        """A request to the card code, as the menu makes it, and the wait for it."""
        before = done()
        if number is not None:
            emu.w8(sym("cardSlot"), number)
        if ram_slot is not None:
            emu.w8(sym("stateSlot"), ram_slot)
        emu.w8(sym("cardWanted"), request)
        yield from wait_for(emu, lambda: done() > before, 6000, f"card request {request}")
        yield 3
        box.check(emu)

    def menu_item(item):
        """Opens the menu, goes to the item of the Savestate menu's SD card page and presses A."""
        emu.release()
        yield from press(emu, *C_ALL)
        if not emu.r8(sym("menuOpen")):
            emu.fail("the menu did not open")
        yield from menu_goto(emu, sym, "Savestate", "SD card", item)

    def close_menu():
        if emu.r8(sym("menuOpen")):
            yield from press(emu, *C_ALL)
        if emu.r8(sym("menuOpen")):
            emu.fail("the menu did not close")

    for i in layout.SETTINGS_BINDINGS:
        emu.w8(sym("settings") + i, 0)
    emu.w8(sym("stateVerify"), 1)
    yield from wait_for(emu, lambda: level() == trace.PRESENT and fc() >= 200, 4000, "the logos")
    yield from press(emu, "START", frames=6)
    yield from wait_for(emu, lambda: level() == trace.FLYTHRU and mode() == 4 and fc() >= 30, 3000, "the title")

    # 1. The card's states of being. Nothing here writes.
    if status() != 0:
        emu.fail("the card status is not 'unchecked' at boot")
    yield from card_op(REQ_CHECK)
    if status() != CARD_NO_CART or emu.r32(sym("sdMailbox") + 16) != 0:
        emu.fail(f"with no cart: status {status()}, {emu.r32(sym('sdMailbox') + 16)} mailbox requests")
    print("sdcard: no cart: Check card says so, and the mailbox was not used")
    emu.w8(sym("sdBackend"), 1)
    box.present = False
    yield from card_op(REQ_CHECK)
    if status() != CARD_NO_CARD:
        emu.fail(f"with no card: status {status()}")
    box.present = True
    yield from card_op(REQ_CHECK)
    if status() != CARD_NO_FS:
        emu.fail(f"with a card of zeros: status {status()}")
    box.switch(image)
    yield from card_op(REQ_CHECK)
    if status() != CARD_NO_FOLDER or box.writes != 0 or sha(image) != sha_initial(image, out):
        emu.fail(f"with an empty card: status {status()}, {box.writes} sectors written")
    print("sdcard: no card, no file system and no folder are told apart; nothing was written")

    # 2. States in Atlantis 1 and in another level.
    yield from card_op(REQ_LOAD, 1)   # a load on the title is refused, and writes nothing
    if hud_message(emu, sym) != "no states on a title" or box.writes != 0:
        emu.fail(f"a load of a missing file on the title: \"{hud_message(emu, sym)}\"")
    yield from press(emu, *C_ALL)
    for b in ("A", "DOWN", "DOWN", "A"):  # Warp, Go
        yield from press(emu, b)
    yield from wait_for(emu, lambda: level() == trace.ATLANTIS1 and mode() == 4 and 0 < fc() < 20, 3000, "Atlantis 1")
    emu.hold(**STICK)
    yield from wait_for(emu, lambda: fc() >= 80, 1000, "game frame 80")
    traces = {}
    original = [emu.r32(COLLIDE + 4 * k) for k in range(3)]
    changed = [(original[0] + 1) & 0xFFFFFFFF, (original[1] - 2) & 0xFFFFFFFF, (original[2] + 1) & 0xFFFFFFFF]
    for i in (0, 1, 2):
        if i == 2:
            for k in range(3):
                emu.w32(COLLIDE + 4 * k, changed[k])
        yield from ram_save(i)
        for k in range(3):
            emu.w32(COLLIDE + 4 * k, original[k])
        traces[i] = (level(), (yield from collect(TRACE)))
        yield 20
    yield from goto(SECOND_LEVEL)
    yield from ram_save(3)
    traces[3] = (level(), (yield from collect(TRACE)))

    # 3. Card files. The first save goes through the menu.
    initial = sha_initial(image, out)
    if sha(image) != initial or box.writes != 0:
        emu.fail("the card changed before the first Save")
    emu.release()
    emu.w8(sym("stateSlot"), 0)
    emu.w8(sym("cardSlot"), 1)
    before = done()
    # The progress bar (practice/src/card.c, DrawProgress): while the game is stopped, one of the two frame buffers
    # has its pixels, and the percent that the ROM keeps grows. The server's thread looks at them.
    bars = []

    def look(e, sectors):
        for name in ("cfb_16_a", "cfb_16_b"):
            row = e.read(sym(name) + ((214 + 3) * 320 + 40) * 2, 240 * 2)
            filled = sum(1 for i in range(0, 480, 2) if row[i] << 8 | row[i + 1] == 0xFFC1)
            empty = sum(1 for i in range(0, 480, 2) if row[i] << 8 | row[i + 1] == 0x39CF)
            if filled + empty >= 230:
                bars.append((sectors, filled, e.r8(sym("cardProgress"))))

    box.snap = look
    yield from menu_item(ITEM_SAVE)
    shown = emu.r32(sym("actionsAnnounced"))
    yield from wait_for(emu, lambda: done() > before, 6000, "the save to card 1 through the menu")
    yield 3
    box.check(emu)
    box.snap = None
    if len(bars) < 10 or [b[1] for b in bars] != sorted(b[1] for b in bars) or bars[-1][1] < 200:
        emu.fail(f"the progress bar: {len(bars)} looks, {bars[:3]} ... {bars[-3:]}")
    print(f"sdcard: the progress bar was seen {len(bars)} times, from {bars[0][1]} to {bars[-1][1]} pixels of 240")
    print(f"sdcard: card file 1 through the menu: {emu.r32(sym('cardLastBytes'))} bytes, "
          f"{emu.r32(sym('cardLastTicks')) / 46875:.0f} ms, of which {emu.r32(sym('cardLastWait')) / 46875:.0f} ms waiting "
          f"for the cart; \"{hud_message(emu, sym)}\"")
    if not hud_message(emu, sym).startswith("saved to card 1: "):
        emu.fail(f"the message after the save: \"{hud_message(emu, sym)}\"")
    if emu.r8(sym("menuOpen")):
        emu.fail("the menu stayed open after Save slot to card")
    emu.hold(**STICK)
    yield from card_op(REQ_SAVE, 7, 0)
    yield from card_op(REQ_SAVE, 42, 1)
    first42 = emu.r32(sym("cardLastBytes"))
    yield from card_op(REQ_SAVE, 42, 2)     # replaces file 42
    yield from card_op(REQ_SAVE, 100, 3)
    sizes = {n: emu.r8(sym("cardHave") + n - 1) for n in (1, 7, 42, 100)}
    print(f"sdcard: file 42 first {first42} bytes, then {emu.r32(sym('cardLastBytes'))}; cardHave {sizes}; "
          f"{box.writes} sectors written, {box.reads} read")
    # Delete file 7 through the menu, and look at the card through it.
    emu.w8(sym("cardSlot"), 7)
    before = done()
    yield from menu_item(ITEM_DELETE)
    yield from wait_for(emu, lambda: done() > before, 3000, "the delete through the menu")
    if hud_message(emu, sym) != "card file 7 deleted":
        emu.fail(f"the message after the delete: \"{hud_message(emu, sym)}\"")
    before = done()
    emu.w8(sym("cardSlot"), 1)
    yield from menu_goto(emu, sym, ITEM_CHECK)
    yield from wait_for(emu, lambda: done() > before, 3000, "Check card through the menu")
    yield 5
    box.check(emu)
    have = [emu.r8(sym("cardHave") + i) for i in range(100)]
    if status() != CARD_READY or [i + 1 for i, v in enumerate(have) if v] != [1, 42, 100]:
        emu.fail(f"after Check card: status {status()}, files {[i + 1 for i, v in enumerate(have) if v]}")
    if have[0] != trace.ATLANTIS1 + 1 or have[99] != SECOND_LEVEL + 1:
        emu.fail(f"the levels in the list: {have[0]}, {have[99]}")
    # A shot of the menu with the list.
    emu.fb_png("sdcard_menu", sym("cfb_16_b"))
    yield from close_menu()
    emu.hold(**STICK)

    # 4. The image, read by the independent reader.
    img, folder, files = card_files(image)
    try:
        img.check()
        if sorted(files) != [1, 42, 100]:
            emu.fail(f"the folder holds {sorted(files)}")
        for number, level_want in ((1, trace.ATLANTIS1), (42, trace.ATLANTIS1), (100, SECOND_LEVEL)):
            first, size = files[number]
            data = img.read(first, size)
            header, memory = parse_card_file(data)
            if header["level"] != level_want:
                emu.fail(f"card file {number} says level {header['level']}")
            print(f"sdcard: file {number}: {size} bytes ({sum(n for _, n in chunks()) / size:.2f} times smaller), "
                  f"level {header['level']}, {header['chunks']} chunks, decoded by the reader")
            if number in (1, 42):
                # File 1 holds slot 1's state with the original words, file 42 slot 3's with the changed ones.
                start = max(a for a in memory if a <= COLLIDE)
                got = [int.from_bytes(memory[start][COLLIDE - start + 4 * k:][:4], "big") for k in range(3)]
                want = original if number == 1 else changed
                if got != want:
                    emu.fail(f"card file {number} holds the words {got} at {COLLIDE:#x}, not {want}")
    finally:
        img.close()
    result = fatimage.fsck(image, 2048)
    if result is not None and result[0] != 0:
        emu.fail("fsck.fat: " + result[1][-300:])
    print("sdcard: the folder, the names, the sizes, the CRCs and every record check out; file 1 holds the original words "
          "and file 42 the changed ones" + ("" if result is None else "; fsck.fat is clean"))
    if result is None:
        print("sdcard: SKIP fsck.fat: not installed. The reader above is the check that counts.")

    # 5. A state of the stopped game, decoded by the reader, against the game's memory.
    yield from decode_against_memory(emu, sym, box, image, requests, card_op, ram_save)

    # The traces for the run that loads the files.
    keep = {"1": traces[0], "42": traces[2], "100": traces[3]}
    json.dump({k: [v[0], [[f, sorted(map(list, s))] for f, s in sorted(v[1].items())]] for k, v in keep.items()},
              open(os.path.join(out, "traces.json"), "w"))
    json.dump({"changed": changed, "original": original}, open(os.path.join(out, "words.json"), "w"))
    print("sdcard: ok")


def sha_initial(image, out):
    """The hash of the freshly made image, kept in a file the first time."""
    path = os.path.join(out, "initial.sha")
    if not os.path.exists(path):
        open(path, "w").write(sha(image))
    return open(path).read()


def decode_against_memory(emu, sym, box, image, requests, card_op, ram_save):
    """Stops the game, saves a state into the pool and from there to card file 50, decodes the file with the reader and
    compares it with the game's memory, which has not moved since the save."""
    from sdutil import read_card_file
    emu.release()
    run_addr = sym("run")
    emu.w8(run_addr + RUN_FRAME_ADVANCE, 1)
    yield 30
    frame = emu.r32(sym("frameCount"))
    yield 10
    if emu.r32(sym("frameCount")) != frame:
        emu.fail("the game did not stop")
    before = emu.r32(sym("stateSaves"))
    emu.w8(sym("stateSlot"), 4)
    emu.w16(requests, emu.r16(requests) | 1 << ACTION_SAVE_STATE)
    for _ in range(8):        # a request waits for the next game frame, and the save for the one after
        if emu.r32(sym("stateSaves")) > before:
            break
        emu.w8(run_addr + RUN_STEPS, 1)
        yield 10
    if emu.r32(sym("stateSaves")) <= before:
        emu.fail("the save into the pool did not run")
    yield 10
    # The draw thread and the audio thread go on while the game is stopped and change some of its memory. A word that
    # is the same in two snapshots taken before and after the card save did not change in between, and the file must
    # hold it.
    first = {addr: emu.read(addr, n) for addr, n in chunks()}
    yield from card_op(REQ_SAVE, 50, 4)
    yield 10
    second = {addr: emu.read(addr, n) for addr, n in chunks()}
    _, memory = parse_card_file(read_card_file(image, 50))
    moved = stable = 0
    wrong = []
    for addr, n in chunks():
        a, b, f = first[addr], second[addr], memory[addr]
        if a == b == f:
            stable += n // 4
            continue
        for off in range(0, n, 4):
            if a[off:off + 4] == b[off:off + 4]:
                stable += 1
                if f[off:off + 4] != a[off:off + 4]:
                    wrong.append(addr + off)
            else:
                moved += 1
    # The audio code's buffers (the ROM's VERIFY_VOLATILE range) and currentTask (a load keeps the draw thread's own
    # value) are not the game's. Nor is the draw thread's matrix scratch, from matrixStack up to atanStep: the draw
    # thread keeps drawing while frame advance stops the game. A word there can hold a value from the middle of a
    # draw when the save copies it, and the finished value in both snapshots.
    wrong = [a for a in wrong if not 0x80296C38 <= a < 0x802AB010 and a != sym("currentTask")
             and not sym("matrixStack") <= a < sym("atanStep")]
    addrs = [a for a, _ in sym.labels]
    names = sorted({sym.labels[bisect_right(addrs, a) - 1][1] for a in wrong})
    print(f"sdcard: the decoded file against the stopped game's memory: {stable} words stable, {moved} moved by other "
          f"threads, {len(wrong)} stable words differ from the file {names[:8]}")
    if wrong:
        emu.fail(f"the file differs from the game's memory at {[hex(a) for a in wrong[:8]]}")
    emu.w8(run_addr + RUN_FRAME_ADVANCE, 0)
    yield 5


from bisect import bisect_right  # noqa: E402
