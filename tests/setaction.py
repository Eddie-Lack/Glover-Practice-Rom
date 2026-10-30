"""Set action in Atlantis 1, through the Game menu's fifth last item, Set action:

1. Duck, then cartwheel: after each, the glove's action mode in RAM becomes that action within a few game frames,
   the game goes on (frameCount advances for three seconds, the glove's action time or position keeps changing,
   the practice code's run state is clear) and the HUD's MODIFIED marker is drawn. The marker goes out after a
   few seconds.
2. The ball state "burst" sets the ball's state to 2 (BALL_BURST) and the game goes on.
3. With the tool unused (before the first use) no text is drawn."""
import layout
import trace
from menunav import goto, to_item
from tools import C_ALL, press, wait_for

RUN_SET_GLOVE, RUN_SET_BALL, RUN_SET_REQUEST, RUN_SET_BALL_REQUEST, RUN_SET_MARKER = 0x36, 0x37, 0x38, 0x39, 0x3A
ACTOR_STATE = 0xD4
STATE_MODE = 0x04
PRACTICE_TEXT_FRAMES = 0x08  # Practice.textFrames
BALL_STATE = 0x1E  # BallControl.state
LIST = ["idle", "walk", "jump", "joined", "throw", "prewhack", "whack", "skidding", "duck", "release", "getup",
        "throwaim", "throw2cup", "falling", "bounce", "bounce2cup", "airwhack", "throw2land", "slam", "slam2st",
        "ballwalk", "crawl", "cartwheel"]
MODES = {name: i for i, name in enumerate(LIST)}
COUNT = 41


def run(emu, sym, args):
    gi = sym("gameInfo")
    fc = lambda: emu.r32(sym("frameCount"))  # noqa: E731
    mode = lambda: emu.r8(gi)  # noqa: E731
    level = lambda: emu.r8(gi + 1)  # noqa: E731
    hand = sym("hand")
    run_ = sym("run")
    settings = sym("settings")
    text_frames = lambda: emu.r32(sym("practice") + PRACTICE_TEXT_FRAMES)  # noqa: E731
    action = lambda: emu.r16(emu.r32(hand + ACTOR_STATE) + STATE_MODE)  # noqa: E731

    yield from wait_for(emu, lambda: level() == trace.PRESENT and fc() >= 200, 4000, "the logos")
    yield from press(emu, "START", frames=6)
    yield from wait_for(emu, lambda: level() == trace.FLYTHRU and mode() == 4 and fc() >= 30, 3000, "the title")
    yield from press(emu, *C_ALL)
    for b in ("A", "DOWN", "DOWN", "A"):  # Warp, Go
        yield from press(emu, b)
    yield from wait_for(emu, lambda: level() == trace.ATLANTIS1 and mode() == 4 and fc() >= 100, 3000, "Atlantis 1")
    emu.w8(settings + layout.SETTINGS_HUD, 0)  # the timer off: no text unless the marker is drawn
    yield 60

    def marker_drawn(frames=30):
        before = text_frames()
        yield frames
        return text_frames() - before >= 2

    if (yield from marker_drawn()):
        emu.fail("text is drawn with the tool unused")

    chosen = 0
    chosen_once = False

    def set_glove(name, index):
        """Game, then Set action; picks the action by RIGHT presses and applies it."""
        nonlocal chosen, chosen_once
        was = chosen_once
        chosen_once = True
        yield from press(emu, *C_ALL)
        if not emu.r8(sym("menuOpen")):
            emu.fail("the menu did not open")
        yield from goto(emu, sym, "Game", "Set action")
        for _ in range((index - chosen) % COUNT):
            yield from press(emu, "RIGHT")
        chosen = index
        if emu.r8(run_ + RUN_SET_GLOVE) != index:
            emu.fail(f"the menu chose glove action {emu.r8(run_ + RUN_SET_GLOVE)}, not {index} ({name})")
        yield from press(emu, "DOWN")
        yield from press(emu, "A")  # Set glove action
        for b in ("B", "B"):
            yield from press(emu, b)
        yield from press(emu, *C_ALL)

    def check_action(name, index):
        mode_wanted = index
        seen = []
        for _ in range(40):
            seen.append(action())
            if action() == mode_wanted:
                break
            yield 1
        if action() != mode_wanted and mode_wanted not in seen:
            emu.fail(f"set action {name}: the glove's action is {seen[-1]}, not {mode_wanted}")
        f0 = fc()
        yield 180  # three seconds
        print(f"setaction: {name} set (action {mode_wanted}); now {action()}, {fc() - f0} game frames in 3 s")
        if fc() - f0 < 20:
            emu.fail(f"set action {name}: the game stopped (frameCount advanced {fc() - f0})")
        if emu.r8(run_ + RUN_SET_REQUEST) or emu.r8(run_ + RUN_SET_BALL_REQUEST):
            emu.fail(f"set action {name}: the request was not taken")

    for name in ("duck", "cartwheel"):
        index = MODES[name]
        yield from set_glove(name, index)
        # The marker is drawn while the game runs on.
        if not (yield from marker_drawn(10)):
            emu.fail(f"set action {name}: no MODIFIED marker")
        yield from check_action(name, index)
        yield 200  # the marker times out (60 drawn frames)
        if (yield from marker_drawn()):
            emu.fail(f"set action {name}: the marker is still drawn after the time is up")
        yield 60

    # The ball: burst (state 2).
    yield from press(emu, *C_ALL)
    yield from goto(emu, sym, "Game", "Set action")
    yield from to_item(emu, sym, "Ball state")
    if emu.r8(run_ + RUN_SET_BALL) != 0:
        emu.fail("the ball choice is not 'burst' at the start")
    yield from goto(emu, sym, "Set ball state")
    for b in ("B", "B"):
        yield from press(emu, b)
    yield from press(emu, *C_ALL)
    ballctl = sym("ballControl")
    seen = []
    for _ in range(20):
        seen.append(emu.r8(ballctl + BALL_STATE))
        yield 1
    print(f"setaction: ball burst, states seen {sorted(set(seen))}")
    if 2 not in seen:
        emu.fail(f"set ball state burst: the ball's state was {sorted(set(seen))}, never 2")
    f0 = fc()
    yield 180
    if fc() - f0 < 20:
        emu.fail("set ball state burst: the game stopped")
