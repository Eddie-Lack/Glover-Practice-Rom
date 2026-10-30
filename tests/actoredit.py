"""Editing an actor in Atlantis 1 (Inspect, Actors, an actor's page, Start; practice/src/actorview.c), with the menu's
own inputs:

1. The first enemy's page, Start: pos x, then RIGHT three times at the step of 1 adds 3; LEFT once takes 1 off.
2. DOWN to vel x, C up (the step becomes 10), RIGHT: the speed's x goes up by 10. C down twice (0.1 steps), RIGHT: up
   by 0.1.
3. DOWN to the state's hits, step 10 again (C up twice), RIGHT: hits changes by 10. DOWN twice to mode, LEFT: mode
   goes down by 10.
4. The edits taint the run (BT_ACTORS) and set run.actorsMod, the HUD's MODIFIED marker.
5. Start again leaves the editing; LEFT then shows another actor and changes no value."""
import trace
from menunav import goto
from tools import C_ALL, press, wait_for

BT_TAINT, BT_ACTORS = 0x01, 1 << 3
RUN_ACTORS_MOD = 0x3F
ACTOR_POS, ACTOR_VEL, ACTOR_STATE = 0x34, 0x58, 0xD4
STATE_HITS, STATE_MODE = 0x00, 0x04
MODE_LEVEL = 4


def s8(v):
    return v - 256 if v > 127 else v


def run(emu, sym, args):
    gi = sym("gameInfo")
    fc = lambda: emu.r32(sym("frameCount"))  # noqa: E731
    mode = lambda: emu.r8(gi)  # noqa: E731
    level = lambda: emu.r8(gi + 1)  # noqa: E731
    al = sym("actorList")
    run_ = sym("run")
    taint = lambda: emu.r8(sym("bestTimesState") + BT_TAINT)  # noqa: E731

    yield from wait_for(emu, lambda: level() == trace.PRESENT and fc() >= 200, 4000, "the logos")
    yield from press(emu, "START", frames=6)
    yield from wait_for(emu, lambda: level() == trace.FLYTHRU and mode() == MODE_LEVEL and fc() >= 30, 3000, "the title")
    yield from press(emu, *C_ALL)
    for b in ("A", "DOWN", "DOWN", "A"):  # Warp, Go
        yield from press(emu, b)
    yield from wait_for(emu, lambda: level() == trace.ATLANTIS1 and mode() == MODE_LEVEL and fc() >= 100, 3000,
                        "Atlantis 1")
    yield 30

    actors = []
    a = emu.r32(al)
    while a != al and a and len(actors) < 500:
        actors.append(a)
        a = emu.r32(a)
    enemy = sym("enemies")
    if enemy not in actors or not emu.r32(enemy + ACTOR_STATE):
        emu.fail("Atlantis 1 has no enemy with a state in the actor list")
    index = actors.index(enemy)
    state = emu.r32(enemy + ACTOR_STATE)
    if emu.r8(run_ + RUN_ACTORS_MOD) or taint() & BT_ACTORS:
        emu.fail("the run is marked modified before the first edit")

    yield from press(emu, *C_ALL)
    yield from goto(emu, sym, "Inspect", "Actors")
    for _ in range(index):
        yield from press(emu, "DOWN")
    yield from press(emu, "A")  # the page of the enemy
    yield from press(emu, "START")

    x0, vx0 = emu.rf(enemy + ACTOR_POS), emu.rf(enemy + ACTOR_VEL)
    hits0, mode0 = s8(emu.r8(state + STATE_HITS)), emu.r16s(state + STATE_MODE)
    for _ in range(3):
        yield from press(emu, "RIGHT")
    yield from press(emu, "LEFT")
    x1 = emu.rf(enemy + ACTOR_POS)
    print(f"actoredit: pos x {x0:.2f} -> {x1:.2f}")
    if abs(x1 - (x0 + 2)) > 0.01:
        emu.fail(f"pos x went from {x0} to {x1}, not up by 2")
    if not emu.r8(run_ + RUN_ACTORS_MOD) or not taint() & BT_ACTORS:
        emu.fail("the edit did not mark the run modified")

    for _ in range(3):
        yield from press(emu, "DOWN")
    yield from press(emu, "CUP")
    yield from press(emu, "RIGHT")
    vx1 = emu.rf(enemy + ACTOR_VEL)
    for _ in range(2):
        yield from press(emu, "CDOWN")
    yield from press(emu, "RIGHT")
    vx2 = emu.rf(enemy + ACTOR_VEL)
    print(f"actoredit: vel x {vx0:.2f} -> {vx1:.2f} -> {vx2:.2f}")
    if abs(vx1 - (vx0 + 10)) > 0.01 or abs(vx2 - (vx1 + 0.1)) > 0.01:
        emu.fail(f"vel x went {vx0}, {vx1}, {vx2}")

    for _ in range(6):  # vel x to hits: vel y, vel z, rot x, rot y, rot z, hits
        yield from press(emu, "DOWN")
    for _ in range(2):
        yield from press(emu, "CUP")
    yield from press(emu, "RIGHT")
    hits1 = s8(emu.r8(state + STATE_HITS))
    for _ in range(2):
        yield from press(emu, "DOWN")
    yield from press(emu, "LEFT")
    mode1 = emu.r16s(state + STATE_MODE)
    print(f"actoredit: hits {hits0} -> {hits1}, mode {mode0} -> {mode1}")
    if hits1 != hits0 + 10 or mode1 != mode0 - 10:
        emu.fail(f"hits {hits0} -> {hits1} (want +10), mode {mode0} -> {mode1} (want -10)")

    # 5. Start leaves the editing: LEFT shows the other actor and changes nothing.
    yield from press(emu, "START")
    yield from press(emu, "LEFT")
    if emu.r16s(state + STATE_MODE) != mode1 or abs(emu.rf(enemy + ACTOR_POS) - (x0 + 2)) > 0.01:
        emu.fail("LEFT after the editing changed a value")
    for b in ("B", "B", "B"):
        yield from press(emu, b)
    yield from press(emu, *C_ALL)
    f0 = fc()
    yield 180
    print(f"actoredit: {fc() - f0} game frames in 3 s after the edits")
    if fc() - f0 < 20:
        emu.fail("the game stopped after the edits")
