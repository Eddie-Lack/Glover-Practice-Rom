"""The hole painter in the collision view (practice/include/moonaim.h, Run.collHoles) at the hub's Space portal.

1. With the view and the painter on and the glove near the dome, the posts that the view draws are the model's hole
   candidates (tools/tricks/cone.py, over the faces read from RAM) within 72 units of the glove, and the dome's tip is
   one of them. The list moves with the glove: after the glove moves 150 units it holds the model's candidates there.
2. The painter costs the game thread at most 3 ms of the CPU counter and a build takes several game frames.
3. With the painter off no post is drawn; with the view off the game thread's painter step does not run.

Pictures: collholes.png (needs --gfx angrylion)."""
import math
import os
import struct
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "tools", "tricks"))
import cone  # noqa: E402
from trainers import Rig, step  # noqa: E402
from trainers_moon import TIP, boot_to_hub, put_hand, read_faces, step_len  # noqa: E402

RUN_COLL_VIEW = 0x2C
RUN_COLL_HOLES = 0x4C        # Run.collHoles (practice/include/actions.h), after warpPower
STATS_HOLES = 0x0A           # CollStats.holes (practice/include/collview.h)
HOLE_VIEW_SIZE = 4 + 64 * 12


def published(r):
    e, sym = r.emu, r.sym
    seq = e.r32(sym("holeSeq"))
    raw = bytes(e.read(sym("holeViews") + (seq & 1) * HOLE_VIEW_SIZE, HOLE_VIEW_SIZE))
    count, capped = raw[0], raw[1]
    pts = [struct.unpack_from(">3f", raw, 4 + 12 * i) for i in range(count)]
    return seq, count, capped, pts


def model_points(cands, centre, reach=72.0):
    return [(c["v"][0] + c["centre"][0], c["v"][1], c["v"][2] + c["centre"][1]) for c in cands
            if math.hypot(c["v"][0] - centre[0], c["v"][2] - centre[2]) <= reach]


def match(rom, want, tol=0.05):
    """The points of WANT with no point of ROM within TOL, and the points of ROM with none in WANT."""
    miss = [w for w in want if not any(math.dist(w, p) < tol for p in rom)]
    extra = [p for p in rom if not any(math.dist(w, p) < tol for w in want)]
    return miss, extra


def violet_pixels(path):
    from PIL import Image
    return sum(1 for (rr, g, b) in Image.open(path).convert("RGB").getdata() if b > 200 and 130 < rr < 220 and g < 130)


def hold_at(r, pos, frames):
    for _ in range(frames):
        put_hand(r, pos, (0.0, 0.0, 0.0))
        yield from step_len(r, ())
    put_hand(r, pos, (0.0, 0.0, 0.0))


def run(emu, sym, args):
    r = Rig(emu, sym)
    yield from boot_to_hub(emu, sym)
    for _ in range(40):
        yield from r.next_frame()
    for _ in range(300):
        if emu.r16s(sym("handControl") + 0x24) <= 0:
            break
        yield from r.next_frame()
    faces = read_faces(r, (TIP[0], TIP[2]), 320)
    cands = cone.candidates(faces)
    print(f"collholes: hub {len(faces)} faces within 320 units of the dome, {len(cands)} hole candidates in the model")
    stats = sym("collStats")
    emu.w8(r.run + RUN_COLL_VIEW, 1)
    yield from r.next_frame()
    seq0 = emu.r32(sym("holeSeq"))
    # The view is on and the painter off: nothing runs, nothing is drawn.
    pos = (TIP[0] + 12.0, TIP[1] + 40.0, TIP[2] + 9.0)
    yield from hold_at(r, pos, 20)
    if emu.r32(sym("holeSeq")) != seq0 or emu.r16(stats + STATS_HOLES) != 0:
        emu.fail("the painter ran or drew with its switch off")
    emu.w8(r.run + RUN_COLL_HOLES, 1)
    worst_frames = 0
    for n in range(40):
        put_hand(r, pos, (0.0, 0.0, 0.0))
        yield from step_len(r, ())
        seq, count, capped, pts = published(r)
        if seq != seq0 and count:
            worst_frames = n + 1
            break
    if not worst_frames:
        emu.fail("the painter published no hole in 40 game frames")
    print(f"collholes: the first list came after {worst_frames} game frames")
    yield from hold_at(r, pos, 6)
    seq, count, capped, pts = published(r)
    want = model_points(cands, pos)
    miss, extra = match(pts, want)
    print(f"collholes: at the dome: {count} holes marked, {len(want)} in the model within 72 units; "
          f"missing {len(miss)}, extra {len(extra)}, capped {capped}")
    if miss or extra or count != len(want):
        emu.fail(f"the marked holes differ from the model: missing {miss[:4]}, extra {extra[:4]}")
    if not any(math.dist(p, TIP) < 1.5 for p in pts):
        emu.fail("the dome's tip is not marked")
    drawn = emu.r16(stats + STATS_HOLES)
    print(f"collholes: the view drew {drawn} posts, {emu.r16(stats)} faces, mode {emu.r8(r.run + RUN_COLL_VIEW)}")
    if drawn < count - 2 or drawn == 0:
        emu.fail(f"the view drew {drawn} posts for {count} holes")
    if args.gfx == "angrylion":
        # The glove above the dome, the camera turned to the tip: violet pixels (the posts) are in a frame buffer.
        best = 0
        for pic, yaw in (((TIP[0] + 60.0, TIP[1] + 20.0, TIP[2]), -1.57), ((TIP[0] + 40.0, TIP[1] + 60.0, TIP[2] + 40.0), -2.356)):
            for _ in range(16):
                put_hand(r, pic, (0.0, 0.0, 0.0))
                r.hold_camera(yaw)
                yield from step_len(r, ())
            r.picture("collholes")
            best = max(best, violet_pixels(os.path.join(args.out, "collholes.png")))
        print(f"collholes: {best} violet pixels in the picture")
        if best < 10:
            emu.fail("the posts are not in the picture")
    # The list follows the glove.
    pos2 = (TIP[0] + 150.0, TIP[1] + 40.0, TIP[2] - 60.0)
    yield from hold_at(r, pos2, 30)
    seq2, count2, capped2, pts2 = published(r)
    want2 = model_points(cands, pos2)
    miss, extra = match(pts2, want2)
    print(f"collholes: 150 units away: {count2} holes marked, {len(want2)} in the model; missing {len(miss)}, extra {len(extra)}")
    if seq2 == seq or miss or extra:
        emu.fail(f"the list did not follow the glove: missing {miss[:4]}, extra {extra[:4]}")
    last, worst = emu.r32(sym("holeLast")), emu.r32(sym("holeWorst"))
    print(f"collholes: painter step: last {last} counts, longest {worst} counts ({worst / 46875:.2f} ms)")
    if worst > 140000:
        emu.fail(f"a painter step took {worst} counts")
    # Off again, and the view off.
    emu.w8(r.run + RUN_COLL_HOLES, 0)
    yield from hold_at(r, pos, 6)
    if emu.r16(stats + STATS_HOLES) != 0:
        emu.fail("posts are drawn with the painter off")
    emu.w8(r.run + RUN_COLL_HOLES, 1)
    emu.w8(r.run + RUN_COLL_VIEW, 0)
    yield from r.next_frame()
    seq3 = emu.r32(sym("holeSeq"))
    yield from hold_at(r, pos, 30)
    if emu.r32(sym("holeSeq")) != seq3:
        emu.fail("the painter ran with the collision view off")
    print("collholes: off with its switch and with the view")
