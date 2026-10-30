"""The first save to a populated large card: a 2200 MB FAT32 card (a sparse image) with 3200 files, folders, and a file of
1.2 GB in front, as tests/fatimage.py's make_large_card builds it. The save has to make the folder and find free
clusters behind everything that is there.

The sectors and commands that the card code asked for, and its own time (the total less the time spent waiting for the
mailbox's answers), are printed; docs/build.md quotes them. The file is then decoded by the reader, and fsck.fat looks at
the image."""
# The ROM with the test mailbox for a cart (make testrom).
TEST_BUILD = True

import os

import fatimage
import layout
import trace
from sdutil import REQ_SAVE, Mailbox, card_files, parse_card_file
from stateutil import hud_message
from tools import C_ALL, press, wait_for

ACTION_SAVE_STATE = 8
STICK = dict(x=0, y=80)


def run(emu, sym, args):
    image = os.path.join(args.out, "large.img")
    fatimage.make_large_card(image)
    box = Mailbox(sym, image)
    box.start(emu)
    try:
        yield from scenario(emu, sym, args, box, image)
    finally:
        box.stop()
        os.remove(image)


def scenario(emu, sym, args, box, image):
    gi, fc_addr = sym("gameInfo"), sym("frameCount")
    requests = sym("requests")
    mode = lambda: emu.r8(gi)  # noqa: E731
    level = lambda: emu.r8(gi + 1)  # noqa: E731
    fc = lambda: emu.r32(fc_addr)  # noqa: E731
    done = lambda: emu.r32(sym("cardDone"))  # noqa: E731

    for i in layout.SETTINGS_BINDINGS:
        emu.w8(sym("settings") + i, 0)
    emu.w8(sym("sdBackend"), 1)
    yield from wait_for(emu, lambda: level() == trace.PRESENT and fc() >= 200, 4000, "the logos")
    yield from press(emu, "START", frames=6)
    yield from wait_for(emu, lambda: level() == trace.FLYTHRU and mode() == 4 and fc() >= 30, 3000, "the title")
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

    before = done()
    emu.w8(sym("cardSlot"), 1)
    emu.w8(sym("stateSlot"), 0)
    emu.w8(sym("cardWanted"), REQ_SAVE)
    yield from wait_for(emu, lambda: done() > before, 12000, "the first save to the large card")
    yield 3
    box.check(emu)
    message = hud_message(emu, sym)
    if not message.startswith("saved to card 1: "):
        emu.fail(f"the first save to the large card: \"{message}\"")
    ticks, wait = emu.r32(sym("cardLastTicks")), emu.r32(sym("cardLastWait"))
    print(f"sdcardbig: first save to a 2200 MB card with 3200 files and 300,000 clusters in use: {box.reads} sectors read "
          f"in {box.read_commands} commands, {box.writes} written in {box.write_commands}; {ticks / 46875:.0f} ms, of which "
          f"{wait / 46875:.0f} ms waiting for the mailbox and {(ticks - wait) / 46875:.0f} ms the console's own work; \"{message}\"")

    img, folder, files = card_files(image)
    try:
        img.check()
        if sorted(files) != [1]:
            emu.fail(f"the folder holds {sorted(files)}")
        first, size = files[1]
        parse_card_file(img.read(first, size))
    finally:
        img.close()
    result = fatimage.fsck(image, 0)
    if result is not None and result[0] != 0:
        emu.fail("fsck.fat: " + result[1][-300:])
    if result is None:
        print("sdcardbig: SKIP fsck.fat: not installed. The reader above is the check that counts.")
    print("sdcardbig: the file is whole and the card has no lost clusters" + ("" if result is None else "; fsck.fat is clean"))
    print("sdcardbig: ok")
