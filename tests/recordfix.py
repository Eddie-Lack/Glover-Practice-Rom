"""Input recording and savestates at the edges of the practice loop, in Atlantis 1.

1. Record with the default binding (stick left + D-pad left), stand still, stop with it again: the recording holds
   no game frame with the stick held left at its end (the stop chord's frames are cut), and none at its start (the
   stick is kept from the game until it is let go).
2. Record pressed in slow motion is refused before anything changes: the last recording and its state stay.
3. A press of the player's own during Play recording ends the playback, and the controls are the player's again.
4. A save asked for during a warp's fade is refused ("game busy"): the slot keeps its state."""
import startlevel
from tools import press, wait_for

LEFT = dict(x=-80, y=0)
DOWN = dict(x=0, y=-80)
UP = dict(x=0, y=80)
RECORDING_BUFFER = 0x807DC000
SLOT_SIZE = 24
SLOT_RECORD = 10


def run(emu, sym, args):
    gi, fc = sym("gameInfo"), sym("frameCount")
    rec = sym("recording")
    run_ = sym("run")
    count = lambda: emu.r32(rec + 4)  # noqa: E731
    playing = lambda: emu.r8(rec + 1)  # noqa: E731

    def slot(n):
        return emu.r32(sym("stateSlots") + SLOT_SIZE * n + 4), emu.r8(sym("stateSlots") + SLOT_SIZE * n + 9)

    yield from startlevel.level(emu, sym)

    # 1. A recording made with the stick binding.
    yield from press(emu, "LEFT", **LEFT)
    yield from wait_for(emu, lambda: emu.r8(rec), 600, "the recording to start")
    yield 100
    emu.hold(**LEFT)
    yield 12
    emu.hold("LEFT", **LEFT)
    yield 8
    emu.release()
    yield from wait_for(emu, lambda: not emu.r8(rec), 300, "the recording to stop")
    yield 10
    n = count()
    frames = [emu.read(RECORDING_BUFFER + 4 * i, 4) for i in range(n)]
    print(f"the recording has {n} game frames")
    if n < 10:
        emu.fail(f"the recording has only {n} game frames")
    for name, f in (("first", frames[0]), ("last", frames[-1])):
        x = f[2] - 256 if f[2] > 127 else f[2]
        if x <= -60:
            emu.fail(f"the {name} game frame of the recording has the stick held left ({x})")

    # 2. Record in slow motion.
    old = (count(), slot(SLOT_RECORD))
    yield from press(emu, "LEFT", **DOWN)
    yield 20
    if emu.r8(run_ + 2) != 1:
        emu.fail(f"slow motion did not turn on ({emu.r8(run_ + 2)})")
    yield from press(emu, "LEFT", **LEFT)
    yield 60
    if emu.r8(rec) or (count(), slot(SLOT_RECORD)) != old:
        emu.fail(f"Record in slow motion changed the recording: {old} to {count(), slot(SLOT_RECORD)}")
    for _ in range(3):
        yield from press(emu, "LEFT", **DOWN)
    yield 20
    if emu.r8(run_ + 2):
        emu.fail("slow motion did not go back off")

    # 3. A press during playback takes over.
    yield from press(emu, "RIGHT", **LEFT)
    yield from wait_for(emu, playing, 600, "the playback to start")
    yield 20
    yield from press(emu, "A")
    yield 10
    if playing():
        emu.fail("A during the playback did not end it")

    # 4. A save during a warp's fade.
    yield from press(emu, "UP", **LEFT)
    yield from wait_for(emu, lambda: slot(0)[0] != 0, 600, "the save into slot 1")
    saved = slot(0)
    saves = emu.r32(sym("stateSaves"))
    yield from press(emu, "DOWN", **UP)  # Warp
    yield 20
    yield from press(emu, "UP", **LEFT)
    yield 60
    if slot(0) != saved or emu.r32(sym("stateSaves")) != saves:
        emu.fail(f"a save during the warp's fade was made: slot {saved} to {slot(0)}")
    yield from wait_for(emu, lambda: emu.r8(gi) == 4 and emu.r32(fc) > 30, 4000, "the level after the warp")
