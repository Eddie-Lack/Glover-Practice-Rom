"""The ghost of a recorded run, in Atlantis 1 (practice/src/ghost.c, build.md "The ghost").

1. Record 150 game frames of the route's random input with the ghost switch off. The ghost keeps the glove's and
   the ball's place for each game frame, at the index frameCount - start, and those places match the ones the test
   saw in the live run.
2. With the switch off, loading the state draws nothing: the draw count stays still, no ghost list is in the game's
   display list buffers.
3. The switch on (the Game menu's second last item), the state loaded and the glove held still: the ghost is drawn on
   every game frame at the stored place of that game frame (the index is frameCount - start), the display list
   holds the ghost's commands and the game's list calls it, and it moves away from the still glove. Pictures.
4. The recording played back with the switch on: the live glove follows the recorded run, and the ghost lies on the
   glove (in step to within a fraction of a game frame's move). Pictures.
5. Past the end of the ghost nothing is drawn. The switch off again: nothing is drawn.
6. A second recording replaces the ghost of the level (one ghost for each level).
Pictures go to build/tests/ghost/."""
import struct

import layout
import trace
from tools import C_ALL, press, wait_for

# Ghost (practice/include/ghost.h)
G_SLOTS, G_WRITING, G_STORED = 0x00, 0x30, 0x3C
G_DRAWS, G_DRAW_INDEX, G_DRAWN_HAND, G_DRAWN_BALL, G_DRAWN_YAW, G_DRAWN_FLAGS = 0x40, 0x44, 0x48, 0x4E, 0x54, 0x56
G_LIST, G_COMMANDS, G_DRAW_FRAME, G_LIVE_HAND, G_LIVE_BALL = 0x58, 0x5C, 0x60, 0x64, 0x70
SLOT_SIZE = 12  # used, level, count (u16), start, stamp
SLOTS = 4
GHOST_BUFFER, SLOT_BYTES = 0x8075C000, 0x20000
RUN_GHOST = 0x47
DYNAMIC_SIZE = 0x2B230
ACTION_LOAD_STATE, ACTION_RECORD, ACTION_PLAY = 9, 10, 11
STATE_SLOT_RECORD = 10  # the pool slot of the recording's own state (practice/include/savestate.h)
RUN = 150


def run(emu, sym, args):
    gi = sym("gameInfo")
    fc_addr = sym("frameCount")
    hand, ball = sym("hand"), sym("ball")
    requests = sym("requests")
    rec = sym("recording")
    run_ = sym("run")
    g = sym("ghost")
    mode = lambda: emu.r8(gi)  # noqa: E731
    level = lambda: emu.r8(gi + 1)  # noqa: E731
    fc = lambda: emu.r32(fc_addr)  # noqa: E731
    inputs = trace.level_input()
    keys = sorted(inputs)
    s16 = lambda a: struct.unpack(">h", bytes(emu.read(a, 2)))[0]  # noqa: E731
    triple = lambda a: tuple(s16(a + 2 * i) for i in range(3))  # noqa: E731
    live = lambda actor: tuple(emu.rf(actor + 0x34 + 4 * i) for i in range(3))  # noqa: E731
    draws = lambda: emu.r32(g + G_DRAWS)  # noqa: E731
    # The game frame of the recording's state: the ghost must start on exactly this game frame. (A poll of frameCount
    # after the recording flag is set can be a game frame late, as the game goes on from the save.)
    saved_frame = lambda: emu.r32s(sym("stateSavedFrame"))  # noqa: E731

    def request(action):
        emu.w16(requests, emu.r16(requests) | 1 << action)

    def slot(i):
        a = g + G_SLOTS + SLOT_SIZE * i
        return {"used": emu.r8(a), "level": emu.r8(a + 1), "count": emu.r16(a + 2), "start": emu.r32s(a + 4),
                "stamp": emu.r32(a + 8)}

    def stored(i, index):
        a = GHOST_BUFFER + SLOT_BYTES * i + 16 * index
        return {"hand": triple(a), "ball": triple(a + 6), "yaw": emu.r16(a + 12), "flags": emu.r8(a + 14)}

    def current():
        """The slot of the finished ghost of this level, or None."""
        found = [i for i in range(SLOTS) if slot(i)["used"] and slot(i)["level"] == level()]
        return found[0] if found else None

    def distance(a, b):
        return sum((x - y) ** 2 for x, y in zip(a, b)) ** 0.5

    def in_game_list():
        """1 if either display list buffer calls the ghost's list (G_DL to its physical address)."""
        want = struct.pack(">II", 0x06000000, emu.r32(g + G_LIST) & 0x1FFFFFFF)
        return any(want in bytes(emu.read(sym("dynamic") + b * DYNAMIC_SIZE, DYNAMIC_SIZE)) for b in (0, 1))

    def picture(name):
        """The frame buffer that holds a drawn picture: the one with more colours (the other is still being drawn)."""
        colours = [len(set(bytes(emu.read(sym(("cfb_16_a", "cfb_16_b")[b]), 320 * 240 * 2))[::2])) for b in (0, 1)]
        emu.fb_png("ghost_" + name, sym(("cfb_16_a", "cfb_16_b")[colours.index(max(colours))]))

    def ghost_menu():
        """The Game menu's second last item, Ghost, A. The menu opens on the item where it was left."""
        yield from press(emu, *C_ALL)
        if not emu.r8(sym("menuOpen")):
            emu.fail("the menu did not open")
        for b in (() if menu_seen[0] else ("DOWN",) * 4) + ("A", "UP", "UP", "A", "B"):
            yield from press(emu, b)
        menu_seen[0] = True
        yield from press(emu, *C_ALL)
        yield 10

    menu_seen = [False]

    def record_run(until, held):
        """Samples the glove and the ball on each video frame up to game frame UNTIL, by game frame; with HELD,
        holds the route's input."""
        seen, last = {}, None
        for _ in range(RUN * 12):
            f = fc()
            if f >= until:
                return seen
            if held:
                k = max([k for k in keys if k <= f % 600] or [keys[0]])
                if inputs[k] != last:
                    emu.hold(*inputs[k][0], x=inputs[k][1], y=inputs[k][2])
                    last = inputs[k]
            seen.setdefault(f, set()).add((tuple(round(v) for v in live(hand)), tuple(round(v) for v in live(ball))))
            yield 1
        emu.fail(f"the level did not reach game frame {until}")

    for i in layout.SETTINGS_BINDINGS:
        emu.w8(sym("settings") + i, 0)
    yield from wait_for(emu, lambda: level() == trace.PRESENT and fc() >= 200, 4000, "the logos")
    yield from press(emu, "START", frames=6)
    yield from wait_for(emu, lambda: level() == trace.FLYTHRU and mode() == 4 and fc() >= 30, 3000, "the title")
    yield from press(emu, *C_ALL)
    for b in ("A", "DOWN", "DOWN", "A"):  # Warp, Go
        yield from press(emu, b)
    yield from wait_for(emu, lambda: level() == trace.ATLANTIS1 and mode() == 4 and fc() >= 100, 3000, "Atlantis 1")
    emu.w8(sym("settings") + layout.SETTINGS_HUD, 0)  # the timer off
    emu.w8(sym("stateSlot"), STATE_SLOT_RECORD)  # Load state loads the recording's state
    if emu.r8(run_ + RUN_GHOST) or draws():
        emu.fail("the ghost is on, or has drawn, at the start")

    # 1. Record with the switch off.
    request(ACTION_RECORD)
    yield from wait_for(emu, lambda: emu.r8(rec), 100, "the recording to start")
    start = saved_frame()
    seen = yield from record_run(start + RUN, True)
    request(ACTION_RECORD)
    yield from wait_for(emu, lambda: not emu.r8(rec), 100, "the recording to stop")
    emu.release()
    yield 30
    i = current()
    if i is None:
        emu.fail("no finished ghost after the recording")
    s = slot(i)
    print(f"ghost: slot {i}, level {s['level']}, {s['count']} game frames from game frame {s['start']}, "
          f"{emu.r32(g + G_STORED)} stored")
    if emu.r8(g + G_WRITING) != 0xFF:
        emu.fail("the ghost is still being written after the recording ended")
    if s["start"] != start or not RUN - 5 <= s["count"] <= RUN + 15:
        emu.fail(f"the ghost has {s['count']} game frames from {s['start']}; the recording's state is from game frame {start}")
    bad, checked = [], 0
    for index in range(s["count"]):
        f = stored(i, index)
        if not f["flags"] & 7 == 7:
            bad.append((index, "flags", f["flags"]))
            continue
        places = seen.get(s["start"] + index)
        if not places:
            continue
        checked += 1
        if not any(distance(f["hand"], p[0]) <= 1.8 and distance(f["ball"], p[1]) <= 1.8 for p in places):
            bad.append((index, f["hand"], f["ball"], sorted(places)[0]))
    print(f"ghost: {checked} game frames checked against the live run, {len(bad)} differ")
    if checked < RUN - 10 or bad:
        emu.fail(f"stored places do not match the run: {checked} checked, first difference {bad[:1]}")
    yaws = {stored(i, index)["yaw"] for index in range(s["count"])}
    if len(yaws) < 3:
        emu.fail(f"the glove's heading never changed in the ghost ({sorted(yaws)})")

    # 2. Switch off: loading the state draws nothing.
    request(ACTION_LOAD_STATE)
    yield 120
    if draws() or emu.r32(g + G_COMMANDS):
        emu.fail("a ghost was drawn with the switch off")
    if emu.r32(g + G_LIST) != 0:
        emu.fail("a ghost list exists with the switch off")
    print("ghost: nothing drawn with the switch off")

    # 3. Switch on; the state loaded; the glove still. The ghost runs from the state's game frame.
    yield from ghost_menu()
    if emu.r8(run_ + RUN_GHOST) != 1:
        emu.fail("the Ghost item did not turn the switch on")
    request(ACTION_LOAD_STATE)
    yield from wait_for(emu, lambda: fc() <= s["start"] + 2, 100, "the state to load")
    samples = []
    last = draws()
    still = live(hand)
    shot = {8: False, 16: False, 24: False}
    for _ in range(RUN * 8):
        yield 1
        if draws() == last:
            continue
        last = draws()
        index = emu.r32s(g + G_DRAW_INDEX)
        samples.append({
            "index": index, "frame": emu.r32s(g + G_DRAW_FRAME), "hand": triple(g + G_DRAWN_HAND),
            "ball": triple(g + G_DRAWN_BALL), "live": live(hand), "commands": emu.r32(g + G_COMMANDS)})
        for k in shot:
            if index >= k and not shot[k]:
                shot[k] = True
                picture(f"still_{k}")
        if index >= s["count"] - 3:
            break
    print(f"ghost: {len(samples)} frames drawn from index {samples[0]['index']} to {samples[-1]['index']}")
    if len(samples) < 60 or samples[-1]["index"] < s["count"] - 6:
        emu.fail(f"only {len(samples)} frames were drawn, up to index {samples[-1]['index'] if samples else None}")
    off_by = [x for x in samples if abs(x["index"] - (x["frame"] - s["start"])) > 1]
    if len(off_by) > 3:
        emu.fail(f"the ghost's game frame is not frameCount - start on {len(off_by)} frames: {off_by[0]}")
    wrong = [x for x in samples if x["hand"] != stored(i, x["index"])["hand"] or x["ball"] != stored(i, x["index"])["ball"]]
    if wrong:
        emu.fail(f"the drawn places differ from the stored ones at index {wrong[0]['index']}: {wrong[0]}")
    indexes = [x["index"] for x in samples]
    if any(b < a for a, b in zip(indexes, indexes[1:])) or max(b - a for a, b in zip(indexes, indexes[1:])) > 4:
        emu.fail(f"the ghost's game frames do not advance steadily: {indexes}")
    apart = max(distance(x["hand"], x["live"]) for x in samples)
    print(f"ghost: the ghost glove got {apart:.0f} units from the still glove")
    if apart < 40 or distance(still, samples[-1]["live"]) > 30:
        emu.fail(f"the ghost did not run apart from the still glove ({apart:.0f}), or the glove moved")
    # The list: one frame's commands.
    commands = samples[-1]["commands"]
    listing = emu.r32(g + G_LIST)
    words = struct.unpack(f">{commands * 2}I", bytes(emu.read(listing, commands * 8)))
    ops = [words[2 * k] >> 24 for k in range(commands)]
    print(f"ghost: the list holds {commands} commands: {ops.count(0x01)} matrices, {ops.count(0x04)} vertex loads, "
          f"{ops.count(0xB1)} triangle pairs")
    if ops[0] != 0xE7 or ops[-1] != 0xB8 or ops.count(0x04) != 2 or ops.count(0x01) != 4 or ops.count(0xB1) < 20:
        emu.fail(f"the ghost's display list is not as expected: {[hex(o) for o in ops]}")
    if not in_game_list():
        emu.fail("the game's display list does not call the ghost's list")

    # 4. Playback with the switch on: the ghost lies on the glove.
    request(ACTION_PLAY)
    yield from wait_for(emu, lambda: emu.r8(rec + 1), 100, "the playback to start")
    yield from wait_for(emu, lambda: fc() <= s["start"] + 2, 100, "the state to load")
    samples, last = [], draws()
    shot = {40: False, 110: False}
    for _ in range(RUN * 8):
        yield 1
        if draws() == last:
            continue
        last = draws()
        index = emu.r32s(g + G_DRAW_INDEX)
        samples.append({"index": index, "hand": triple(g + G_DRAWN_HAND), "ball": triple(g + G_DRAWN_BALL),
                        "live": tuple(emu.rf(g + G_LIVE_HAND + 4 * k) for k in range(3)),
                        "liveball": tuple(emu.rf(g + G_LIVE_BALL + 4 * k) for k in range(3)),
                        "frame": emu.r32s(g + G_DRAW_FRAME)})
        for k in shot:
            if index >= k and not shot[k]:
                shot[k] = True
                picture(f"playback_{k}")
        if not emu.r8(rec + 1):
            break
    # The places the draw thread saw in the live run, at the time of each draw.
    gaps = [distance(x["hand"], x["live"]) for x in samples]
    ball_gaps = [distance(x["ball"], x["liveball"]) for x in samples]
    moves = [distance(a["hand"], b["hand"]) for a, b in zip(samples, samples[1:]) if b["index"] != a["index"]]
    for k in (-2, -1, 0, 1, 2):
        mean = sum(distance(stored(i, x["index"] + k)["hand"], x["live"]) for x in samples[5:-5]) / len(samples[5:-5])
        print(f"ghost: playback, mean gap of the glove to the ghost's index {k:+d}: {mean:.1f}")
    gaps.sort()
    print(f"ghost: playback, {len(samples)} frames; glove gap median {gaps[len(gaps) // 2]:.1f}, "
          f"max {gaps[-1]:.1f}; ball gap max {max(ball_gaps):.1f}; the glove moves {sum(moves) / len(moves):.1f} "
          f"units a draw")
    if len(samples) < 60:
        emu.fail(f"only {len(samples)} frames were drawn in the playback")
    if gaps[-1] > 1.8 or max(ball_gaps) > 1.8:
        emu.fail("the ghost does not lie on the glove in the playback")

    # 5. Past the end nothing is drawn; switched off, nothing is drawn.
    yield from wait_for(emu, lambda: not emu.r8(rec + 1), 400, "the playback to end")
    yield 60
    last = draws()
    yield 60
    if draws() != last:
        emu.fail("the ghost is drawn past the end of the recording")
    picture("after_end")
    yield from ghost_menu()
    if emu.r8(run_ + RUN_GHOST):
        emu.fail("the Ghost item did not turn the switch off")
    request(ACTION_LOAD_STATE)
    yield 30
    last = draws()
    yield 90
    if draws() != last:
        emu.fail("the ghost is drawn with the switch off again")
    print("ghost: nothing drawn past the end, nothing drawn with the switch off again")

    # 6. A second recording replaces the level's ghost.
    old = slot(i)
    request(ACTION_RECORD)
    yield from wait_for(emu, lambda: emu.r8(rec), 100, "the second recording to start")
    mid = saved_frame()
    yield from record_run(mid + 40, False)
    request(ACTION_RECORD)
    yield from wait_for(emu, lambda: not emu.r8(rec), 100, "the second recording to stop")
    yield 30
    used = [j for j in range(SLOTS) if slot(j)["used"] and slot(j)["level"] == level()]
    if len(used) != 1:
        emu.fail(f"{len(used)} ghosts for the level after a second recording")
    n = slot(used[0])
    print(f"ghost: second recording: slot {used[0]}, {n['count']} game frames from {n['start']} "
          f"(first: slot {i}, {old['count']} from {old['start']})")
    if n["start"] != mid or not 30 <= n["count"] <= 60 or n["stamp"] <= old["stamp"]:
        emu.fail(f"the second ghost is wrong (the recording's state is from game frame {mid}): {n}")

    # 7. A level restart during a recording ends the ghost recording: the places so far stay, and the game frames
    # that count again from 0 do not write over them.
    request(ACTION_RECORD)
    yield from wait_for(emu, lambda: emu.r8(rec), 100, "the third recording to start")
    mid = saved_frame()
    yield from record_run(mid + 40, False)
    request(0)  # ACTION_RESET
    yield from wait_for(emu, lambda: fc() < mid, 200, "the level to restart")
    yield 60
    if emu.r8(g + G_WRITING) != 0xFF:
        emu.fail("the ghost recording went on after the level restarted")
    used = [j for j in range(SLOTS) if slot(j)["used"] and slot(j)["level"] == level()]
    n = slot(used[0]) if len(used) == 1 else None
    if n is None or n["start"] != mid or not 30 <= n["count"] <= 60:
        emu.fail(f"the ghost after a restart is wrong (the recording's state is from game frame {mid}): {n}")
    print(f"ghost: a restart ended the ghost recording with {n['count']} game frames kept")
