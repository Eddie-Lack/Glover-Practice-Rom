"""Input recording and playback. In Atlantis 1, Record saves a state and records the route's random input
(tests/trace.py) for RUN game frames. Play loads the state and plays the input back, with nothing held on the
controller. The state must be the same on every game frame, and the playback must end by itself. A save into the
chosen slot between the two does not change where Play starts. A recording stops at RECORDING_MAX with a message."""
import layout
import trace
from stateutil import hud_message
from tools import C_ALL, press, wait_for

RUN = 150
ACTION_SAVE_STATE = 8
ACTION_RECORD = 10
ACTION_PLAY = 11
RECORDING_MAX = 36000       # practice/include/recording.h: 30 minutes at 20 game frames a second
RECORDING_BUFFER = 0x807DC000


def run(emu, sym, args):
    gi = sym("gameInfo")
    fc_addr = sym("frameCount")
    hand, ball, player = sym("hand"), sym("ball"), sym("player")
    requests = sym("requests")
    rec = sym("recording")
    mode = lambda: emu.r8(gi)  # noqa: E731
    level = lambda: emu.r8(gi + 1)  # noqa: E731
    fc = lambda: emu.r32(fc_addr)  # noqa: E731
    inputs = trace.level_input()
    keys = sorted(inputs)

    def request(action):
        emu.w16(requests, emu.r16(requests) | 1 << action)

    def state():
        s = [emu.r32(sym("rseed")), emu.r32(player + 0x18)]
        for actor in (hand, ball):
            s += [emu.r32(actor + o) for o in (0x34, 0x38, 0x3C, 0x58, 0x5C, 0x60)]
        return tuple(s)

    def record(until, live):
        """Samples the state on each game frame up to UNTIL; with LIVE, holds the route's input."""
        seen, held = {}, None
        for _ in range(RUN * 12):
            f = fc()
            if f >= until:
                return seen
            if live:
                k = max([k for k in keys if k <= f % 600] or [keys[0]])
                if inputs[k] != held:
                    emu.hold(*inputs[k][0], x=inputs[k][1], y=inputs[k][2])
                    held = inputs[k]
            seen.setdefault(f, set()).add(state())
            yield 1
        emu.fail(f"the level did not reach game frame {until}")

    for i in layout.SETTINGS_BINDINGS:
        emu.w8(sym("settings") + i, 0)
    yield from wait_for(emu, lambda: level() == trace.PRESENT and fc() >= 200, 4000, "the logos")
    yield from press(emu, "START", frames=6)
    yield from wait_for(emu, lambda: level() == trace.FLYTHRU and mode() == 4 and fc() >= 30, 3000, "the title")
    yield from press(emu, *C_ALL)
    for b in ("A", "DOWN", "DOWN", "A"):
        yield from press(emu, b)
    yield from wait_for(emu, lambda: level() == trace.ATLANTIS1 and mode() == 4 and fc() >= 100, 3000, "Atlantis 1")

    request(ACTION_RECORD)
    yield from wait_for(emu, lambda: emu.r8(rec), 100, "the recording to start")
    start = fc()
    first = yield from record(start + RUN, True)
    request(ACTION_RECORD)
    yield from wait_for(emu, lambda: not emu.r8(rec), 100, "the recording to stop")
    emu.release()
    count = emu.r32(rec + 4)
    print(f"recording: {count} game frames recorded")

    # A save into the chosen slot after the recording: Play still starts from the recording's own copy of the state.
    request(ACTION_SAVE_STATE)
    yield from wait_for(emu, lambda: emu.r32(sym("stateSaves")) >= 2, 600, "the second save")
    yield 10
    request(ACTION_PLAY)
    yield from wait_for(emu, lambda: emu.r8(rec + 1), 100, "the playback to start")
    second = yield from record(start + RUN, False)
    yield from wait_for(emu, lambda: not emu.r8(rec + 1), 400, "the playback to end")

    # The first game frame was sampled while the save ran in the first run.
    bad = [f for f in sorted(first) if f > start and f in second and not first[f] & second[f]]
    print(f"recording: {len(first)} game frames compared, {len(bad)} differ")
    if len(first) < RUN - 5:
        emu.fail(f"only {len(first)} game frames were sampled")
    if bad:
        f = bad[0]
        emu.fail(f"in the playback, game frame {f} differs: {sorted(first[f])[0]} and {sorted(second[f])[0]}")

    # The limit: the count is set to three game frames below RECORDING_MAX while recording.
    marker = RECORDING_BUFFER + RECORDING_MAX * 4
    emu.w32(marker, 0xDEADBEEF)
    request(ACTION_RECORD)
    yield from wait_for(emu, lambda: emu.r8(rec), 100, "the recording to start")
    emu.w32(rec + 4, RECORDING_MAX - 3)
    yield from wait_for(emu, lambda: not emu.r8(rec), 200, "the recording to stop at the limit")
    yield 2
    message = hud_message(emu, sym)
    print(f"recording: at the limit: {emu.r32(rec + 4)} game frames; \"{message}\"")
    if emu.r32(rec + 4) != RECORDING_MAX:
        emu.fail(f"the recording stopped at {emu.r32(rec + 4)} game frames, not {RECORDING_MAX}")
    if message != "recording stopped: 30 minutes is the limit":
        emu.fail(f"the message at the limit: \"{message}\"")
    if emu.r32(marker) != 0xDEADBEEF:
        emu.fail("the recording wrote behind its buffer's limit")
