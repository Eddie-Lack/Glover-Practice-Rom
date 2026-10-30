"""Loads the card files that tests/sdcard.py made (run that first), in another emulator run, and compares the game
with what the first run saw after each save, as tests/slots.py does for the pool.

1. In another level, card files 1, 42 and 100 load: the level is right, the game frames after the load equal those
   after the save, and file 42's words (a change that keeps the chunk's sums) are back.
2. A file that is not there says so. A file with one byte changed (the CRC) is refused before the game's memory is
   touched: the level and the frames go on. So are files with one wrong header field (magic, version, header size,
   layout hash, chunk count, state size, level; the CRC is right in these) and files of 40, 67 and 5000 bytes.
3. A load through the menu.
4. No sector was written by any of this: the image is as the first run left it, but for the byte that this test
   changes itself.
The image is copied first; the first run's copy stays as it was."""
# The ROM with the test mailbox for a cart (make testrom).
TEST_BUILD = True

import json
import os
import shutil

import layout
import trace
from menunav import goto as menu_goto
from sdutil import Mailbox, REQ_LOAD, card_files, edit_card_file, restore_card_file, sha, write_card_file_raw
from stateutil import hud_message
from tools import C_ALL, press, wait_for

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
FIRST = os.path.join(ROOT, "build", "tests", "sdcard")
ACTION_WARP = 1
RUN_WARP_LEVEL = 4
TRACE = 40
STICK = dict(x=0, y=80)
COLLIDE = 0x800FF000
OTHER_LEVEL = 30
ITEM_LOAD = "Load from card"


def same(emu, a, b, what):
    a = {f: v for f, v in a.items() if f > min(a) + 2}
    bad = [f for f in sorted(a) if f in b and not a[f] & b[f]]
    missing = [f for f in a if f not in b]
    if bad:
        emu.fail(f"{what}: game frame {bad[0]} differs: {sorted(a[bad[0]])[0]} and {sorted(b[bad[0]])[0]}")
    if len(missing) > 3 or len(a) < TRACE - 4:
        emu.fail(f"{what}: only {len(a) - len(missing)} of {len(a)} game frames were seen again")


def run(emu, sym, args):
    image = os.path.join(args.out, "sd.img")
    shutil.copy(os.path.join(FIRST, "sd.img"), image)
    box = Mailbox(sym, image)
    box.start(emu)
    try:
        yield from scenario(emu, sym, args, box, image)
    finally:
        box.stop()


def scenario(emu, sym, args, box, image):
    traces = {int(k): (v[0], {f: {tuple(s) for s in states} for f, states in v[1]})
              for k, v in json.load(open(os.path.join(FIRST, "traces.json"))).items()}
    words = json.load(open(os.path.join(FIRST, "words.json")))
    gi, fc_addr = sym("gameInfo"), sym("frameCount")
    hand, ball, player = sym("hand"), sym("ball"), sym("player")
    requests = sym("requests")
    mode = lambda: emu.r8(gi)  # noqa: E731
    level = lambda: emu.r8(gi + 1)  # noqa: E731
    fc = lambda: emu.r32(fc_addr)  # noqa: E731
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

    def card_load(number):
        before = done()
        emu.w8(sym("cardSlot"), number)
        emu.w8(sym("cardWanted"), REQ_LOAD)
        yield from wait_for(emu, lambda: done() > before, 6000, f"the load of card file {number}")
        yield 3
        box.check(emu)

    for i in layout.SETTINGS_BINDINGS:
        emu.w8(sym("settings") + i, 0)
    emu.w8(sym("sdBackend"), 1)
    start_hash = sha(image)
    yield from wait_for(emu, lambda: level() == trace.PRESENT and fc() >= 200, 4000, "the logos")
    yield from press(emu, "START", frames=6)
    yield from wait_for(emu, lambda: level() == trace.FLYTHRU and mode() == 4 and fc() >= 30, 3000, "the title")
    emu.w8(sym("run") + RUN_WARP_LEVEL, OTHER_LEVEL)
    emu.w16(requests, emu.r16(requests) | 1 << ACTION_WARP)
    yield from wait_for(emu, lambda: level() == OTHER_LEVEL and mode() == 4 and 40 <= fc() < 400, 4000, "another level")
    emu.hold(**STICK)

    # 1. Three files.
    for number in (1, 42, 100, 1):
        want_level, want = traces[number]
        yield from card_load(number)
        shown = hud_message(emu, sym)
        yield from wait_for(emu, lambda: mode() == 4, 200, "the level")
        t = yield from collect(TRACE)
        if level() != want_level or not shown.startswith(f"loaded card {number}: "):
            emu.fail(f"card file {number}: level {level()} (wanted {want_level}), message \"{shown}\"")
        same(emu, want, t, f"after the load of card file {number}")
        if number == 42:
            got = [emu.r32(COLLIDE + 4 * k) for k in range(3)]
            if got != words["changed"]:
                emu.fail(f"card file 42 was saved with words {words['changed']}; it loaded {got}")
            for k in range(3):
                emu.w32(COLLIDE + 4 * k, words["original"][k])
        ticks, wait = emu.r32(sym("cardLastTicks")), emu.r32(sym("cardLastWait"))
        print(f"sdcard load: card file {number}: level {level()}, {ticks / 46875:.0f} ms, of which {wait / 46875:.0f} ms "
              f"waiting for the cart and {(ticks - wait) / 46875:.0f} ms of the console's own work; \"{shown}\"")
        yield 20
    if box.writes != 0 or sha(image) != start_hash:
        emu.fail("a load wrote to the card")

    # 2. A file that is not there, and one with a changed byte.
    yield from card_load(7)
    if hud_message(emu, sym) != "no card file 7":
        emu.fail(f"a file that is not there: \"{hud_message(emu, sym)}\"")
    img, _, files = card_files(image)
    first, size = files[100]
    lba = img.data_start + (first - 2) * img.spc
    img.close()
    with open(image, "r+b") as f:
        f.seek(lba * 512 + 300)
        byte = f.read(1)
        f.seek(lba * 512 + 300)
        f.write(bytes([byte[0] ^ 0x5A]))
    before_level = level()
    yield from wait_for(emu, lambda: mode() == 4, 200, "the level")
    yield from card_load(100)
    if hud_message(emu, sym) != "card file 100 is not a usable state":
        emu.fail(f"a file with a changed byte: \"{hud_message(emu, sym)}\"")
    t = yield from collect(TRACE)
    if level() != before_level:
        emu.fail("a refused file changed the level")
    print("sdcard load: a missing file and a file with a changed byte are refused; the game goes on")
    if box.writes != 0:
        emu.fail("a refused load wrote to the card")

    # 2b. A file with one wrong field in its header (the CRC is made right again: only the field is wrong), and short
    # files. Each is refused, the game goes on and nothing is written.
    def put(index, value):
        def edit(d):
            d[index] = value
        return edit

    def xor(index, value):
        def edit(d):
            d[index] ^= value
        return edit

    def magic(d):
        d[0:4] = b"XPSS"

    header_cases = [("magic", magic), ("version 1", put(5, 1)), ("version 3", put(5, 3)), ("header size", put(7, 32)),
                    ("layout hash", xor(20, 1)), ("chunk count", xor(11, 1)), ("state bytes", xor(19, 1)),
                    ("level", put(8, 200))]
    before_level = level()
    for what, edit in header_cases:
        saved = edit_card_file(image, 42, edit)
        box.switch(image)
        yield from card_load(42)
        if hud_message(emu, sym) != "card file 42 is not a usable state":
            emu.fail(f"a file with a wrong {what}: \"{hud_message(emu, sym)}\"")
        yield from wait_for(emu, lambda: mode() == 4, 200, "the level")
        yield from collect(10)
        if level() != before_level:
            emu.fail(f"a file with a wrong {what} changed the level")
        restore_card_file(image, 42, saved)
        box.switch(image)
    data, size = saved
    for what, short in (("40 bytes", 40), ("67 bytes", 67), ("5000 bytes", 5000)):
        write_card_file_raw(image, 42, data, short)
        box.switch(image)
        yield from card_load(42)
        if hud_message(emu, sym) != "card file 42 is not a usable state":
            emu.fail(f"a file of {what}: \"{hud_message(emu, sym)}\"")
        yield from wait_for(emu, lambda: mode() == 4, 200, "the level")
        yield from collect(10)
        if level() != before_level:
            emu.fail(f"a file of {what} changed the level")
    write_card_file_raw(image, 42, data, size)
    box.switch(image)
    yield from card_load(42)
    if not hud_message(emu, sym).startswith("loaded card 42: "):
        emu.fail(f"file 42 after the tests: \"{hud_message(emu, sym)}\"")
    yield from wait_for(emu, lambda: mode() == 4, 200, "the level")
    for k in range(3):
        emu.w32(COLLIDE + 4 * k, words["original"][k])
    print(f"sdcard load: wrong magic, version, header size, layout, chunk count, state size and level, and files of 40, 67 and "
          f"5000 bytes are refused; the game goes on")
    if box.writes != 0:
        emu.fail("a refused load wrote to the card")

    # 3. Through the menu.
    emu.w8(sym("cardSlot"), 1)
    emu.release()
    before = done()
    yield from press(emu, *C_ALL)
    yield from menu_goto(emu, sym, "Savestate", "SD card", ITEM_LOAD)
    yield from wait_for(emu, lambda: done() > before, 6000, "the load through the menu")
    yield 10
    emu.hold(**STICK)
    if level() != traces[1][0] or emu.r8(sym("menuOpen")):
        emu.fail(f"the load through the menu: level {level()}")
    print(f"sdcard load: through the menu: \"{hud_message(emu, sym)}\"")
    print("sdcard load: ok")
