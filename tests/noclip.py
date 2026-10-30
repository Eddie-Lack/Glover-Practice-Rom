"""Moon jump and noclip in Atlantis 1, through the Game menu (its 14th to 16th items; the Set action menu is the 17th, Chaos inputs the 18th, the free camera the 19th, Ghost the 20th, Trainers the 21st):

1. Moon jump on, L held on controller 2: the glove rises. Released, it falls again. The HUD marker shows while the
   switch is on. A text frame is drawn only when something is to show, and the test turns the timer off, so the
   count of text frames tells whether the marker is drawn.
2. Noclip on, the stick pushed forward: the glove flies a long way. D-pad up lifts it, and it stays where it is
   when the D-pad is let go (no gravity). The stick fully up with the D-pad does not fire a binding. The game's gravity and landscape flags are off in the meantime.
3. A state saved in noclip, then noclip off: the flags are back, the glove falls, and with both switches off the
   marker is gone. Loading the state gives the flags back as well."""
import layout
import trace
from menunav import goto
from tools import C_ALL, press, wait_for

CONT_L = 0x0020
ACTOR_FLAGS = 0x28
ACTOR_POS = 0x34
FLAGS_OFF = 0x11  # gravity and landscape collision
RUN_MOON_JUMP, RUN_NOCLIP, RUN_NOCLIP_SPEED = 0x33, 0x34, 0x35
ACTION_SAVE_STATE, ACTION_LOAD_STATE = 8, 9
PRACTICE_TEXT_FRAMES = 0x08  # Practice.textFrames


def run(emu, sym, args):
    gi = sym("gameInfo")
    fc = lambda: emu.r32(sym("frameCount"))  # noqa: E731
    mode = lambda: emu.r8(gi)  # noqa: E731
    level = lambda: emu.r8(gi + 1)  # noqa: E731
    hand = sym("hand")
    run_ = sym("run")
    settings = sym("settings")
    pos = lambda: [emu.rf(hand + ACTOR_POS + 4 * i) for i in range(3)]  # noqa: E731
    flags = lambda: emu.r32(hand + ACTOR_FLAGS)  # noqa: E731
    text_frames = lambda: emu.r32(sym("practice") + PRACTICE_TEXT_FRAMES)  # noqa: E731

    yield from wait_for(emu, lambda: level() == trace.PRESENT and fc() >= 200, 4000, "the logos")
    yield from press(emu, "START", frames=6)
    yield from wait_for(emu, lambda: level() == trace.FLYTHRU and mode() == 4 and fc() >= 30, 3000, "the title")
    yield from press(emu, *C_ALL)
    for b in ("A", "DOWN", "DOWN", "A"):  # Warp, Go
        yield from press(emu, b)
    yield from wait_for(emu, lambda: level() == trace.ATLANTIS1 and mode() == 4 and fc() >= 100, 3000, "Atlantis 1")
    emu.w8(settings + layout.SETTINGS_HUD, 0)  # the timer off: no text unless the marker is drawn
    yield 60

    def game_menu(*path):
        """Opens the menu, chooses the items of PATH (A on each) and closes it."""
        yield from press(emu, *C_ALL)
        if not emu.r8(sym("menuOpen")):
            emu.fail("the menu did not open")
        yield from goto(emu, sym, *path)
        yield from press(emu, *C_ALL)
        yield 10

    def request(action):
        requests = sym("requests")
        emu.w16(requests, emu.r16(requests) | 1 << action)

    def marker_drawn(frames=30):
        before = text_frames()
        yield frames
        return text_frames() - before >= 2

    if (yield from marker_drawn()):
        emu.fail("text is drawn with every switch off")
    if emu.r8(run_ + RUN_MOON_JUMP) or emu.r8(run_ + RUN_NOCLIP) or emu.r8(run_ + RUN_NOCLIP_SPEED) != 2:
        emu.fail("the switches are not off, or the speed is not 8, at the start")

    # 1. Moon jump (Game, Movement and camera).
    yield from game_menu("Game", "Movement and camera", "Moon jump")
    if not emu.r8(run_ + RUN_MOON_JUMP):
        emu.fail("the moon jump did not turn on")
    if not (yield from marker_drawn()):
        emu.fail("no marker with the moon jump on")
    y0 = pos()[1]
    f0 = fc()
    for _ in range(60):
        emu.w16(sym("controllerdata") + 6, CONT_L)
        yield 1
    y1 = pos()[1]
    print(f"noclip: moon jump, height {y0:.1f} to {y1:.1f} after {fc() - f0} game frames")
    if y1 < y0 + 60:
        emu.fail(f"the moon jump rose from {y0:.1f} to {y1:.1f}, not at least 60 units")
    emu.w16(sym("controllerdata") + 6, 0)
    top = pos()[1]
    for _ in range(30):  # the glove still rises for a few game frames, then falls
        yield 3
        top = max(top, pos()[1])
    if pos()[1] > top - 10:
        emu.fail(f"the glove did not fall after the moon jump let go ({top:.1f} to {pos()[1]:.1f})")
    yield from game_menu("Game", "Movement and camera", "Moon jump")
    if emu.r8(run_ + RUN_MOON_JUMP):
        emu.fail("the moon jump did not turn off")
    if (yield from marker_drawn()):
        emu.fail("the marker is drawn with the moon jump off")
    yield 100  # on the ground again

    # 2. Noclip (the next item): speed 8, the stick forward for about 30 game frames.
    yield from game_menu("Game", "Movement and camera", "Noclip")
    if not emu.r8(run_ + RUN_NOCLIP):
        emu.fail("noclip did not turn on")
    if not (yield from marker_drawn()):
        emu.fail("no marker with noclip on")
    if flags() & FLAGS_OFF:
        emu.fail(f"the glove's flags are {flags():#x} in noclip: gravity or collision is on")
    start = pos()
    f0 = fc()
    emu.hold(x=0, y=80)
    yield 90
    emu.release()
    yield 3
    moved = pos()
    flat = ((moved[0] - start[0]) ** 2 + (moved[2] - start[2]) ** 2) ** 0.5
    print(f"noclip: forward, {flat:.0f} units in {fc() - f0} game frames; height {start[1]:.1f} to {moved[1]:.1f}")
    # 8 units a game frame at the default speed, with the stick at 80 of 80: once a game frame, not more.
    frames = fc() - f0
    if not 6 * frames <= flat <= 9 * frames:
        emu.fail(f"the glove moved {flat:.1f} units in {frames} game frames, not about 8 a game frame")
    if abs(moved[1] - start[1]) > 3:
        emu.fail(f"the glove changed height by {moved[1] - start[1]:.1f} flying level")
    # The stick fully up with the D-pad is the default binding for a reset: noclip turns the bindings off.
    emu.hold("UP", x=0, y=80)
    yield 45
    emu.release()
    yield 15
    lifted = pos()[1]
    yield 40
    if abs(pos()[1] - lifted) > 1 or lifted < moved[1] + 60:
        emu.fail(f"D-pad up lifted the glove to {lifted:.1f} from {moved[1]:.1f}, and it ended at {pos()[1]:.1f}")
    if flags() & FLAGS_OFF:
        emu.fail(f"the glove's flags are {flags():#x} in noclip: gravity or collision is on")

    # A state saved in noclip keeps the glove's flags.
    saved = sym("stateSaved")
    request(ACTION_SAVE_STATE)
    yield 10
    if not emu.r8(saved):
        emu.fail("the state was not saved in noclip")
    if flags() & FLAGS_OFF:
        emu.fail(f"the glove's flags are {flags():#x} after a save in noclip: gravity or collision is on")

    # 3. Noclip off: the flags are back and the glove falls.
    yield from game_menu("Game", "Movement and camera", "Noclip")
    if emu.r8(run_ + RUN_NOCLIP):
        emu.fail("noclip did not turn off")
    if flags() & FLAGS_OFF != FLAGS_OFF:
        emu.fail(f"the glove's flags are {flags():#x} after noclip: gravity or collision is still off")
    top = pos()[1]
    yield 12
    if pos()[1] > top - 5:
        emu.fail(f"the glove did not fall after noclip ({top:.1f} to {pos()[1]:.1f})")
    yield 240  # the "state saved" message goes out
    if (yield from marker_drawn()):
        emu.fail("the marker is drawn with both switches off")

    # The state, loaded with noclip off, has the flags that the glove had.
    request(ACTION_LOAD_STATE)
    yield 10
    yield 5
    if flags() & FLAGS_OFF != FLAGS_OFF:
        emu.fail(f"the glove's flags are {flags():#x} after loading a state saved in noclip")
