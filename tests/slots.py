"""Savestate slots and compression (practice/include/savestate.h).

The input is a constant stick forward, so the game gets the same input whatever the video frame timing is.

1. In Atlantis 1: three states in slots 1 to 3. The first makes a base (the level's state, compressed on its own);
   the others differ from it by some kilobytes. Every save is decoded again at once and compared with the game's
   memory (stateVerify): not one chunk may differ.
2. The same in two more levels: each gets its own base.
3. Every slot loads back to the game that it saved: the level is right, and the game frames after the load equal
   those after the save.
4. A save into a used slot in another level frees the old state and its base once no slot uses it, and the states
   that lie behind it move down without harm.
5. The menu: the Slot choice, "Clear this slot" and "Clear all slots".
6. A level when all bases are taken, and a full pool: a save that does not fit says so, and the other states are
   still good.
The decoding of a save also covers the chunks that the save stored as "the same as the base's": the verify compares
every chunk. State 3 of step 1 has a change that keeps both plain sums of its chunk (a word up by one, the next down
by two, the next up by one); it must be stored, and load back.
The sizes and the times are printed."""
import bisect

import layout
import trace
from stateutil import Verify, hud_message
from tools import C_ALL, press, wait_for

ACTION_WARP = 1
ACTION_SAVE_STATE = 8
ACTION_LOAD_STATE = 9
RUN_WARP_LEVEL = 4        # Run.warpLevel (practice/include/actions.h)
SLOT_SIZE, BASE_SIZE = 24, 12  # Slot and Base (practice/src/savestate.c): size at +4, base at +8, level at +9
POOL_END = 0x806EC000       # STATE_POOL_END (practice/include/savestate.h)
BASES = 3                 # STATE_BASES (practice/include/savestate.h)
STATE_SIZE = 1965652      # build/state_ranges.h
TRACE = 40                # game frames compared after a save or a load
LEVELS = [trace.ATLANTIS1, 20, 30]
STICK = dict(x=0, y=80)
# Three words in the texture bank buffer below the code, which a level leaves alone once loaded.
COLLIDE = 0x800FF000


def run(emu, sym, args):
    gi = sym("gameInfo")
    fc_addr = sym("frameCount")
    hand, ball, player = sym("hand"), sym("ball"), sym("player")
    requests = sym("requests")
    slot_var, slots, bases = sym("stateSlot"), sym("stateSlots"), sym("stateBases")
    verify = Verify(emu, sym)
    mode = lambda: emu.r8(gi)  # noqa: E731
    level = lambda: emu.r8(gi + 1)  # noqa: E731
    fc = lambda: emu.r32(fc_addr)  # noqa: E731

    def request(action):
        emu.w16(requests, emu.r16(requests) | 1 << action)

    def state():
        s = [emu.r32(sym("rseed")), emu.r32(player + 0x18)]
        for actor in (hand, ball):
            s += [emu.r32(actor + o) for o in (0x34, 0x38, 0x3C, 0x58, 0x5C, 0x60)]
        return tuple(s)

    def slot(i):
        a = slots + SLOT_SIZE * i
        return emu.r32(a + 4), emu.r8(a + 8), emu.r8(a + 9)  # size in bytes (0: empty), base, level

    def base_sizes():
        return [emu.r32(bases + BASE_SIZE * b + 4) for b in range(BASES)]

    def bases_levels():
        return [emu.r8(bases + BASE_SIZE * b + 8) for b in range(BASES)]

    def pool_free():
        return POOL_END - emu.r32(sym("statePoolEnd"))

    def collect(n):
        """{game frame: {states}} for the next N game frames."""
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

    announced = lambda: emu.r32(sym("actionsAnnounced"))  # noqa: E731

    def save(i):
        before = emu.r32(sym("stateSaves"))
        shown = announced()
        emu.w8(slot_var, i)
        request(ACTION_SAVE_STATE)
        yield from wait_for(emu, lambda: emu.r32(sym("stateSaves")) > before, 1500, f"the save into slot {i + 1}")
        yield 3
        if announced() != shown + 1:
            emu.fail(f"the save into slot {i + 1} did not wait a frame to show \"saving...\"")
        verify.check(f"slot {i + 1}")
        ticks = emu.r32(sym("stateSaveTicks"))
        size = slot(i)[0]
        return size, ticks

    def load(i):
        before = emu.r32(sym("stateLoads"))
        shown = announced()
        emu.w8(slot_var, i)
        request(ACTION_LOAD_STATE)
        yield from wait_for(emu, lambda: emu.r32(sym("stateLoads")) > before, 1500, f"the load of slot {i + 1}")
        if announced() != shown + 1:
            emu.fail(f"the load of slot {i + 1} did not wait a frame to show \"loading...\"")
        return emu.r32(sym("stateLoadTicks"))

    def goto(lv):
        emu.w8(sym("run") + RUN_WARP_LEVEL, lv)
        request(ACTION_WARP)
        yield from wait_for(emu, lambda: level() == lv and mode() == 4 and 40 <= fc() < 400, 4000, f"level {lv}")

    def same(a, b, what):
        # The draw thread's calls of Random fall between the game frames a little differently for the first game
        # frames after a save or a load: the first three are left out (tests/savestate.py compares from the second).
        a = {f: v for f, v in a.items() if f > min(a) + 2}
        bad = [f for f in sorted(a) if f in b and not a[f] & b[f]]
        missing = [f for f in a if f not in b]
        if bad:
            emu.fail(f"{what}: game frame {bad[0]} differs: {sorted(a[bad[0]])[0]} and {sorted(b[bad[0]])[0]}")
        if len(missing) > 3 or len(a) < TRACE - 4:
            emu.fail(f"{what}: only {len(a) - len(missing)} of {len(a)} game frames were seen again")

    for i in layout.SETTINGS_BINDINGS:
        emu.w8(sym("settings") + i, 0)
    emu.w8(sym("stateVerify"), 1)
    yield from wait_for(emu, lambda: level() == trace.PRESENT and fc() >= 200, 4000, "the logos")
    yield from press(emu, "START", frames=6)
    yield from wait_for(emu, lambda: level() == trace.FLYTHRU and mode() == 4 and fc() >= 30, 3000, "the title")
    yield from press(emu, *C_ALL)
    for b in ("A", "DOWN", "DOWN", "A"):  # Warp, Go
        yield from press(emu, b)
    yield from wait_for(emu, lambda: level() == trace.ATLANTIS1 and mode() == 4 and 0 < fc() < 20, 3000, "Atlantis 1")
    emu.hold(**STICK)
    yield from wait_for(emu, lambda: fc() >= 80, 1000, "game frame 80")
    free0 = pool_free()
    print(f"slots: the pool is {free0 >> 10} KB; a state is {STATE_SIZE >> 10} KB")

    traces = {}   # slot: (level, {game frame: {states}})
    sizes = {}

    def keep(i):
        size, ticks = yield from save(i)
        t = yield from collect(TRACE)
        traces[i] = (level(), t)
        sizes[i] = size
        return size, ticks

    # 1. Three states in Atlantis 1.
    original = [emu.r32(COLLIDE + 4 * k) for k in range(3)]
    changed = [(original[0] + 1) & 0xFFFFFFFF, (original[1] - 2) & 0xFFFFFFFF, (original[2] + 1) & 0xFFFFFFFF]
    for n, i in enumerate((0, 1, 2)):
        if n == 2:
            for k in range(3):
                emu.w32(COLLIDE + 4 * k, changed[k])
        size, ticks = yield from keep(i)
        for k in range(3):
            emu.w32(COLLIDE + 4 * k, original[k])
        used = free0 - pool_free()
        print(f"slots: slot {i + 1} in level {level()}: {size} bytes; the pool holds {used >> 10} KB; "
              f"save {ticks} ticks ({ticks / 46875:.0f} ms)")
        if n == 0:
            base = base_sizes()[0]
            if not 200_000 < base < 900_000:
                emu.fail(f"the base is {base} bytes")
            print(f"slots: the base is {base} bytes, {STATE_SIZE / base:.2f} times smaller than the state")
        elif size > 120_000:
            emu.fail(f"slot {i + 1} took {size} bytes: it should differ from the base by a few kilobytes")
        if sum(1 for s in base_sizes() if s) != 1:
            emu.fail(f"{sum(1 for s in base_sizes() if s)} bases for one level")
        yield from wait_for(emu, lambda: fc() >= 30 * (n + 3), 1000, "more game frames")
        yield 30

    # 2. Two more levels.
    for lv, slot_ids in ((LEVELS[1], (3, 4)), (LEVELS[2], (5,))):
        yield from goto(lv)
        for i in slot_ids:
            size, ticks = yield from keep(i)
            print(f"slots: slot {i + 1} in level {lv}: {size} bytes; the pool holds {(free0 - pool_free()) >> 10} KB; "
                  f"save {ticks} ticks ({ticks / 46875:.0f} ms)")
            yield 30
    nb = sum(1 for s in base_sizes() if s)
    if nb != 3:
        emu.fail(f"{nb} bases for three levels")
    if [slot(i)[1] for i in (0, 3, 5)] == [slot(0)[1]] * 3:
        emu.fail("the three levels share a base")

    # 3. Every slot back, in a mixed order.
    for i in (2, 5, 0, 3, 1, 4):
        ticks = yield from load(i)
        want = traces[i][0]
        yield from wait_for(emu, lambda: mode() == 4, 200, "the level")
        t = yield from collect(TRACE)
        if level() != want:
            emu.fail(f"slot {i + 1} was saved in level {want}, and the load gave level {level()}")
        same(traces[i][1], t, f"after the load of slot {i + 1}")
        if i == 2:
            got = [emu.r32(COLLIDE + 4 * k) for k in range(3)]
            if got != changed:
                emu.fail(f"slot 3 was saved with words {changed} that keep the sums of the base's chunk; it loaded {got}")
            for k in range(3):
                emu.w32(COLLIDE + 4 * k, original[k])
        print(f"slots: slot {i + 1} loaded in level {level()}: load {ticks} ticks ({ticks / 46875:.0f} ms)")

    # 4. A save in another level into a used slot: slot 2 (Atlantis 1) takes the state of level 30.
    yield from load(5)
    yield 5
    before = pool_free()
    size, ticks = yield from keep(1)
    if slot(1)[2] != 30:
        emu.fail("slot 2 did not take level 30")
    # Slot 2 and slot 3 still use the base of Atlantis 1: Atlantis 1's base stays until both are gone.
    if sum(1 for s in base_sizes() if s) != 3:
        emu.fail("the base of Atlantis 1 was freed while slot 1 and 3 use it")
    traces[1] = (30, traces[1][1])
    # Clear slots 1 and 3 through the menu: the base of Atlantis 1 goes, and the states behind it move down.
    free_before_clear = pool_free()
    emu.release()
    yield from press(emu, *C_ALL)
    if not emu.r8(sym("menuOpen")):
        emu.fail("the menu did not open")
    yield from press(emu, "DOWN")  # Savestate
    yield from press(emu, "A")
    for target in (2, 0):          # slot 3, then slot 1
        for _ in range(12):        # Slot is the first item: bring it to slot TARGET
            if emu.r8(slot_var) == target:
                break
            yield from press(emu, "RIGHT")
        if emu.r8(slot_var) != target:
            emu.fail("the Slot choice does not reach the slot")
        for _ in range(8):
            yield from press(emu, "DOWN")  # Clear this slot
        yield from press(emu, "A")
        yield 5
        for _ in range(8):
            yield from press(emu, "UP")
        if slot(target)[0]:
            emu.fail(f"slot {target + 1} was not cleared")
    for _ in range(3):
        yield from press(emu, "B")
    if emu.r8(sym("menuOpen")):
        emu.fail("the menu did not close")
    nb = sum(1 for s in base_sizes() if s)
    print(f"slots: after clearing slots 1 and 3: {nb} bases, the pool gained {(pool_free() - free_before_clear) >> 10} KB")
    if nb != 2:
        emu.fail(f"{nb} bases after the clear: the base of Atlantis 1 should be gone")
    emu.hold(**STICK)
    for i in (3, 5, 1, 4):  # slots 4 and 6 and 2 and 5: their states moved in the pool
        yield from load(i)
        yield from wait_for(emu, lambda: mode() == 4, 200, "the level")
        t = yield from collect(TRACE)
        if level() != traces[i][0]:
            emu.fail(f"slot {i + 1}: level {level()}, not {traces[i][0]}")
        same(traces[i][1], t, f"after the clear, the load of slot {i + 1}")
    print("slots: states behind a cleared one load as before")

    # 5. Fill: every slot, then a new base for a fourth level. Level 1 ends if Glover walks on, so the stick is released.
    emu.release()
    yield from goto(1)
    for i in (0, 2, 6, 7, 8, 9):
        size, ticks = yield from keep(i)
        print(f"slots: slot {i + 1} in level 1: {size} bytes; free {pool_free() >> 10} KB; save {ticks} ticks")
        yield 10
    used = sum(1 for i in range(10) if slot(i)[0])
    print(f"slots: {used} of 10 slots hold a state; the pool has {pool_free() >> 10} KB free")
    if used != 10:
        emu.fail(f"{used} slots hold a state, not 10")

    # 6. A fourth level: the three bases are taken, so the state differs from the base of another level (the one with
    # the most equal chunks), or does not fit.
    yield from goto(35)
    before = emu.r32(sym("stateSaves"))
    emu.w8(slot_var, 0)
    request(ACTION_SAVE_STATE)
    yield from wait_for(emu, lambda: emu.r32(sym("stateSaves")) > before, 1500, "the save in the fifth level")
    yield 3
    verify.check("the fifth level")
    size = slot(0)[0]
    print(f"slots: slot 1 in the fourth level: {size} bytes (0: it did not fit), on the base of level "
          f"{bases_levels()[slot(0)[1]]}; free {pool_free() >> 10} KB")
    if size:
        t = yield from collect(TRACE)
        traces[0] = (35, t)
        yield from goto(10)
        yield from load(0)
        yield from wait_for(emu, lambda: mode() == 4, 200, "the level")
        if level() != 35:
            emu.fail(f"the load of the fourth level's state gave level {level()}")
    if size and pool_free() < size:
        # A save that does not fit: the slot's own state goes first, and the message says so.
        for i, text in ((6, "state not saved: pool full, slot 7 emptied"), (6, "state not saved: pool full (slot 7)")):
            before = emu.r32(sym("stateSaves"))
            emu.w8(slot_var, i)
            request(ACTION_SAVE_STATE)
            yield from wait_for(emu, lambda: emu.r32(sym("stateSaves")) > before, 1500, "the save that does not fit")
            yield 3
            shown = hud_message(emu, sym)
            if slot(i)[0] != 0 or shown != text:
                emu.fail(f"a save into slot {i + 1} that does not fit: {slot(i)[0]} bytes, message \"{shown}\"")
        print(f"slots: a save that does not fit empties its slot and says so: \"{shown}\"")
    # The other states are still good. Their traces were recorded with the stick forward.
    emu.hold(**STICK)
    for i in (3, 5):
        yield from load(i)
        yield from wait_for(emu, lambda: mode() == 4, 200, "the level")
        t = yield from collect(TRACE)
        same(traces[i][1], t, f"after the full pool, the load of slot {i + 1}")
    print("slots: ok")
