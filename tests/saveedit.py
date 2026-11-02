"""The save file editor (Settings, Save file; practice/src/saveedit.c, build.md "The save file editor").

1. The editor changes its own copy of the save (saveEditSlots, saveEditBests): file 1's lives, crystals won and
   returned, the first level's done flag, all-garibs flag and best time. The game's copy in RAM and the EEPROM file
   stay as they were until "Write". Lives start at 3, crystals returned stay at or below crystals won, and the
   difficulty of the file in play (currentSaveSlot) does not change, in the editor or at Write.
2. A is asked twice: the first A only asks (a confirmation), another button cancels it and writes nothing.
3. Write: the game's copy gets the edits, the EEPROM file (the game's blocks 31 to 51 for the files, 7 to 30 for
   the best times, 52 to 55 for the CRCs) holds them, and every CRC is valid, decoded as the game does (CRC-32 with
   the polynomial 0x04C11DB7, no inversion; docs/game.md).
4. Reload asks first as well. After it, an edit that was not written is gone, the written values are back, and the
   game's copy holds the cartridge's data. A request is refused while another is pending (the game has an EEPROM
   request of its own).
   The run also writes a copy of its save folder with one byte of file 2 changed (build/tests/saveedit/save_bad),
   for saveeditbad.py.
The merge keeps the game's change to bits and counts that were not edited (levels 0x7, crystals 1 and 1 at the end).
saveeditload.py then starts a new emulator from this run's save folder."""
import glob
import os
import shutil

import layout
import trace
from menunav import goto
from tools import C_ALL, press, wait_for

SLOT_SIZE = 0x1C
SLOTS_ADDR, TIMES_ADDR, CRC_ADDR = 0x1F * 8, 0x07 * 8, 0x34 * 8
SLOTS_LEN, TIMES_LEN, HISCORE_ADDR, HISCORE_LEN = 0xA8, 0xC0, 8, 0x30


def crc(data):
    table = []
    for i in range(256):
        c = i << 24
        for _ in range(8):
            c = ((c << 1) ^ 0x04C11DB7) & 0xFFFFFFFF if c & 0x80000000 else (c << 1) & 0xFFFFFFFF
        table.append(c)
    c = 0
    for b in data:
        c = ((c << 8) & 0xFFFFFFFF) ^ table[(c >> 24) ^ b]
    return c


def check_crcs(data):
    """The EEPROM image's six file CRCs, the hiscore CRC and the best time CRC, against the blocks they cover."""
    stored = [int.from_bytes(data[CRC_ADDR + 4 * i:CRC_ADDR + 4 * i + 4], "big") for i in range(8)]
    ok = [crc(data[SLOTS_ADDR + i * SLOT_SIZE:SLOTS_ADDR + (i + 1) * SLOT_SIZE]) == stored[i] for i in range(6)]
    ok.append(crc(data[HISCORE_ADDR:HISCORE_ADDR + HISCORE_LEN]) == stored[6])
    ok.append(crc(data[TIMES_ADDR:TIMES_ADDR + TIMES_LEN]) == stored[7])
    return ok


def run(emu, sym, args):
    gi = sym("gameInfo")
    slot0 = sym("saveSlot")
    times = sym("bestTimes")
    se = sym("saveEdit")
    levels, garibs, lives, won, returned = slot0 + 4, slot0 + 8, slot0 + 0x10, slot0 + 0x12, slot0 + 0x13
    # The editor's own copy of file 1 and of the first best time.
    shadow = sym("saveEditSlots")
    s_levels, s_garibs, s_lives, s_diff, s_won, s_returned = (shadow + 4, shadow + 8, shadow + 0x10, shadow + 0x11,
                                                              shadow + 0x12, shadow + 0x13)
    s_times = sym("saveEditBests")

    def eeprom():
        files = glob.glob(os.path.join(args.out, "save", "*.eep"))
        if not files:
            emu.fail("the emulator wrote no EEPROM file")
        return open(files[0], "rb").read()

    def shot(name):
        if args.gfx == "angrylion":
            emu.fb_png(name, sym("cfb_16_b"))
            emu.fb_png(name + "_0", sym("cfb_16_a"))

    while not (emu.r8(gi + 1) == trace.PRESENT and emu.r32(sym("frameCount")) >= 200):
        yield 1
    yield from press(emu, "START")
    yield 600
    initial = eeprom()
    if not all(check_crcs(initial)):
        emu.fail(f"the fresh EEPROM has bad CRCs: {check_crcs(initial)}")
    time0 = emu.r16s(times)
    if emu.r32(levels) != 0 or emu.r8(lives) != 0:
        emu.fail("file 1 of a fresh EEPROM is not blank")

    yield from press(emu, *C_ALL)
    if not emu.r8(sym("menuOpen")):
        emu.fail("the menu did not open")
    yield from goto(emu, sym, "Settings", "Save file")
    shot("file_page")
    # Lives 5 (the first step goes from 0 to 3), crystals won 2, returned 1.
    d0 = emu.r8(slot0 + 0x11)
    for b in ["DOWN"] + ["RIGHT"] * 3 + ["DOWN"] + ["RIGHT"] * 2 + ["DOWN", "RIGHT"]:
        yield from press(emu, b)
    if (emu.r8(s_lives), emu.r8(s_won), emu.r8(s_returned)) != (5, 2, 1):
        emu.fail(f"lives, won, returned are {(emu.r8(s_lives), emu.r8(s_won), emu.r8(s_returned))}, not (5, 2, 1)")
    # Crystals returned stop at crystals won; won going down takes returned with it.
    for b in ["RIGHT"] * 4:
        yield from press(emu, b)
    if emu.r8(s_returned) != 2:
        emu.fail(f"crystals returned are {emu.r8(s_returned)}, not 2 (the crystals won)")
    # The cursor goes to the won row first, so the clamp (not the returned row's own step) lowers returned.
    for b in ["UP", "LEFT"]:
        yield from press(emu, b)
    if (emu.r8(s_won), emu.r8(s_returned)) != (1, 1):
        emu.fail(f"won {emu.r8(s_won)} and returned {emu.r8(s_returned)}, not 1 and 1")
    for b in ["RIGHT", "DOWN"]:
        yield from press(emu, b)
    if (emu.r8(s_won), emu.r8(s_returned)) != (2, 1):
        emu.fail(f"won {emu.r8(s_won)} and returned {emu.r8(s_returned)}, not 2 and 1")
    # Difficulty: the file in play (currentSaveSlot) keeps its own. File 1 is in play after the boot.
    cur = sym("currentSaveSlot")
    emu.w16(cur, 0)
    yield from press(emu, "DOWN")
    yield from press(emu, "RIGHT")
    if emu.r8(s_diff) != d0:
        emu.fail(f"the difficulty of the file in play is {emu.r8(s_diff)}, not {d0}")
    # With another file in play the edit is allowed.
    emu.w16(cur, 1)
    yield from press(emu, "RIGHT")
    want = (d0 + 1) % 3
    if emu.r8(s_diff) != want:
        emu.fail(f"difficulty is {emu.r8(s_diff)}, not {want} (was {d0})")
    # The game's copy has none of it.
    if emu.r8(lives) != 0 or emu.r8(won) != 0 or emu.r8(returned) != 0 or emu.r8(slot0 + 0x11) != d0:
        emu.fail("the game's copy of file 1 changed before Write")
    # Levels, first row: done, then all garibs, then the time up by 3 seconds.
    for b in ["DOWN", "A", "RIGHT", "A", "RIGHT", "A", "RIGHT", "RIGHT", "RIGHT"]:
        yield from press(emu, b)
    shot("levels_page")
    if emu.r8(se + layout.SAVEEDIT_PAGE) != 1:
        emu.fail("the levels page did not open")
    if emu.r32(s_levels) != 1 or emu.r32(s_garibs) != 1 or emu.r16s(s_times) != time0 + 3:
        emu.fail(f"level edits: levels {emu.r32(s_levels)}, garibs {emu.r32(s_garibs)}, time {emu.r16s(s_times)} "
                 f"(was {time0})")
    if emu.r32(levels) != 0 or emu.r32(garibs) != 0 or emu.r16s(times) != time0:
        emu.fail("the game's copy of the levels changed before Write")
    yield from press(emu, "B")
    yield 120
    if eeprom() != initial:
        emu.fail("the EEPROM changed before Write")
    # A save by the game itself with the edits pending (UpdateSaveSlot posts requests 6 and 11, without 4): the
    # cartridge gets none of the edits and every CRC stays valid. The two writes make the same request queue as the
    # game's, in one step of the harness.
    queue = sym("eepromMessageQueue")
    emu.w8(queue + 1, 11)
    emu.w8(sym("eepromMessageNum"), 2)
    emu.w8(queue, 6)
    yield 200
    after = eeprom()
    if emu.r8(queue) or emu.r8(sym("eepromMessageNum")):
        emu.fail("the game's own save requests did not run")
    if not all(check_crcs(after)):
        emu.fail(f"CRCs valid after a save by the game with edits pending: {check_crcs(after)}")
    if after[SLOTS_ADDR:SLOTS_ADDR + SLOT_SIZE] != initial[SLOTS_ADDR:SLOTS_ADDR + SLOT_SIZE] or \
            after[TIMES_ADDR:TIMES_ADDR + 2] != initial[TIMES_ADDR:TIMES_ADDR + 2]:
        emu.fail("a save by the game wrote an edit")

    # A asks, another button cancels: nothing is written.
    yield from press(emu, "DOWN")  # Write
    yield from press(emu, "A")
    if not emu.r8(se + layout.SAVEEDIT_CONFIRM):
        emu.fail("A on Write did not ask")
    shot("confirm")
    yield from press(emu, "B")
    if emu.r8(se + layout.SAVEEDIT_CONFIRM) or emu.r8(se + layout.SAVEEDIT_REQUEST):
        emu.fail("B did not cancel the question")
    if not emu.r8(sym("menuOpen")):
        emu.fail("B closed the menu with the question open")
    yield 120
    if eeprom() != initial:
        emu.fail("the EEPROM changed after a cancelled write")

    yield from press(emu, "A")
    yield from press(emu, "A")
    yield 400
    if emu.r8(se + layout.SAVEEDIT_STATUS) != 1:
        emu.fail(f"the status is {emu.r8(se + layout.SAVEEDIT_STATUS)}, not 1 (written)")
    shot("written")
    data = eeprom()
    slot = data[SLOTS_ADDR:SLOTS_ADDR + SLOT_SIZE]
    if int.from_bytes(slot[4:8], "big") != 1 or int.from_bytes(slot[8:12], "big") != 1:
        emu.fail(f"the EEPROM slot 1 has levels/garibs {slot[4:12].hex()}")
    if (slot[0x10], slot[0x11], slot[0x12], slot[0x13]) != (5, want, 2, 1):
        emu.fail(f"the EEPROM slot 1 has lives/difficulty/won/returned {slot[0x10:0x14].hex()}")
    if (emu.r8(lives), emu.r8(won), emu.r8(returned), emu.r32(levels), emu.r16s(times)) != (5, 2, 1, 1, time0 + 3):
        emu.fail("the game's copy of file 1 does not hold the written edits")
    if int.from_bytes(data[TIMES_ADDR:TIMES_ADDR + 2], "big") != time0 + 3:
        emu.fail("the EEPROM's first best time is not the edited one")
    ok = check_crcs(data)
    if not all(ok):
        emu.fail(f"CRCs valid: {ok}")
    if data[0x1C0:0x200] != initial[0x1C0:0x200]:
        emu.fail("the editor changed the practice settings' blocks")

    # A difficulty edit made while another file was in play: if file 1 is in play at the write, the write keeps
    # its difficulty, in the game's copy, in the editor's and on the cartridge.
    for b in ["UP"] * 2 + ["RIGHT"]:
        yield from press(emu, b)
    if emu.r8(s_diff) != (want + 1) % 3:
        emu.fail(f"difficulty is {emu.r8(s_diff)}, not {(want + 1) % 3}")
    emu.w16(cur, 0)
    for b in ["DOWN"] * 2 + ["A", "A"]:
        yield from press(emu, b)
    yield 400
    if emu.r8(slot0 + 0x11) != want or emu.r8(s_diff) != want or eeprom()[SLOTS_ADDR + 0x11] != want:
        emu.fail(f"the write changed the difficulty of the file in play: game {emu.r8(slot0 + 0x11)}, editor "
                 f"{emu.r8(s_diff)}, cartridge {eeprom()[SLOTS_ADDR + 0x11]}, was {want}")
    if not all(check_crcs(eeprom())):
        emu.fail(f"CRCs valid after the second write: {check_crcs(eeprom())}")

    # An edit and the game's own change to the same word or the same pair: neither is lost. The editor is closed
    # and opened again after the game's change, and a crystal count that the game raised stays within the count won.
    for b in ["UP", "A", "A", "DOWN", "RIGHT"]:  # the levels page, its first column (done), the second level
        yield from press(emu, b)
    if emu.r32(s_levels) != 3:
        emu.fail(f"the editor's levels are {emu.r32(s_levels):#x}, not 0x3")
    emu.w32(levels, 5)  # the game finishes the third level while the editor is closed
    for b in ["B", "B", "A"]:  # the file page, Settings, the editor again
        yield from press(emu, b)
    if emu.r32(s_levels) != 7:
        emu.fail(f"the reopened editor has levels {emu.r32(s_levels):#x}, not 0x7 (the edit and the game's change)")
    for b in ["DOWN", "DOWN", "LEFT"]:  # crystals won 2 to 1: returned stays 1
        yield from press(emu, b)
    if (emu.r8(s_won), emu.r8(s_returned)) != (1, 1):
        emu.fail(f"won {emu.r8(s_won)} and returned {emu.r8(s_returned)}, not 1 and 1")
    emu.w8(returned, 2)  # the game returns another crystal while the editor is closed
    for b in ["DOWN"] * 4 + ["A", "A"]:
        yield from press(emu, b)
    yield 400
    if (emu.r32(levels), emu.r8(won), emu.r8(returned)) != (7, 1, 1):
        emu.fail(f"after the write the game's copy has levels {emu.r32(levels):#x}, won {emu.r8(won)}, returned "
                 f"{emu.r8(returned)}, not 0x7, 1, 1 (the clamp)")
    slot = eeprom()[SLOTS_ADDR:SLOTS_ADDR + SLOT_SIZE]
    if int.from_bytes(slot[4:8], "big") != 7 or (slot[0x12], slot[0x13]) != (1, 1):
        emu.fail(f"the EEPROM slot 1 after the merge: {slot.hex()}")
    if not all(check_crcs(eeprom())):
        emu.fail(f"CRCs valid after the merge: {check_crcs(eeprom())}")
    data = eeprom()

    # Reload discards an edit that was not written. It asks first; another button cancels.
    for b in ["UP"] * 5 + ["RIGHT"] * 3:
        yield from press(emu, b)
    if emu.r8(s_lives) != 8:
        emu.fail(f"lives are {emu.r8(s_lives)}, not 8")
    for b in ["DOWN"] * 6 + ["A"]:
        yield from press(emu, b)
    if emu.r8(se + layout.SAVEEDIT_CONFIRM) != 2:
        emu.fail("A on Reload did not ask")
    yield from press(emu, "B")
    yield 60
    # The game's copy differs from the cartridge's: only a reload that completes puts the cartridge's data back.
    emu.w8(lives, 9)
    if emu.r8(se + layout.SAVEEDIT_CONFIRM) or emu.r8(se + layout.SAVEEDIT_REQUEST) or emu.r8(s_lives) != 8 or \
            emu.r8(lives) != 9:
        emu.fail("B did not cancel the reload question")
    yield from press(emu, "A")
    yield from press(emu, "A")
    yield 400
    if emu.r8(s_lives) != 5 or emu.r8(se + layout.SAVEEDIT_STATUS) != 2:
        emu.fail(f"reload: lives {emu.r8(s_lives)}, status {emu.r8(se + layout.SAVEEDIT_STATUS)}")
    if emu.r8(lives) != 5:
        emu.fail(f"the reload left the game's copy with {emu.r8(lives)} lives, not the cartridge's 5")
    if eeprom() != data:
        emu.fail("the EEPROM changed on Reload")
    # A second request is refused while one is pending. A count in the game's request queue with no request makes
    # the game look busy: the service waits, and nothing takes the count back.
    num = sym("eepromMessageNum")
    emu.w8(num, 1)
    for b in ["A", "A"]:  # the cursor is on Reload
        yield from press(emu, b)
    yield 60
    if emu.r8(se + layout.SAVEEDIT_REQUEST) != 2:
        emu.fail("a reload did not wait while the game had an EEPROM request")
    yield from press(emu, "A")
    if emu.r8(se + layout.SAVEEDIT_CONFIRM):
        emu.fail("A on Reload asked while a request was pending")
    yield from press(emu, "UP")  # Write
    yield from press(emu, "A")
    if emu.r8(se + layout.SAVEEDIT_CONFIRM) or emu.r8(se + layout.SAVEEDIT_REQUEST) != 2:
        emu.fail("A on Write asked, or replaced the request that was pending")
    emu.w8(num, 0)
    yield 200
    if emu.r8(se + layout.SAVEEDIT_REQUEST) or emu.r8(se + layout.SAVEEDIT_STATUS) != 2:
        emu.fail("the pending reload did not run when the game's queue was empty")
    # A save folder with one byte of file 2 changed, for saveeditbad.py. The emulator keeps the file in memory
    # and writes it out as the game writes it. The copy is made from the file on disk.
    bad = os.path.join(args.out, "save_bad")
    shutil.rmtree(bad, ignore_errors=True)
    shutil.copytree(os.path.join(args.out, "save"), bad)
    path = glob.glob(os.path.join(bad, "*.eep"))[0]
    image = bytearray(open(path, "rb").read())
    image[SLOTS_ADDR + SLOT_SIZE + 0x10] ^= 0x55
    open(path, "wb").write(image)
