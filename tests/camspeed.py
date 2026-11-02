"""Camera speed (practice/include/camspeed.h) in Atlantis 1: how far the camera falls behind Glover.

The ball runs straight away from the camera for 110 game frames, with the camera free (it follows as the game makes it
follow). After each game frame the glove, the ball, the camera and the camera's target point are all moved back by the
ball's move, so the level's start floor stays under the ball and the camera sees the same ground. The lag is the
horizontal distance from the camera to the ball, minus the distance at rest. Two drives:

- mash: Glover holds the ball, the D-pad up and L every 2nd game frame (a full mash, about 10 units a game frame);
- fast: the ball's and the glove's velocity are set to 14 units a game frame (the top speed of a bounce) before each
  game frame.

For each setting (normal, fast, faster) the test prints the mean lag over game frames 60 to 109. It checks:

1. Normal: the camera follows as the game makes it (followSpeed never above 20, the camera body's maxSpeed 20), and two
   runs from the same state give the same camera positions on every frame (the hook changes nothing).
2. Fast and faster keep the camera closer than normal in both drives, faster closer than fast.
3. The setting taints the practice best time (the taint bit BT_OTHER) and the HUD draws the camera icon and not MODIFIED
   (the frame buffer: icon pixels at the place above the score's last digit, no MODIFIED text).

Pictures: camspeed_off.png, camspeed_fast.png (needs --gfx angrylion)."""
import math
import os
import struct
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "tools", "tricks"))
from trainers import (ACTOR_PLAT_PREV_POS, ACTOR_POS, ACTOR_PREV_POS, ACTOR_ROT, ACTOR_VEL, Rig,  # noqa: E402
                      boot_to_atlantis, get_joined, step)

RUN_CAMERA_SPEED = 0x4D
RUN_NOCLIP = 0x34
CAM_INFO_ACTOR = 0x04          # CameraInfo.actor
CAM_INFO_BLOCKED, CAM_INFO_FOLLOW_SPEED = 0x1A8, 0x174
ACTOR_PHYS = 0xD0
PHYS_MAX_SPEED = 0x08
NAMES = ["normal", "fast", "faster"]
FRAMES, MEASURE_FROM = 110, 60
BT_TAINT, BT_OTHER = 1, 0x80   # BestTimesState.taint and its bit for a camera speed
ICON_BOX = (279, 9, 293, 23)   # x0, y0, x1, y1: the camera icon (hud.c: x 281 to 291, y 11 to 20)
MODIFIED_BOX = (180, 14, 279, 23)   # the MODIFIED text is right aligned to x 316; its left part is clear of the icon


def icon_pixels(emu, sym):
    """(pixels of the icon's glass in the icon's place, pixels of the MODIFIED text's red left of it) in the frame buffer
    that holds the newer picture."""
    colours = [len(set(bytes(emu.read(sym(("cfb_16_a", "cfb_16_b")[b]), 320 * 240 * 2))[::2])) for b in (0, 1)]
    raw = bytes(emu.read(sym(("cfb_16_a", "cfb_16_b")[colours.index(max(colours))]), 320 * 240 * 2))

    def count(box, want):
        n = 0
        for y in range(box[1], box[3]):
            for x in range(box[0], box[2]):
                v = (raw[(y * 320 + x) * 2] << 8) | raw[(y * 320 + x) * 2 + 1]
                if want((v >> 11) & 31, (v >> 6) & 31, (v >> 1) & 31):
                    n += 1
        return n
    glass = count(ICON_BOX, lambda r, g, b: 8 <= r <= 14 and 17 <= g <= 22 and b >= 28)       # 0x60A0FF
    red = count(MODIFIED_BOX, lambda r, g, b: r >= 28 and 7 <= g <= 13 and 7 <= b <= 13)      # 0xFF5050
    return glass, red


def horizontal(a, b):
    return math.hypot(a[0] - b[0], a[2] - b[2])


def read_vec(emu, a):
    return [emu.rf(a + 4 * i) for i in range(3)]


def lag_run(r, mode, drive, record=False):
    """Returns (the mean lag over the measured frames, the rows of the run)."""
    emu, sym = r.emu, r.sym
    cam, info = r.cam, sym("cameraInfo")
    emu.w8(r.run + RUN_CAMERA_SPEED, mode)
    rows = []
    home = None
    rest = None
    for i in range(FRAMES):
        yaw = emu.rf(cam + ACTOR_ROT + 4)
        if drive == "fast":
            for a in (r.ball, r.hand):
                r.setvec(a + ACTOR_VEL, [14.0 * math.sin(yaw), r.vec(a + ACTOR_VEL)[1], 14.0 * math.cos(yaw)])
        buttons = ("UP", "L") if drive == "mash" and i % 2 == 0 else ("UP",)
        yield from step(r, buttons)
        ball = r.vec(r.ball + ACTOR_POS)
        camp = r.vec(cam + ACTOR_POS)
        target = r.vec(info + CAM_INFO_ACTOR + ACTOR_POS)
        if home is None:
            home = list(ball)
        if rest is None:
            rest = horizontal(camp, ball)
        rows.append(dict(lag=horizontal(camp, ball) - rest, target_lag=horizontal(target, ball),
                         follow=emu.rf(info + CAM_INFO_FOLLOW_SPEED), maxspeed=emu.rf(emu.r32(cam + ACTOR_PHYS) + PHYS_MAX_SPEED),
                         blocked=emu.r32(info + CAM_INFO_BLOCKED), cam=camp, ball=ball, hand=r.vec(r.hand + ACTOR_POS), timer=emu.r16(info + 0x178),
                         focus=emu.r32(info + 0x100), tpos=target,
                         speed=horizontal(ball, home)))
        dx, dz = home[0] - ball[0], home[2] - ball[2]
        for base in (r.ball, r.hand, cam, info + CAM_INFO_ACTOR):
            for off in (ACTOR_POS, ACTOR_PREV_POS, ACTOR_PLAT_PREV_POS):
                for k, d in ((0, dx), (2, dz)):
                    emu.wf(base + off + 4 * k, emu.rf(base + off + 4 * k) + d)
    emu.release()
    mean = sum(row["lag"] for row in rows[MEASURE_FROM:]) / (FRAMES - MEASURE_FROM)
    return mean, rows


def run(emu, sym, args):
    r = Rig(emu, sym)
    yield from boot_to_atlantis(emu, sym)
    yield 60
    yield from get_joined(r)
    for _ in range(200):               # the camera settles behind the ball
        yield from r.next_frame()
    base = emu.save("camspeed_base")
    yield 3
    results = {}
    for drive in ("mash", "fast"):
        for mode in (0, 0, 1, 2):
            emu.hold()
            emu.load(base)
            yield 3
            mean, rows = yield from lag_run(r, mode, drive)
            if os.environ.get('CAM_DEBUG'):
                for i, row in enumerate(rows[::6]):
                    print(i, 'hand', [round(v, 1) for v in row['hand']], 'target', [round(v, 1) for v in row['tpos']], 'timer', row['timer'], 'follow', round(row['follow'], 1), hex(row['focus']), [round(v, 1) for v in row['ball']], [round(v, 1) for v in row['cam']], round(row['lag'], 1), round(row['target_lag'], 1), round(row['speed'], 2))
            speed = sum(row["speed"] for row in rows[MEASURE_FROM:]) / (FRAMES - MEASURE_FROM)
            top = max(row["lag"] for row in rows)
            blocked = sum(1 for row in rows if row["blocked"])
            print(f"camspeed: {drive:4s} {NAMES[mode]:6s}: mean lag {mean:6.1f} (largest {top:6.1f}), ball moves {speed:5.2f} a frame, "
                  f"target {sum(row['target_lag'] for row in rows[MEASURE_FROM:]) / (FRAMES - MEASURE_FROM):5.1f} behind, "
                  f"follow speed {max(row['follow'] for row in rows):5.1f}, camera maxSpeed {max(row['maxspeed'] for row in rows):5.1f}, "
                  f"blocked on {blocked} frames")
            if mode == 0 and (drive, 0) in results:
                first = results[(drive, 0)][1]
                same = all(a["cam"] == b["cam"] and a["ball"] == b["ball"] for a, b in zip(first, rows))
                if not same:
                    emu.fail(f"{drive}: two runs of normal from the same state differ")
                print(f"camspeed: {drive}: two runs of normal from the same state give the same camera on all {FRAMES} frames")
            results.setdefault((drive, mode), (mean, rows))
            if mode == 0 and max(row["follow"] for row in rows) > 20.001 or mode == 0 and max(row["maxspeed"] for row in rows) > 20.001:
                emu.fail("with the camera at normal a limit of the game was raised")
        m = [results[(drive, k)][0] for k in range(3)]
        if not (m[0] > m[1] + 0.5 and m[1] > m[2] + 0.5):
            emu.fail(f"{drive}: the lag does not fall with the setting: {m}")
    emu.w8(r.run + RUN_CAMERA_SPEED, 0)
    # The taint, the icon and the MODIFIED text.
    bt = sym("bestTimesState")
    emu.hold()
    emu.load(base)
    yield 3
    for _ in range(6):
        yield from r.next_frame()
    if emu.r8(bt + BT_TAINT) & BT_OTHER:
        emu.fail("the run is tainted with the camera at normal")
    glass_off, modified_off = icon_pixels(emu, sym)
    r.picture("camspeed_off")
    emu.w8(r.run + RUN_CAMERA_SPEED, 1)
    for _ in range(6):
        yield from r.next_frame()
    if not emu.r8(bt + BT_TAINT) & BT_OTHER:
        emu.fail("a camera speed did not taint the run")
    glass_on, modified_on = icon_pixels(emu, sym)
    r.picture("camspeed_fast")
    if glass_off or glass_on < 6 or modified_on or modified_off:
        emu.fail(f"the camera icon's glass: {glass_off} pixels with the setting off, {glass_on} on; MODIFIED text pixels {modified_off}, {modified_on}")
    print(f"camspeed: a camera speed taints the run (BT_OTHER); the HUD draws the icon ({glass_on} glass pixels, none with it off) and no MODIFIED text")
    # Another modified feature still shows MODIFIED, with the icon.
    emu.w8(r.run + RUN_NOCLIP, 1)
    for _ in range(6):
        yield from r.next_frame()
    glass_both, modified_both = icon_pixels(emu, sym)
    emu.w8(r.run + RUN_NOCLIP, 0)
    emu.w8(r.run + RUN_CAMERA_SPEED, 0)
    if glass_both < 6 or modified_both < 20:
        emu.fail(f"with noclip on as well the HUD draws {glass_both} icon pixels and {modified_both} MODIFIED pixels")
    print(f"camspeed: with noclip on too, MODIFIED shows ({modified_both} pixels) and so does the icon")
