"""A card that fills up: an 8 MB FAT16 card takes about twelve states.

1. Save one state into card files 1, 2, 3 and on until the card is full: the message says so, and the files that were
   saved before are whole (the reader decodes each).
2. The save that failed left nothing behind: no lost clusters (the reader, and fsck.fat if installed).
3. A save into file 1 while the card is full (it needs room for the new file while the old one stands) fails, and
   file 1 is as it was. After file 2 is deleted, it works.
The save and load times and the sizes are printed."""
# The ROM with the test mailbox for a cart (make testrom).
TEST_BUILD = True

import os

import fatimage
import layout
import trace
from sdutil import REQ_DELETE, REQ_SAVE, Mailbox, card_files, parse_card_file
from stateutil import hud_message
from tools import press, wait_for

ACTION_SAVE_STATE = 8
STICK = dict(x=0, y=80)


def run(emu, sym, args):
    image = os.path.join(args.out, "sd.img")
    fatimage.format_image(image, 8, 16, 2, False)
    box = Mailbox(sym, image)
    box.start(emu)
    try:
        yield from scenario(emu, sym, args, box, image)
    finally:
        box.stop()


def scenario(emu, sym, args, box, image):
    gi, fc_addr = sym("gameInfo"), sym("frameCount")
    requests = sym("requests")
    mode = lambda: emu.r8(gi)  # noqa: E731
    level = lambda: emu.r8(gi + 1)  # noqa: E731
    fc = lambda: emu.r32(fc_addr)  # noqa: E731
    done = lambda: emu.r32(sym("cardDone"))  # noqa: E731

    def card_op(request, number, ram_slot=0):
        before = done()
        emu.w8(sym("cardSlot"), number)
        emu.w8(sym("stateSlot"), ram_slot)
        emu.w8(sym("cardWanted"), request)
        yield from wait_for(emu, lambda: done() > before, 6000, f"card request {request}")
        yield 3
        box.check(emu)
        return hud_message(emu, sym)

    def whole(numbers):
        img, _, files = card_files(image)
        try:
            img.check()
            if sorted(files) != sorted(numbers):
                emu.fail(f"the folder holds {sorted(files)}, not {sorted(numbers)}")
            for n in numbers:
                first, size = files[n]
                parse_card_file(img.read(first, size))
        finally:
            img.close()
        result = fatimage.fsck(image)
        if result is not None and result[0] != 0:
            emu.fail("fsck.fat: " + result[1][-300:])
        if result is None:
            print("sdcardfull: SKIP fsck.fat: not installed. The reader above is the check that counts.")

    for i in layout.SETTINGS_BINDINGS:
        emu.w8(sym("settings") + i, 0)
    emu.w8(sym("sdBackend"), 1)
    yield from wait_for(emu, lambda: level() == trace.PRESENT and fc() >= 200, 4000, "the logos")
    yield from press(emu, "START", frames=6)
    yield from wait_for(emu, lambda: level() == trace.FLYTHRU and mode() == 4 and fc() >= 30, 3000, "the title")
    from tools import C_ALL
    yield from press(emu, *C_ALL)
    for b in ("A", "DOWN", "DOWN", "A"):
        yield from press(emu, b)
    yield from wait_for(emu, lambda: level() == trace.ATLANTIS1 and mode() == 4 and 0 < fc() < 20, 3000, "Atlantis 1")
    emu.hold(**STICK)
    yield from wait_for(emu, lambda: fc() >= 80, 1000, "game frame 80")
    before = emu.r32(sym("stateSaves"))
    emu.w16(requests, emu.r16(requests) | 1 << ACTION_SAVE_STATE)
    yield from wait_for(emu, lambda: emu.r32(sym("stateSaves")) > before, 1500, "the save")
    yield 5

    saved = []
    for n in range(1, 40):
        message = yield from card_op(REQ_SAVE, n)
        if message.startswith("saved to card"):
            saved.append(n)
            continue
        if message != "not saved to card: the card is full":
            emu.fail(f"card file {n}: \"{message}\"")
        break
    else:
        emu.fail("the card did not fill")
    print(f"sdcardfull: {len(saved)} files fit; the next said \"{message}\"")
    if not 8 <= len(saved) <= 13:
        emu.fail(f"{len(saved)} files fit on an 8 MB card")
    whole(saved)
    print("sdcardfull: the files that were saved are whole, and the failed save left no lost clusters")

    message = yield from card_op(REQ_SAVE, 1)
    if message != "not saved to card: the card is full":
        emu.fail(f"a replacement on a full card: \"{message}\"")
    whole(saved)
    yield from card_op(REQ_DELETE, 2)
    saved.remove(2)
    message = yield from card_op(REQ_SAVE, 1)
    if not message.startswith("saved to card 1: "):
        emu.fail(f"a replacement after a delete: \"{message}\"")
    whole(saved)
    print("sdcardfull: a replacement fails on a full card and leaves the old file; after a delete it works")
    print("sdcardfull: ok")
