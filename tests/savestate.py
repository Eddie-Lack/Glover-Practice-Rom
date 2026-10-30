"""Savestates give the same game again.

In Atlantis 1, with input chosen by the game frame as in tests/trace.py, the state is saved on game frame SAVE_AT.
The game runs on to game frame SAVE_AT + RUN and is recorded. Then the state is loaded: the level goes back to
game frame SAVE_AT, and with the same input it must give the same state on every game frame up to SAVE_AT + RUN.
A second load, from a later place, must leave the game in the level. Last, a warp to a level with another song
and a load from there: the game is back in Atlantis 1 with Atlantis 1's song."""
import bisect

import layout
import ramcheck
import trace
from tools import C_ALL, press, wait_for

SAVE_AT = 149  # asked on game frame 149, saved on game frame 150: an input change (trace.level_input, seed 1)
SAVED_FRAME = 150
RUN = 200
ACTION_SAVE_STATE = 8
ACTION_LOAD_STATE = 9
ACTION_WARP = 1
RUN_WARP_LEVEL = 4      # Run.warpLevel (practice/include/actions.h)
LEVEL_INFO_SIZE = 0x38  # LevelInfo, with the song at 0x35
AUDIO_CTRL_CURRENT = 0x18


def run(emu, sym, args):
    gi = sym("gameInfo")
    fc_addr = sym("frameCount")
    hand, ball, player = sym("hand"), sym("ball"), sym("player")
    requests = sym("requests")
    saved = sym("stateSaved")
    mode = lambda: emu.r8(gi)  # noqa: E731
    level = lambda: emu.r8(gi + 1)  # noqa: E731
    fc = lambda: emu.r32(fc_addr)  # noqa: E731
    inputs = trace.level_input()
    keys = sorted(inputs)
    masked = ramcheck.masked_ranges(sym)
    holding = {"held": None}  # the input that play() holds now

    def request(action):
        emu.w16(requests, emu.r16(requests) | 1 << action)

    def input_for(f):
        i = bisect.bisect_right(keys, f) - 1
        return inputs[keys[i]] if i >= 0 else ([], 0, 0)

    def state():
        s = [emu.r32(sym("rseed")), emu.r32(player + 0x18)]
        for actor in (hand, ball):
            s += [emu.r32(actor + o) for o in (0x34, 0x38, 0x3C, 0x58, 0x5C, 0x60)]
        return tuple(s)

    def play(until, record, ram_at):
        """Plays to game frame UNTIL with the input of each game frame. Returns {game frame: {states}} and the
        RAM at game frame RAM_AT."""
        seen, ram = {}, None
        held = None
        holding["held"] = None
        for _ in range(RUN * 10):
            f = fc()
            if f >= until:
                return seen, ram
            if mode() != 4 or level() != trace.ATLANTIS1:
                emu.fail(f"the game left Atlantis 1 on game frame {f} (mode {mode()}, level {level()})")
            want = input_for(f)
            if want != held:
                emu.hold(*want[0], x=want[1], y=want[2])
                held = want
                holding["held"] = want
            if f > record:
                seen.setdefault(f, set()).add(state())
            if f == ram_at and ram is None:
                ram = ramcheck.snapshot(emu, masked)
            yield 1
        emu.fail(f"the level did not reach game frame {until}")

    # The random input would fire the default bindings (tests/trace.py).
    for i in layout.SETTINGS_BINDINGS:
        emu.w8(sym("settings") + i, 0)
    # Atlantis 1 through the Warp menu, as in tests/tools.py.
    yield from wait_for(emu, lambda: level() == trace.PRESENT and fc() >= 200, 4000, "the logos")
    yield from press(emu, "START", frames=6)
    yield from wait_for(emu, lambda: level() == trace.FLYTHRU and mode() == 4 and fc() >= 30, 3000,
                        "the title flythrough")
    request(ACTION_LOAD_STATE)  # with no state yet: nothing happens
    yield 10
    request(ACTION_SAVE_STATE)  # on a title: refused
    yield 10
    if emu.r8(saved):
        emu.fail("a state was saved on the title")
    yield from press(emu, *C_ALL)
    for b in ("A", "DOWN", "DOWN", "A"):  # Warp, Go
        yield from press(emu, b)
    yield from wait_for(emu, lambda: level() == trace.ATLANTIS1 and mode() == 4 and 0 < fc() < 20, 3000,
                        "Atlantis 1")

    # Save on SAVE_AT + 1, then run on.
    yield from play(SAVE_AT, SAVE_AT, -1)
    at_request = holding["held"]  # the input that the controller thread has when the save is asked for
    request(ACTION_SAVE_STATE)
    first, ram1 = yield from play(SAVE_AT + RUN, SAVE_AT, SAVE_AT + RUN // 2)
    if not emu.r8(saved):
        emu.fail("the state was not saved")
    saved_frame = emu.r32(sym("stateSavedFrame"))
    print(f"savestate: asked on game frame {SAVE_AT}, saved on game frame {saved_frame}")
    if saved_frame != SAVED_FRAME or SAVED_FRAME not in keys:
        emu.fail(f"the state was saved on game frame {saved_frame}, not on the input change at {SAVED_FRAME}")

    # Load: back to the saved game frame, and the same game again. The input held when the load is asked for is the
    # one that was held when the save was asked for.
    emu.hold(*at_request[0], x=at_request[1], y=at_request[2])
    request(ACTION_LOAD_STATE)
    yield from wait_for(emu, lambda: fc() <= SAVED_FRAME + 1, 200, "the load")
    second, ram2 = yield from play(SAVE_AT + RUN, SAVE_AT, SAVE_AT + RUN // 2)

    bad = [f for f in sorted(first) if f in second and not first[f] & second[f]]
    missing = [f for f in first if f not in second]
    print(f"savestate: {len(first)} game frames compared, {len(bad)} differ, {len(missing)} missing")
    if bad:
        f = bad[0]
        emu.fail(f"after the load, game frame {f} differs: {sorted(first[f])[0]} and {sorted(second[f])[0]}")
    if len(missing) > 2:
        emu.fail(f"after the load, game frames {missing[:5]} were not seen")
    diff, small = ramcheck.differences(sym.labels, ram1, ram2)
    diff.update(small)
    print(f"savestate: RAM at game frame {SAVE_AT + RUN // 2}: {sum(diff.values())} words differ "
          f"{sorted(diff.items(), key=lambda x: -x[1])[:8]}")

    # A second load, from a later place, then a save on the title and a load from it in the level.
    emu.release()
    yield 100
    request(ACTION_LOAD_STATE)
    yield 100
    if not (level() == trace.ATLANTIS1 and mode() == 4):
        emu.fail("the second load did not stay in Atlantis 1")
    emu.shot("loaded")

    # A load from a level with another song.
    level_info = sym("levelInfo")
    song = lambda lv: emu.r8(level_info + LEVEL_INFO_SIZE * lv + 0x35)  # noqa: E731
    other = next(lv for lv in range(42) if song(lv) not in (0, song(trace.ATLANTIS1)))
    emu.w8(sym("run") + RUN_WARP_LEVEL, other)
    request(ACTION_WARP)
    yield from wait_for(emu, lambda: level() == other and mode() == 4 and fc() >= 40, 4000, f"level {other}")
    playing = emu.r32(sym("audioCtrl") + AUDIO_CTRL_CURRENT)
    if playing == song(trace.ATLANTIS1):
        emu.fail(f"level {other} plays Atlantis 1's song {playing}: the load would not show the change")
    request(ACTION_LOAD_STATE)
    yield 60
    after = emu.r32(sym("audioCtrl") + AUDIO_CTRL_CURRENT)
    print(f"savestate: song {playing} in level {other}; after the load, level {level()} with song {after}")
    if level() != trace.ATLANTIS1 or after != song(trace.ATLANTIS1):
        emu.fail(f"after the load from level {other}: level {level()}, song {after}, not {song(trace.ATLANTIS1)}")
