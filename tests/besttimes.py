"""The practice best times (practice/src/besttimes.c, build.md "Practice best times"), in Atlantis 1 and 2.

A level is ended the game's own way: newLevExitTimer and exitType make CheckForExits call ExitLevel with the
"complete" flag, as the real end of a level does. The end of level screen then calls CheckForLevelCompleted, the
game's point that updates its own records, and the practice code notes the time there.

1. The Hi Scores screen of a fresh boot shows the game's own table (retail): the times of the five levels in the
   game's form ("m:ss"), and no practice time or dash.
2. Atlantis 1 finished: practiceBests holds the level's in-game time in game frames (the same count as the HUD's
   timer). Atlantis 2 finished: its own place is filled (bits 0 and 1 of the table).
3. A slower run does not replace the time. A faster run does.
4. A run with noclip, and a run with a savestate loaded in the level, are not kept. The next normal run is.
5. The game's own records (bestTimes, in whole seconds, and hiscoreTable) follow the game's rules and the practice
   code never writes them; the EEPROM file holds the game's records and nothing in the practice settings' blocks.
   The file must exist and must hold the game's current best time.
6. From the title, Hi Scores: the world's page shows the practice times with hundredths and a dash for a level
   without one. With "game records" chosen the screen shows the game's table. Pictures.
7. The menu (Settings, the two items before Save file) chooses what the screen shows and clears the times; with none, the screen
   shows the game's table again.
8. Hard difficulty: the game ends a level there without the end of level screen. A run that is slower than the
   game's record (the game goes to the title) and a run that beats it (the game goes to the name screen) are both
   noted.
Go to checkpoint, the menu's Lives and Health, and a random number call or level start seed taint a run. A run on an
Easy file is not tainted by the game's own Locate garibs bit.
Pictures go to build/tests/besttimes/."""
import os

import layout
import trace
from menunav import goto, to_item
from tools import C_ALL, press, wait_for

# BestTimesState (practice/include/besttimes.h)
BT_SHOW_GAME, BT_TAINT, BT_RESULT, BT_BIT, BT_FRAMES, BT_FINISHED = 0x00, 0x01, 0x02, 0x03, 0x04, 0x06
RESULT_NONE, RESULT_NEW, RESULT_SLOWER, RESULT_MODIFIED = 0, 1, 2, 3
BT_STATE, BT_NOCLIP, BT_ACTORS, BT_OTHER = 1 << 6, 1 << 0, 1 << 3, 1 << 7
RUN_WARP_LEVEL, RUN_WARP_DIFFICULTY, RUN_NOCLIP, RUN_CHECKPOINT = 0x04, 0x05, 0x34, 0x06
RUN_RNG_CALLS, RUN_RNG_RESET = 0x2F, 0x30
ACTION_WARP, ACTION_GOTO_CHECKPOINT, ACTION_SAVE_STATE, ACTION_LOAD_STATE = 1, 7, 8, 9
TEXT_STR = 0x08  # Text.str
PLAYER_LEVEL_FRAMES = 0x18
PLAYER_HEALTH = 0x14  # s32
BEST_TIMES_SIZE = 30 * 6  # BestTime: s16 time, char name[3], u8
HISCORE_SIZE = 6 * 8      # HiscoreEntry: s32 score, char name[4]
MODE_LEVEL, MODE_END_OF_LEVEL = 4, 7


def hundredths(frames):
    return frames * 5


def text(frames):
    h = hundredths(frames)
    return f"{h // 6000}:{h // 100 % 60:02d}.{h % 100:02d}"


def run(emu, sym, args):
    gi = sym("gameInfo")
    fc = lambda: emu.r32(sym("frameCount"))  # noqa: E731
    mode = lambda: emu.r8(gi)  # noqa: E731
    level = lambda: emu.r8(gi + 1)  # noqa: E731
    player = sym("player")
    levelFrames = lambda: emu.r32(player + PLAYER_LEVEL_FRAMES)  # noqa: E731
    bt = sym("bestTimesState")
    bests = sym("practiceBests")
    run_ = sym("run")
    requests = sym("requests")
    result = lambda: emu.r8(bt + BT_RESULT)  # noqa: E731
    finished = lambda: emu.r16(bt + BT_FINISHED)  # noqa: E731
    best = lambda bit: emu.r16(bests + 2 * bit)  # noqa: E731
    menu_open = lambda: emu.r8(sym("menuOpen"))  # noqa: E731
    game_records = lambda: bytes(emu.read(sym("bestTimes"), BEST_TIMES_SIZE)) + \
        bytes(emu.read(sym("hiscoreTable"), HISCORE_SIZE))  # noqa: E731

    def shot(name):
        if args.gfx == "angrylion":
            emu.fb_png(name, sym("cfb_16_b"))
            emu.fb_png(name + "_0", sym("cfb_16_a"))

    def request(action):
        emu.w16(requests, emu.r16(requests) | 1 << action)

    def texts(frames=12):
        """The strings the game's text list holds over a few video frames."""
        out = set()
        for _ in range(frames):
            head = sym("textList")
            p = emu.r32(head)
            while p != head and len(out) < 400:
                out.add(emu.read(p + TEXT_STR, 0x40).split(b"\0")[0].decode("latin-1"))
                p = emu.r32(p)
            yield 1
        out.discard("")
        texts.last = out

    def warp_menu(item):
        yield from press(emu, *C_ALL)
        if not menu_open():
            emu.fail("the menu did not open")
        yield from press(emu, "A")
        for b in ["DOWN"] * item + ["A"]:
            yield from press(emu, b)

    def finish(target, expect_level, label):
        """Plays the level to TARGET game frames of the in-game timer, ends it, and waits for the end of level
        screen to note the time. Returns the time of the run."""
        yield from wait_for(emu, lambda: level() == expect_level and mode() == MODE_LEVEL and fc() >= 5, 3000,
                            f"{label} to start")
        yield from wait_for(emu, lambda: levelFrames() >= target, 4000, f"{label} to reach {target} frames")
        before = finished()
        emu.w8(gi + 0x11, trace.LEVEL_EXIT)  # gameInfo.exitType
        emu.w16(sym("newLevExitTimer"), 1)
        yield from wait_for(emu, lambda: finished() != before, 2000, f"{label} to be finished")
        yield from wait_for(emu, lambda: mode() == MODE_END_OF_LEVEL, 600, "the end of level screen")
        t = levelFrames()
        print(f"besttimes: {label}: {t} game frames ({text(t)}), result {result()}")
        if emu.r16(bt + BT_FRAMES) != t:
            emu.fail(f"{label}: lastFrames {emu.r16(bt + BT_FRAMES)}, the timer says {t}")
        return t

    def go_on(next_level):
        """The end of level screen to the next level."""
        for i in range(500):
            if level() == next_level and mode() == MODE_LEVEL and fc() >= 1:
                return
            if i % 30 < 8:
                emu.hold("A")
            else:
                emu.release()
            yield 2
        emu.release()
        emu.fail(f"the end of level screen did not lead to level {next_level}")

    def warp_to(target_level):
        emu.w8(run_ + RUN_WARP_LEVEL, target_level)
        request(ACTION_WARP)
        yield 10
        yield from wait_for(emu, lambda: level() == target_level and mode() == MODE_LEVEL and fc() < 60, 3000,
                            f"the warp to level {target_level}")

    def choose(screen, spot, next_screen):
        """Moves the title screen's cursor to SPOT (the stick, D-pad down), and chooses it with A."""
        cursor = sym("screenList") + 0xEC * screen + 0x1C
        for _ in range(20):
            if emu.r32(cursor) == spot:
                break
            emu.hold(x=0, y=-80)
            yield 8
            emu.release()
            yield 20
        if emu.r32(cursor) != spot:
            emu.fail(f"the cursor of screen {screen} is on {emu.r32(cursor)}, not {spot}")
        yield 20
        # The game takes A when its animation allows; a press is repeated until the screen changes.
        for _ in range(8):
            yield from press(emu, "A", frames=6)
            yield 40
            if emu.r32(sym("nextScreen")) == next_screen or emu.r32(sym("currentScreen")) != screen:
                break
        yield from wait_for(emu, lambda: emu.r32(sym("currentScreen")) == next_screen, 600,
                            f"screen {next_screen} (screen {emu.r32(sym('currentScreen'))}, animation "
                            f"{emu.r32(sym('currentAnim'))}, next {emu.r32(sym('nextScreen'))}, cursor "
                            f"{emu.r32(cursor)}, mode {mode()}, menu {menu_open()})")
        yield 60

    def to_page(n):
        """The Hi Scores screen to page N (-1: the score table, 0 to 5: a world), by the stick."""
        current = lambda: emu.r32s(sym("currentHiscoresLevel"))  # noqa: E731
        for _ in range(60):
            if current() == n:
                break
            emu.hold(x=80 if current() < n else -80, y=0)
            yield 6
            emu.release()
            yield 14
        else:
            emu.fail(f"the Hi Scores screen did not reach page {n} (on {current()})")
        yield 90

    def title_menu_to_hiscores():
        """From the title menu (screen 1): its 4th entry (screen 3), then the 4th entry of that (screen 4)."""
        yield from wait_for(emu, lambda: emu.r32(sym("currentScreen")) == 1 and mode() == 1, 600, "the title menu")
        yield 200
        yield from choose(1, 3, 3)
        yield from choose(3, 3, 4)
        yield from wait_for(emu, lambda: emu.r32s(sym("currentHiscoresLevel")) == -1, 600, "the Hi Scores screen")
        yield 120

    # Settings that would draw over the screens are not needed: the HUD stays as it is.
    yield from wait_for(emu, lambda: level() == trace.PRESENT and fc() >= 200, 4000, "the logos")
    yield from press(emu, "START", frames=6)
    yield from wait_for(emu, lambda: level() == trace.FLYTHRU and mode() == MODE_LEVEL and fc() >= 30, 3000,
                        "the title flythrough")
    if any(best(i) for i in range(30)) or finished():
        emu.fail("practice best times exist before any level was finished")

    # 1. The game's own table on a fresh boot. With no practice time the screen is the game's. The pages show the
    # game's initial times and no hundredths.
    yield from wait_for(emu, lambda: fc() >= 110, 600, "the flythrough to run")
    yield from press(emu, "START", frames=6)
    yield from title_menu_to_hiscores()
    shot("hiscores_fresh_score")
    yield from to_page(0)
    yield from texts()
    fresh = set(texts.last)
    if any("." in s and s[0].isdigit() for s in fresh) or "-:--.--" in fresh:
        emu.fail(f"the fresh Hi Scores page shows practice style times: {sorted(fresh)}")
    # The game's page shows the table's five times as "m:ss".
    game_page = {f"{emu.r16s(sym('bestTimes') + 6 * i) // 60}:{emu.r16s(sym('bestTimes') + 6 * i) % 60:02d}"
                 for i in range(5)}
    if not game_page <= fresh:
        emu.fail(f"the fresh Hi Scores page {sorted(fresh)} lacks the game's times {sorted(game_page)}")
    shot("hiscores_fresh_world")
    print(f"besttimes: the fresh Atlantis page shows {sorted(s for s in fresh if ':' in s)}")
    yield from press(emu, "B")  # back to the title menu
    yield 200

    # Play: Atlantis 1 through the warp.
    yield from warp_menu(2)  # Go
    saved_before = game_records()

    # 2. Atlantis 1 and Atlantis 2 finished.
    t1 = yield from finish(200, trace.ATLANTIS1, "Atlantis 1")
    if result() != RESULT_NEW or best(0) != t1 or emu.r8(bt + BT_BIT) != 0:
        emu.fail(f"Atlantis 1: result {result()}, best {best(0)}, bit {emu.r8(bt + BT_BIT)}, time {t1}")
    # The game's own record: whole seconds, set by the game's own rules.
    game_time = emu.r16s(sym("bestTimes"))
    if game_time != t1 // 20:
        emu.fail(f"the game's own best time is {game_time}, not {t1 // 20} (the game's rule)")
    yield from go_on(trace.ATLANTIS2)
    t2 = yield from finish(140, trace.ATLANTIS2, "Atlantis 2")
    if result() != RESULT_NEW or best(1) != t2 or best(0) != t1 or emu.r8(bt + BT_BIT) != 1:
        emu.fail(f"Atlantis 2: result {result()}, bests {best(0)} {best(1)}, bit {emu.r8(bt + BT_BIT)}")
    if best(2) or best(3):
        emu.fail("a level that was not played has a time")
    yield from go_on(12)  # Atlantis 3

    # 3. A slower run does not replace it; a faster one does.
    yield from warp_to(trace.ATLANTIS1)
    t3 = yield from finish(t1 + 60, trace.ATLANTIS1, "slower Atlantis 1")
    if result() != RESULT_SLOWER or best(0) != t1:
        emu.fail(f"the slower run: result {result()}, best {best(0)} (was {t1})")
    yield from go_on(trace.ATLANTIS2)
    yield from warp_to(trace.ATLANTIS1)
    t4 = yield from finish(t1 - 50, trace.ATLANTIS1, "faster Atlantis 1")
    if result() != RESULT_NEW or best(0) != t4 or t4 >= t1:
        emu.fail(f"the faster run: result {result()}, best {best(0)} (time {t4}, was {t1})")
    yield from go_on(trace.ATLANTIS2)

    # 4. Modified runs: noclip, and a loaded savestate.
    yield from warp_to(trace.ATLANTIS1)
    emu.w8(run_ + RUN_NOCLIP, 1)
    yield 10
    t5 = yield from finish(t4 - 30, trace.ATLANTIS1, "noclip Atlantis 1")
    emu.w8(run_ + RUN_NOCLIP, 0)
    if result() != RESULT_MODIFIED or best(0) != t4:
        emu.fail(f"the noclip run: result {result()}, best {best(0)} (was {t4})")
    if not emu.r8(bt + BT_TAINT) & BT_NOCLIP:
        emu.fail("the noclip run has no noclip reason")
    yield from go_on(trace.ATLANTIS2)
    yield from warp_to(trace.ATLANTIS1)
    if emu.r8(bt + BT_TAINT):
        emu.fail(f"a new level starts with reasons {emu.r8(bt + BT_TAINT)}")
    yield from wait_for(emu, lambda: levelFrames() >= 30, 3000, "30 frames")
    request(ACTION_SAVE_STATE)
    yield 20
    yield from wait_for(emu, lambda: levelFrames() >= 60, 3000, "60 frames")
    request(ACTION_LOAD_STATE)
    yield from wait_for(emu, lambda: emu.r8(bt + BT_TAINT) & BT_STATE, 200, "the state load to note itself")
    t6 = yield from finish(t4 - 30, trace.ATLANTIS1, "state-load Atlantis 1")
    if result() != RESULT_MODIFIED or best(0) != t4:
        emu.fail(f"the run with a loaded state: result {result()}, best {best(0)} (was {t4})")
    yield from go_on(trace.ATLANTIS2)

    # Go to checkpoint moves Glover; Lives and Health from the Game menu change the run.
    yield from warp_to(trace.ATLANTIS1)
    yield from wait_for(emu, lambda: fc() >= 40, 600, "the level to run")
    if emu.r8(bt + BT_TAINT):
        emu.fail(f"a new level starts with reasons {emu.r8(bt + BT_TAINT)} (before the checkpoint)")
    emu.w8(run_ + RUN_CHECKPOINT, 0)
    request(ACTION_GOTO_CHECKPOINT)
    yield from wait_for(emu, lambda: emu.r8(bt + BT_TAINT) & BT_ACTORS, 200, "Go to checkpoint to note itself")
    yield from wait_for(emu, lambda: fc() >= 80, 600, "the level to run on")  # warp_to waits for a young level
    yield from warp_to(trace.ATLANTIS1)
    yield 30
    if emu.r8(bt + BT_TAINT):
        emu.fail(f"a new level starts with reasons {emu.r8(bt + BT_TAINT)} (after the checkpoint)")

    def game_menu_change(item, button):
        """Opens the menu, chooses Game, moves to entry ITEM (0: Cheats), presses BUTTON and closes the menu."""
        yield from press(emu, *C_ALL)
        for b in ["DOWN"] * 4 + ["A"] + ["DOWN"] * item + [button]:
            yield from press(emu, b)
        yield from press(emu, "B")
        for b in ["UP"] * 4:  # the root menu is on Warp again
            yield from press(emu, b)
        yield from press(emu, *C_ALL)
        if menu_open():
            emu.fail("the menu did not close")

    yield from game_menu_change(2, "LEFT")  # Lives
    if not emu.r8(bt + BT_TAINT) & BT_OTHER:
        emu.fail("a changed number of lives did not taint the run")
    # warp_to can return before the warp has started the level; the new level clears the reasons.
    yield from warp_to(trace.ATLANTIS1)
    yield from wait_for(emu, lambda: emu.r8(bt + BT_TAINT) == 0 and level() == trace.ATLANTIS1 and
                        mode() == MODE_LEVEL and fc() >= 5, 3000, "the new level to clear the reasons")
    health = emu.r32(player + PLAYER_HEALTH)
    yield from game_menu_change(3, "LEFT")  # Health
    if emu.r32(player + PLAYER_HEALTH) == health or not emu.r8(bt + BT_TAINT) & BT_OTHER:
        emu.fail(f"a changed health ({health} to {emu.r32(player + PLAYER_HEALTH)}) did not taint the run")
    yield from warp_to(trace.ATLANTIS1)
    t7 = yield from finish(t4 - 20, trace.ATLANTIS1, "a normal run after the modified ones")
    if result() != RESULT_NEW or best(0) != t7:
        emu.fail(f"the normal run after the modified ones: result {result()}, best {best(0)} (time {t7})")
    yield from go_on(trace.ATLANTIS2)

    # An Easy file: StartNewLevel sets cheat bit 0 (Locate garibs) itself, and the run is still kept.
    # A warp inside a level keeps the file in play (the warp's own file, 6), so its difficulty is set directly.
    diff_at = sym("saveSlot") + 0x1C * emu.r16(sym("currentSaveSlot")) + 0x11
    file_difficulty = emu.r8(diff_at)
    emu.w8(diff_at, 0)
    yield from warp_to(trace.ATLANTIS1)
    yield from wait_for(emu, lambda: fc() >= 40, 600, "the Easy level to run")
    if not emu.r32(sym("cheatActivated")) & 1:
        emu.fail("the game did not set Locate garibs on an Easy file")
    if emu.r8(bt + BT_TAINT):
        emu.fail(f"an Easy level has reasons {emu.r8(bt + BT_TAINT)} with no tool used")
    t8 = yield from finish(t7 + 40, trace.ATLANTIS1, "Easy Atlantis 1")
    if result() != RESULT_SLOWER or best(0) != t7:
        emu.fail(f"the Easy run: result {result()}, best {best(0)} (was {t7}, time {t8})")
    yield from go_on(trace.ATLANTIS2)
    # One call of the random numbers, or the level start's seed, changes the run.
    for field in (RUN_RNG_CALLS, RUN_RNG_RESET):
        yield from warp_to(trace.ATLANTIS1)
        yield from wait_for(emu, lambda: fc() >= 40, 600, "the level to run")
        if emu.r8(bt + BT_TAINT):
            emu.fail(f"a new level starts with reasons {emu.r8(bt + BT_TAINT)} (before the random numbers)")
        emu.w8(run_ + field, 1)
        yield from wait_for(emu, lambda: emu.r8(bt + BT_TAINT) & BT_OTHER, 200, "the random numbers to taint the run")
        yield from wait_for(emu, lambda: fc() >= 80, 600, "the level to run on")  # warp_to waits for a young level
    emu.w8(diff_at, file_difficulty)
    yield from warp_to(trace.ATLANTIS2)

    # 5. The game's records follow the game's rules. The EEPROM file holds them; the practice settings' blocks are
    # untouched. mupen64plus writes the file as the game writes the EEPROM.
    game_time = emu.r16s(sym("bestTimes"))
    if game_time != t7 // 20:
        emu.fail(f"the game's own best time is {game_time}, not {t7 // 20}")
    if game_records()[BEST_TIMES_SIZE:] != saved_before[BEST_TIMES_SIZE:]:
        emu.fail("the game's hiscore table changed")
    yield 200
    eep = [f for f in os.listdir(os.path.join(args.out, "save")) if f.endswith(".eep")]
    if not eep:
        emu.fail("the emulator wrote no EEPROM file")
    data = open(os.path.join(args.out, "save", eep[0]), "rb").read()
    if len(data) < 512:
        emu.fail(f"the EEPROM file has {len(data)} bytes")
    block = lambda n: data[8 * n:8 * n + 8]  # noqa: E731
    if any(b != 0xFF for b in data[8 * 56:8 * 64]):
        emu.fail("the practice settings' EEPROM blocks (56 to 63) are not erased")
    table = b"".join(block(7 + i) for i in range(24))
    if int.from_bytes(table[:2], "big") != game_time:
        emu.fail(f"the EEPROM file's first best time is {int.from_bytes(table[:2], 'big')} s, the game's is {game_time}")
    print(f"besttimes: EEPROM file {len(data)} bytes, blocks 56 to 63 erased, game's best time {game_time} s")

    # 6. The Hi Scores screen from the title.
    yield from warp_menu(8)  # the early title menu
    yield from wait_for(emu, lambda: emu.r8(sym("earlyTitle")) and fc() > 40, 1500, "the early title")
    yield from warp_menu(9)  # back to the normal title
    yield from wait_for(emu, lambda: level() == trace.FLYTHRU and mode() == 1, 600, "the normal title")
    records = game_records()
    yield from title_menu_to_hiscores()
    shot("hiscores_score")
    yield from to_page(0)
    yield from texts()
    page = set(texts.last)
    shot("hiscores_practice")
    print(f"besttimes: the Atlantis page shows {sorted(s for s in page if ':' in s)}")
    for want in (text(t7), text(t2)):
        if want not in page:
            emu.fail(f"the Hi Scores page does not show {want}: {sorted(page)}")
    if "-:--.--" not in page:
        emu.fail("a level without a time has no dash on the Hi Scores page")
    if game_records() != records:
        emu.fail("showing the practice times changed the game's records")
    # The next page (Carnival): no level has a time.
    yield from to_page(1)
    yield from texts()
    if "-:--.--" not in texts.last or any(s[0].isdigit() and "." in s for s in texts.last):
        emu.fail(f"the Carnival page: {sorted(texts.last)}")
    shot("hiscores_practice_carnival")
    yield from to_page(0)

    # The setting, from the menu: Settings, "Hi Scores screen shows".
    yield from press(emu, *C_ALL)
    if not menu_open():
        emu.fail("the menu did not open on the Hi Scores screen")
    yield from goto(emu, sym, "Settings")
    yield from to_item(emu, sym, "Hi Scores screen shows")
    yield from press(emu, "RIGHT")  # game records
    if emu.r8(bt + BT_SHOW_GAME) != 1:
        emu.fail(f"the menu did not choose the game's records ({emu.r8(bt + BT_SHOW_GAME)})")
    yield from press(emu, *C_ALL)
    yield 60
    yield from texts()
    page = set(texts.last)
    shot("hiscores_game")
    if text(t7) in page or any(s[0].isdigit() and "." in s for s in page):
        emu.fail(f"with the game's records chosen the page shows practice times: {sorted(page)}")
    print(f"besttimes: with the game's records the page shows {sorted(s for s in page if ':' in s)}")

    # 7. Clear: with no time left, the screen is the game's.
    yield from press(emu, *C_ALL)
    yield from goto(emu, sym, "Settings")
    yield from to_item(emu, sym, "Hi Scores screen shows")
    yield from press(emu, "LEFT")  # practice bests
    yield from goto(emu, sym, "Clear practice best times")
    if any(best(i) for i in range(30)):
        emu.fail("Clear practice best times left a time")
    yield from press(emu, *C_ALL)
    yield 60
    yield from texts()
    page = set(texts.last)
    shot("hiscores_cleared")
    if any(s[0].isdigit() and "." in s for s in page) or "-:--.--" in page:
        emu.fail(f"after the clear the page is not the game's: {sorted(page)}")
    if game_records() != records:
        emu.fail("the clear changed the game's records")
    print("besttimes: the clear left the game's records as they were")

    # 8. Hard difficulty. The Warp menu starts Atlantis 1 from the title on Hard. The game does not show the end of
    # level screen there: a run that does not beat the game's record goes back to the title, one that beats it goes
    # to the name screen (currentScreen 5). The practice code notes both.
    emu.w8(run_ + RUN_WARP_LEVEL, trace.ATLANTIS1)
    emu.w8(run_ + RUN_WARP_DIFFICULTY, 2)
    best_first = best(0)

    def hard(label, game_record, target, menu_ups):
        yield from press(emu, *C_ALL)
        if not menu_open():
            emu.fail(f"{label}: the menu did not open")
        for b in ["UP"] * menu_ups + ["A"] + ["DOWN"] * 2 + ["A"]:  # Warp, Go
            yield from press(emu, b)
        yield from wait_for(emu, lambda: level() == trace.ATLANTIS1 and mode() == MODE_LEVEL and fc() >= 5, 3000,
                            f"{label} to start")
        slot = sym("saveSlot") + 0x1C * emu.r16s(sym("currentSaveSlot"))
        if emu.r8(slot + 0x11) != 2:
            yield 60
        yield from wait_for(emu, lambda: levelFrames() >= target, 4000, f"{label} to reach {target} frames")
        emu.w16(sym("bestTimes"), game_record)
        before = finished()
        emu.w8(gi + 0x11, trace.LEVEL_EXIT)
        emu.w16(sym("newLevExitTimer"), 1)
        yield from wait_for(emu, lambda: finished() != before, 2000, f"{label} to be noted")
        frames = emu.r16(bt + BT_FRAMES)
        print(f"besttimes: {label}: {frames} game frames, result {result()}")
        if result() != RESULT_NEW or emu.r8(bt + BT_BIT) != 0 or best(0) != frames or frames < target:
            emu.fail(f"{label}: result {result()}, bit {emu.r8(bt + BT_BIT)}, best {best(0)}, time {frames}")
        return frames

    # Slower than the game's record (0 s): the game goes back to the title.
    # Glover dies at about 100 frames when left alone, and a death ends a Hard run.
    h1 = yield from hard("Hard run, no record", 0, 60, 8)
    # The game leaves a Hard run for the Time Trial screen (level 1, mode 15).
    yield from wait_for(emu, lambda: level() == 1 and mode() == 15, 3000,
                        "the title after a Hard run without a record")
    if best(0) != h1:
        emu.fail(f"the practice best is {best(0)} after the first Hard run, not {h1}")
    # Faster than the game's record (9999 s): the game goes to the name screen.
    # The Hard run leaves for the Time Trial screen (level 1, mode 15). B goes back to the title.
    yield 300
    yield from press(emu, "B", frames=6)
    yield from wait_for(emu, lambda: mode() == 1 and fc() >= 30, 3000, "the title menu")
    h2 = yield from hard("Hard run, record", 9999, h1 - 30, 0)
    yield from wait_for(emu, lambda: emu.r32(sym("currentScreen")) == 5 and mode() == 1, 3000,
                        "the name screen after a Hard record")
    if h2 >= h1:
        emu.fail(f"the second Hard run ({h2}) is not faster than the first ({h1})")
    print(f"besttimes: Hard runs noted: {h1} and {h2} game frames (was {best_first})")
