"""The HUD's layout (practice/src/hud.c, the default places in practice/src/settings.c) in Atlantis 1. The HUD notes
the place of each item and note on the last frame that drew it (hudBoxes), with the text's shadow.

1. Every HUD item on at its default place, with long values: an hour-long full-game timer and a long split, the
   random number seed with its call count, the glove and the ball at -1000.5, four watches, four presses in the
   input history with large frame numbers, the missed taps line. Every note: MODIFIED with three reasons, frame
   advance, the free camera, a recording and then a playback, a message. No two of them overlap, none overlaps the
   game's own HUD (the places below, measured in Atlantis 1 and in a level with a countdown), and all are at least
   16 pixels from the screen's edges.
2. Every item at the far corners of the Layout menu (x 160, y 120 and x 0, y 0) is drawn inside the same area.
3. MODIFIED shows for a run that does not count but has no reason word (Freeze random numbers) and goes at the next
   level start. The lag counter starts again at each level start.
4. The input history notes nothing while the menu is open and drops the chord of the four C buttons; the frame of
   a press is the in-game timer's frame."""
import struct

import layout
import trace
from tools import C_ALL, press, wait_for

SAFE = (16, 16, 304, 224)  # x0, y0, x1, y1: the part of the 320 by 240 picture that every TV shows
# The game's own HUD, x0, y0, x1, y1 (not included), with a few pixels to spare.
GAME_HUD = {
    "the star card and the garib count": (20, 8, 120, 42),
    "the score (five digits)": (226, 12, 302, 42),
    "the camera icon": (279, 9, 293, 21),
    "a level's countdown": (132, 10, 188, 57),
    "the hearts": (20, 198, 74, 218),
    "the lives": (228, 194, 296, 224),
}
NAMES = ["timer", "input display", "glove", "ball", "glove action", "frame counter", "lag counter", "random seed",
         "last level time", "full-game timer", "watch list", "MODIFIED", "free camera", "frame advance",
         "recording", "message"]
BOX_SIZE = 10  # HudBox (practice/include/hud.h)
DRAW_FRAMES = 0x04  # Practice.drawFrames
RUN_FRAME_ADVANCE, RUN_RNG_FROZEN, RUN_MOON_JUMP, RUN_NOCLIP = 0x00, 0x0A, 0x33, 0x34
RUN_MISSED_TAPS, RUN_CHAOS, RUN_FREE_CAM = 0x44, 0x45, 0x46
FREECAM_ACTIVE = 0x3C
REC_RECORDING, REC_PLAYING, REC_COUNT, REC_FRAME = 0x00, 0x01, 0x04, 0x08
GT_RUNNING, GT_SPLIT_COUNT, GT_ELAPSED, GT_SPLITS = 0x00, 0x02, 0x04, 0x08
ACTOR_POS = 0x34
PLAYER_LEVEL_FRAMES = 0x18
SETTINGS_INPUT_HISTORY = 0x09
PRESS_SIZE, PRESS_HISTORY = 8, 4  # Press: bit, holding, frames, frame
CONT_A = 0x8000
BT_TAINT = 0x01
ACTION_SAVE_STATE = 8


def overlap(a, b):
    return a[0] < b[2] and b[0] < a[2] and a[1] < b[3] and b[1] < a[3]


def write(emu, a, data):
    for i, b in enumerate(data):
        emu.w8(a + i, b)


def run(emu, sym, args):
    gi = sym("gameInfo")
    fc = lambda: emu.r32(sym("frameCount"))  # noqa: E731
    mode = lambda: emu.r8(gi)  # noqa: E731
    level = lambda: emu.r8(gi + 1)  # noqa: E731
    run_ = sym("run")
    settings = sym("settings")
    hud = settings + layout.SETTINGS_HUD
    presses = sym("presses")

    def boxes():
        """The boxes drawn on one of the last three frames, by name."""
        now = emu.r32(sym("practice") + DRAW_FRAMES) & 0xFFFF
        found = {}
        for i, name in enumerate(NAMES):
            x0, y0, x1, y1, frame = struct.unpack(">hhhhH", bytes(emu.read(sym("hudBoxes") + BOX_SIZE * i, BOX_SIZE)))
            if (now - frame) & 0xFFFF <= 3:
                found[name] = (x0, y0, x1, y1)
        return found

    def check(found, what):
        for name, b in found.items():
            if b[0] < SAFE[0] or b[1] < SAFE[1] or b[2] > SAFE[2] or b[3] > SAFE[3]:
                emu.fail(f"{what}: the {name} at {b} is outside the safe area {SAFE}")
            for game, g in GAME_HUD.items():
                if overlap(b, g):
                    emu.fail(f"{what}: the {name} at {b} overlaps {game} at {g}")
        names = sorted(found)
        for i, a in enumerate(names):
            for b in names[i + 1:]:
                if overlap(found[a], found[b]):
                    emu.fail(f"{what}: the {a} at {found[a]} overlaps the {b} at {found[b]}")

    def history():
        out = []
        for i in range(PRESS_HISTORY):
            bit, holding, frames, frame = struct.unpack(">HBBi", bytes(emu.read(presses + PRESS_SIZE * i, PRESS_SIZE)))
            if bit:
                out.append((bit, frame))
        return out

    yield from wait_for(emu, lambda: level() == trace.PRESENT and fc() >= 200, 4000, "the logos")
    yield from press(emu, "START", frames=6)
    yield from wait_for(emu, lambda: level() == trace.FLYTHRU and mode() == 4 and fc() >= 30, 3000, "the title")
    yield from press(emu, *C_ALL)
    for b in ("A", "DOWN", "DOWN", "A"):  # Warp, Go
        yield from press(emu, b)
    yield from wait_for(emu, lambda: level() == trace.ATLANTIS1 and mode() == 4 and fc() >= 100, 3000, "Atlantis 1")
    default = bytes(emu.read(hud, layout.HUD_PLACE_SIZE * layout.HUD_COUNT))

    # 4. The input history.
    emu.w8(hud + layout.HUD_PLACE_SIZE * 1, 1)  # the input display
    emu.w8(settings + SETTINGS_INPUT_HISTORY, 1)
    yield from press(emu, "A")
    before = history()
    if not before or before[0][0] != CONT_A:
        emu.fail(f"A was not noted in the input history: {before}")
    timer = emu.r32(sym("player") + PLAYER_LEVEL_FRAMES)
    if not 0 < timer - before[0][1] <= 3:
        emu.fail(f"the press of A is at frame {before[0][1]}, the in-game timer is at {timer}")
    for c in C_ALL:  # one C button after another: the chord is made on the last one
        emu.hold(*C_ALL[:C_ALL.index(c) + 1])
        yield 8
    emu.release()
    yield 8
    if not emu.r8(sym("menuOpen")):
        emu.fail("the menu did not open")
    yield from press(emu, "DOWN")
    yield from press(emu, "UP")
    yield from press(emu, *C_ALL)
    if emu.r8(sym("menuOpen")):
        emu.fail("the menu did not close")
    after = history()
    if after != before:
        emu.fail(f"the menu changed the input history: {before} before, {after} after")
    print(f"hudlayout: the input history keeps {after} through the menu; a press's frame is the in-game timer's")

    # 3. MODIFIED with no reason word, and the lag counter.
    emu.w8(sym("bestTimesState") + BT_TAINT, 0)
    yield 10
    if "MODIFIED" in boxes():
        emu.fail("MODIFIED shows with the run not tainted")
    emu.w8(run_ + RUN_RNG_FROZEN, 1)
    yield 20
    if "MODIFIED" not in boxes():
        emu.fail("MODIFIED does not show with the random numbers frozen")
    emu.w8(run_ + RUN_RNG_FROZEN, 0)
    emu.w32(sym("lagFrames"), 5000)
    yield from press(emu, "UP", x=0, y=80)  # reset the level
    yield from wait_for(emu, lambda: fc() < 20, 2000, "the level to restart")
    yield from wait_for(emu, lambda: level() == trace.ATLANTIS1 and mode() == 4 and fc() >= 60, 3000, "Atlantis 1")
    if "MODIFIED" in boxes():
        emu.fail("MODIFIED stays after the level started again")
    if emu.r32(sym("lagFrames")) >= 5000:
        emu.fail("the lag counter did not start again with the level")
    print("hudlayout: MODIFIED shows while the run is tainted with no reason word, and goes at the level start")

    # 1. Everything on, with long values.
    for i in range(layout.HUD_COUNT):
        emu.w8(hud + layout.HUD_PLACE_SIZE * i, 1)
    yield from press(emu, "UP", x=0, y=80)  # a reset: the last level time and the seed's call count
    yield from wait_for(emu, lambda: fc() < 20, 2000, "the level to restart")
    yield from wait_for(emu, lambda: level() == trace.ATLANTIS1 and mode() == 4 and fc() >= 60, 3000, "Atlantis 1")
    for i in range(4):
        emu.w32(sym("watches") + 4 * i, 0x80000400 + 4 * i)
    emu.w8(run_ + RUN_MISSED_TAPS, 1)
    emu.w8(run_ + RUN_FREE_CAM, 1)
    emu.w8(run_ + RUN_NOCLIP, 1)
    emu.w8(run_ + RUN_MOON_JUMP, 1)
    emu.w8(run_ + RUN_CHAOS, 1)
    emu.w32(sym("player") + PLAYER_LEVEL_FRAMES, 65000)  # long frame numbers in the timer and the history
    for _ in range(PRESS_HISTORY):
        yield from press(emu, "CLEFT")
    emu.w16(sym("requests"), emu.r16(sym("requests")) | 1 << ACTION_SAVE_STATE)  # its message
    yield 20
    if not emu.r8(sym("freeCam") + FREECAM_ACTIVE):
        emu.fail("the free camera is not placed")
    emu.w8(run_ + RUN_FRAME_ADVANCE, 1)
    yield 10
    gt = sym("gameTimer")
    emu.w8(gt + GT_RUNNING, 1)
    emu.w32(gt + GT_ELAPSED, 2000000)  # 9 hours
    emu.w8(gt + GT_SPLIT_COUNT, 12)
    emu.w32(gt + GT_SPLITS + 4 * 10, 100)
    emu.w32(gt + GT_SPLITS + 4 * 11, 1999000)
    for actor in ("hand", "ball"):
        for k in range(3):
            emu.wf(sym(actor) + ACTOR_POS + 4 * k, -1000.5)
    rec = sym("recording")
    emu.w32(rec + REC_COUNT, 23999)
    emu.w8(rec + REC_RECORDING, 1)
    yield 6
    found = boxes()
    emu.w8(rec + REC_RECORDING, 0)
    emu.w32(rec + REC_COUNT, 24000)
    emu.w32(rec + REC_FRAME, 23999)
    emu.w8(rec + REC_PLAYING, 1)
    yield 6
    playing = boxes()
    emu.w8(rec + REC_PLAYING, 0)
    emu.w32(rec + REC_COUNT, 0)
    emu.w32(rec + REC_FRAME, 0)
    missing = [n for n in NAMES if n not in found]
    if missing:
        emu.fail(f"not drawn: {missing}")
    check(found, "everything on")
    check(dict(found, recording=playing["recording"]), "everything on, with a playback")
    emu.fb_png("hudlayout", sym("cfb_16_a"))
    emu.fb_png("hudlayout1", sym("cfb_16_b"))
    print("hudlayout: every item and note with long values:")
    for name in NAMES:
        print(f"  {name:18s} {found[name]}")

    # 2. The far corners of the Layout menu.
    for corner in ((160, 120), (0, 0)):
        for i in range(layout.HUD_COUNT):
            emu.w8(hud + layout.HUD_PLACE_SIZE * i + 1, corner[0])
            emu.w8(hud + layout.HUD_PLACE_SIZE * i + 2, corner[1])
        yield 6
        found = boxes()
        for name, b in found.items():
            if b[0] < SAFE[0] or b[1] < SAFE[1] or b[2] > SAFE[2] or b[3] > SAFE[3]:
                emu.fail(f"with every item at {corner}, the {name} at {b} is outside the safe area")
        print(f"hudlayout: with every item at x {corner[0]}, y {corner[1]} each stays inside the safe area")
    write(emu, hud, default)
    for off in (RUN_MISSED_TAPS, RUN_FREE_CAM, RUN_NOCLIP, RUN_MOON_JUMP, RUN_CHAOS, RUN_FRAME_ADVANCE):
        emu.w8(run_ + off, 0)
    yield 20
