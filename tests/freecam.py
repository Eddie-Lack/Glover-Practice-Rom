"""The free camera in Atlantis 1, through the Game menu (its third last item, 19th), flown with controller 2:

1. With the switch off the draw thread builds no free view and no note is drawn.
2. Switched on (speed 8): the free camera starts at the game's camera. The stick forward moves it along its view
   direction (about 8 units on each video frame it is read), the game's own camera and the glove stay where they
   are, the game keeps running, and the view matrix that the draw thread built differs from the game's.
3. C right turns the view, C up and C down tilt it, D-pad up lifts it. R makes the flight faster.
4. While the game waits (frame advance) the free camera still moves, the game does not.
5. A binding on controller 2's D-pad does not fire while the free camera is on (the D-pad flies the camera), a binding
   on its L still fires, and the D-pad binding fires when the camera is off. A view direction set straight up is
   turned away from the vertical on the next read, and the position stays a finite number.
6. Switched off: the matrix is the game's again (within the water's sway), and no free view is built. The note is gone.
Pictures of the three views go to build/tests/freecam/."""
import struct

import layout
import trace
from menunav import goto, to_item
from tools import C_ALL, press, wait_for

# FreeCam (practice/include/freecam.h)
FC_POS, FC_DIR, FC_GAME_EYE, FC_VIEW_EYE, FC_MOVES, FC_VIEWS, FC_ACTIVE = 0x00, 0x0C, 0x18, 0x24, 0x30, 0x34, 0x3C
RUN_FREE_CAM, RUN_FRAME_ADVANCE, RUN_HUD_HIDDEN = 0x46, 0x00, 0x03
ACTION_HUD, CONT_A, CONT_L = 5, 0x8000, 0x0020
ACTOR_POS = 0x34
PRACTICE_TEXT_FRAMES = 0x08
DYNAMIC_SIZE, DYNAMIC_CAMERA_VIEW = 0x2B230, 0x4080
CONT_R, CONT_CUP, CONT_CDOWN, CONT_CRIGHT, CONT_UP = 0x0010, 0x0008, 0x0004, 0x0001, 0x0800
SPEED = 8.0  # the third choice


def run(emu, sym, args):
    gi = sym("gameInfo")
    fc = lambda: emu.r32(sym("frameCount"))  # noqa: E731
    fc2 = lambda: emu.r32(sym("frameCount2"))  # noqa: E731
    mode = lambda: emu.r8(gi)  # noqa: E731
    level = lambda: emu.r8(gi + 1)  # noqa: E731
    vec = lambda a: [emu.rf(a + 4 * i) for i in range(3)]  # noqa: E731
    cam = freecam = sym("freeCam")
    game_cam = lambda: vec(sym("camera") + ACTOR_POS)  # noqa: E731
    hand = lambda: vec(sym("hand") + ACTOR_POS)  # noqa: E731
    run_ = sym("run")
    pos = lambda: vec(freecam + FC_POS)  # noqa: E731
    direction = lambda: vec(freecam + FC_DIR)  # noqa: E731
    views = lambda: emu.r32(freecam + FC_VIEWS)  # noqa: E731
    moves = lambda: emu.r32(freecam + FC_MOVES)  # noqa: E731
    text_frames = lambda: emu.r32(sym("practice") + PRACTICE_TEXT_FRAMES)  # noqa: E731
    pad = sym("controllerdata") + 6  # controller 2: button (u16), stick x, stick y

    def matrices():
        """The camera matrices of both frame buffers, as the draw thread built them."""
        return b"".join(bytes(emu.read(sym("dynamic") + b * DYNAMIC_SIZE + DYNAMIC_CAMERA_VIEW, 64)) for b in (0, 1))

    def decode(raw):
        """The 16 elements of an N64 fixed-point matrix (libultra's Mtx): integer halves, then fractions."""
        words = struct.unpack(">16I", raw)
        out = []
        for k in range(16):
            i = words[k // 2] >> (16 if k % 2 == 0 else 0) & 0xFFFF
            f = words[8 + k // 2] >> (16 if k % 2 == 0 else 0) & 0xFFFF
            out.append((i - 0x10000 if i & 0x8000 else i) + f / 65536)
        return out

    def matrix_distance(a, b):
        """The largest difference between two elements, over the matrices of both frame buffers."""
        return max(abs(x - y) for n in (0, 64) for x, y in zip(decode(a[n:n + 64]), decode(b[n:n + 64])))

    def dist(a, b):
        return sum((x - y) ** 2 for x, y in zip(a, b)) ** 0.5

    def fly(frames, buttons=0, x=0, y=0):
        """Holds controller 2 for FRAMES video frames. Returns the controller reads that moved the camera: the game
        reads the controllers once a game frame, which is every third video frame or less often."""
        m = moves()
        for _ in range(frames):
            emu.w16(pad, buttons)
            emu.w8(pad + 2, x)
            emu.w8(pad + 3, y)
            yield 1
        emu.w16(pad, 0)
        emu.w8(pad + 2, 0)
        emu.w8(pad + 3, 0)
        yield 6
        return moves() - m

    def picture(name):
        emu.fb_png("freecam_" + name + "_1", sym("cfb_16_b"))
        emu.fb_png("freecam_" + name + "_0", sym("cfb_16_a"))

    yield from wait_for(emu, lambda: level() == trace.PRESENT and fc() >= 200, 4000, "the logos")
    yield from press(emu, "START", frames=6)
    yield from wait_for(emu, lambda: level() == trace.FLYTHRU and mode() == 4 and fc() >= 30, 3000, "the title")
    yield from press(emu, *C_ALL)
    for b in ("A", "DOWN", "DOWN", "A"):  # Warp, Go
        yield from press(emu, b)
    yield from wait_for(emu, lambda: level() == trace.ATLANTIS1 and mode() == 4 and fc() >= 100, 3000, "Atlantis 1")
    emu.w8(sym("settings") + layout.SETTINGS_HUD, 0)  # the timer off: no text unless the note is drawn
    yield 120

    # 1. Off: no free view, no note, no text.
    t0, v0 = text_frames(), views()
    yield 40
    if views() != v0 or emu.r8(freecam + FC_ACTIVE) or emu.r8(run_ + RUN_FREE_CAM):
        emu.fail("the free camera is on, or builds views, at the start")
    if text_frames() - t0 >= 2:
        emu.fail("text is drawn with the free camera off")
    base_matrix = matrices()
    base_cam, base_hand = game_cam(), hand()
    picture("0_game")

    # 2. On, speed 8 (Game, Movement and camera, Free camera).
    yield from press(emu, *C_ALL)
    if not emu.r8(sym("menuOpen")):
        emu.fail("the menu did not open")
    yield from goto(emu, sym, "Game", "Movement and camera")
    yield from to_item(emu, sym, "Free camera")
    for b in ("RIGHT", "RIGHT", "RIGHT", "B"):
        yield from press(emu, b)
    yield from press(emu, *C_ALL)
    yield 30
    if emu.r8(run_ + RUN_FREE_CAM) != 3:
        emu.fail(f"the free camera speed is {emu.r8(run_ + RUN_FREE_CAM)}, not 3")
    if not emu.r8(freecam + FC_ACTIVE) or views() == 0:
        emu.fail("the free camera did not start")
    if dist(pos(), game_cam()) > 1 or dist(vec(freecam + FC_VIEW_EYE), vec(freecam + FC_GAME_EYE)) > 1:
        emu.fail(f"the free camera did not start at the game's camera: {pos()} and {game_cam()}")
    d0 = direction()
    if abs(dist(d0, [0, 0, 0]) - 1) > 0.01:
        emu.fail(f"the view direction {d0} is not a unit vector")
    t0 = text_frames()
    yield 30
    if text_frames() - t0 < 2:
        emu.fail("no 'free camera' note is drawn with the free camera on")

    start, f0, v0 = pos(), fc2(), views()
    reads = yield from fly(120, y=80)
    end = pos()
    flown = dist(start, end)
    along = sum((e - s) * d for e, s, d in zip(end, start, d0))
    print(f"freecam: forward {flown:.0f} units in {reads} reads, along the view {along:.0f}; {fc2() - f0} game frames")
    if reads < 10 or not 0.9 * SPEED * reads <= flown <= 1.1 * SPEED * reads:
        emu.fail(f"the camera moved {flown:.1f} units in {reads} reads, not about {SPEED} a read")
    if along < 0.98 * flown:
        emu.fail(f"the camera did not move along its view direction ({along:.1f} of {flown:.1f})")
    if dist(game_cam(), base_cam) > 2 or dist(hand(), base_hand) > 2:
        emu.fail(f"the game's camera or the glove moved: {base_cam} to {game_cam()}, {base_hand} to {hand()}")
    if fc2() - f0 < 10:
        emu.fail("the game stopped while the free camera flew")
    if views() - v0 < 10:
        emu.fail("the draw thread built few views from the free camera")
    free_matrix = matrices()
    if matrix_distance(free_matrix, base_matrix) < 0.5:
        emu.fail("the view matrix is the game's while the free camera flew")
    # The draw thread's own camera value is still the game's.
    for b in (0, 1):
        if dist(vec(sym("actualCamPos") + 12 * b), base_cam) > 2:
            emu.fail(f"actualCamPos[{b}] follows the free camera: {vec(sym('actualCamPos') + 12 * b)}")
    picture("1_forward")

    # 3. Turn, tilt, lift, and the fast flight.
    reads = yield from fly(60, buttons=CONT_CRIGHT)
    d1 = direction()
    print(f"freecam: C right, {reads} reads, direction {d0} to {d1}")
    if reads < 5 or sum(a * b for a, b in zip(d0, d1)) > 0.95 or abs(dist(d1, [0, 0, 0]) - 1) > 0.01:
        emu.fail(f"C right did not turn the view: {d0} to {d1}")
    # Looking to the right: the new direction has a positive part along the old 'right' (-dz, dx).
    if (-d0[2] * d1[0] + d0[0] * d1[2]) <= 0:
        emu.fail(f"C right turned the view to the left: {d0} to {d1}")
    yield from fly(30, buttons=CONT_CUP)
    d2 = direction()
    if d2[1] <= d1[1] + 0.1:
        emu.fail(f"C up did not tilt the view up: {d1} to {d2}")
    yield from fly(40, buttons=CONT_CDOWN)
    if direction()[1] >= d2[1] - 0.1:
        emu.fail(f"C down did not tilt the view down: {d2} to {direction()}")
    y0 = pos()[1]
    reads = yield from fly(40, buttons=CONT_UP)
    if reads < 5 or pos()[1] < y0 + 0.9 * SPEED * reads:
        emu.fail(f"D-pad up lifted the camera from {y0:.1f} to {pos()[1]:.1f} in {reads} reads")
    s0 = pos()
    reads = yield from fly(40, buttons=CONT_R, y=80)
    fast = dist(s0, pos()) / max(reads, 1)
    print(f"freecam: R, {fast:.1f} units a read")
    if reads < 5 or fast < 3.5 * SPEED:
        emu.fail(f"R did not make the flight faster ({fast:.1f} units a read)")
    picture("2_turned")
    if dist(game_cam(), base_cam) > 2:
        emu.fail("the game's camera moved with the free camera's turn")

    # 3b. A direction straight up (the flat length is 0) is turned away from the vertical, and no NaN gets in.
    for i, v in enumerate((0.0, 1.0, 0.0)):
        emu.wf(freecam + FC_DIR + 4 * i, v)
    reads = yield from fly(30, y=80)
    d, p = direction(), pos()
    if reads < 3 or any(x != x or abs(x) > 1e5 for x in d + p):
        emu.fail(f"a vertical direction gave a bad pose: {d} {p} after {reads} reads")
    if (d[0] ** 2 + d[2] ** 2) ** 0.5 < 0.04:
        emu.fail(f"the direction is still nearly vertical: {d}")

    # 4. The game waits (frame advance): the free camera moves, the game does not.
    emu.w8(run_ + RUN_FRAME_ADVANCE, 1)
    yield 30
    f0, p0 = fc2(), pos()
    reads = yield from fly(80, y=80)
    if fc2() != f0:
        emu.fail("the game ran during the frame advance")
    if reads < 5 or dist(p0, pos()) < 0.9 * SPEED * reads:
        emu.fail(f"the free camera did not move while the game waited ({dist(p0, pos()):.1f} units, {reads} reads)")
    emu.w8(run_ + RUN_FRAME_ADVANCE, 0)
    yield 30
    if fc2() <= f0:
        emu.fail("the game did not go on after the frame advance")

    # 5. A binding on a button of the camera (D-pad up: hide the HUD) does not fire while the camera is on. A binding
    # on a button that the camera does not use (L) still fires.
    settings = sym("settings")

    def bind_hud(button):
        emu.w16(settings + layout.SETTINGS_BIND_BUTTONS + 2 * ACTION_HUD, button)
        where = settings + layout.SETTINGS_BIND_WHERE + ACTION_HUD // 2
        emu.w8(where, emu.r8(where) & 0x0F | 0x10)  # an odd action has the high half; bit 0 is controller 2

    bind_hud(CONT_UP)
    emu.w8(run_ + RUN_HUD_HIDDEN, 0)
    yield from fly(60, buttons=CONT_UP)  # one press: the action toggles the HUD
    if emu.r8(run_ + RUN_HUD_HIDDEN):
        emu.fail("a binding on controller 2's D-pad fired while the free camera was on")
    bind_hud(CONT_L)
    yield from fly(60, buttons=CONT_L)
    if not emu.r8(run_ + RUN_HUD_HIDDEN):
        emu.fail("a binding on controller 2's L did not fire while the free camera was on")
    print("freecam: a controller 2 binding on the D-pad did not fire with the camera on; one on L did")
    emu.w8(run_ + RUN_HUD_HIDDEN, 0)
    bind_hud(CONT_UP)

    # 6. Off (RIGHT three times from speed 3 wraps to off): the game's matrix is back.
    yield from press(emu, *C_ALL)
    yield from goto(emu, sym, "Game", "Movement and camera")
    yield from to_item(emu, sym, "Free camera")
    for b in ("RIGHT", "RIGHT", "RIGHT", "B"):
        yield from press(emu, b)
    yield from press(emu, *C_ALL)
    yield 40
    if emu.r8(run_ + RUN_FREE_CAM) != 0 or emu.r8(freecam + FC_ACTIVE):
        emu.fail("the free camera did not turn off")
    v0, t0 = views(), text_frames()
    yield 60
    if views() != v0:
        emu.fail("views are built from the free camera after it was switched off")
    if text_frames() - t0 >= 2:
        emu.fail("the note is drawn after the free camera was switched off")
    if dist(game_cam(), base_cam) > 2:
        emu.fail("the game's camera moved")
    # Not byte for byte: the level's water sways the game's up vector a little on every frame.
    if matrix_distance(matrices(), base_matrix) > 0.05:
        emu.fail("the view matrix is not the game's after the free camera was switched off")
    picture("3_off")

    # The D-pad binding fires with the camera off.
    yield from fly(30, buttons=CONT_UP)
    if not emu.r8(run_ + RUN_HUD_HIDDEN):
        emu.fail("the binding on controller 2 does not fire with the free camera off")
    print("freecam: the controller 2 binding fires with the camera off")
