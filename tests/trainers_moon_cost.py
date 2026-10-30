"""The cost of the Moon Pound aim (practice/include/trainers.h) in a level with many faces.

The glove jumps over a grid of spots, with the stick moved on every frame (so that no slam is reused from the frame
before). The aim has a budget of about 4 ms of the CPU counter for each
game frame (moonaim.c, STEP_COUNTS) and builds its buffer of faces over several game frames. MoonView.counts, the work of
each frame (it also counts other threads' time), stays under a ceiling; the view says how many faces and candidates the
aim worked with.

MOON_COST_LEVEL=n picks the level (default LEVEL). The counter also runs while other threads do: some levels (12, 15) show
spikes of 30 ms that are not the aim's. TRAINERS_DEBUG=1 prints every frame."""
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "tools", "tricks"))
from trainers import ACTOR_POS, RUN_TRAINERS, TR_MOON, Rig  # noqa: E402
from trainers2 import read_view  # noqa: E402
from trainers_moon import TIP, boot_to_hub, put_hand, step_len  # noqa: E402

LEVEL = 1             # the first hub: the dome of the Space portal has the most faces of the levels that were tried (91 in reach)
CEILING = 250000      # CPU counter ticks (46.875 MHz): the budget of 187500 and the time of one small step
MANY_FACES = 80
SPOTS = [(0, 0), (40, 0), (-40, 40), (0, -60), (70, 70), (-80, -80), (20, 120), (0, 0), (120, 0), (-30, -20)]


def cost_check(r):
    emu = r.emu
    emu.w8(r.run + RUN_TRAINERS, TR_MOON)
    for _ in range(60):
        yield from r.next_frame()
    home = r.vec(r.hand + ACTOR_POS) if LEVEL != 1 or os.environ.get('MOON_COST_LEVEL') else (TIP[0], TIP[1], TIP[2])
    worst = dict(counts=0, faces=0, cands=0, flags=0)
    seen = built = 0
    for dx, dz in SPOTS:
        pos = (home[0] + dx, home[1] + 60.0, home[2] + dz)
        for k in range(30):
            if k % 8 == 0:
                put_hand(r, pos, (0.0, 4.0, 0.0))
            # The stick moves the glove off the path that the last frame predicted: the slams cannot be reused.
            yield from step_len(r, (), x=(40 if k % 2 else -40), y=(30 if k % 3 else -30))
            v = read_view(r)["moon"]
            if os.environ.get("TRAINERS_DEBUG"):
                print("cost", k, v["live"], v["why"], "counts", v["counts"], "faces", v["faces"], "cands", v["cands"],
                      "flags", v["flags"], "builds", v["rebuilds"], "sims", v["sims"], "calls", v["calls"], "iters", v["iters"], "strip", v["strip_counts"])
            if not v["live"]:
                continue
            seen += 1
            for key in ("counts", "faces", "cands"):
                worst[key] = max(worst[key], v[key])
            worst["flags"] |= v["flags"]
            built = v["rebuilds"]
    print(f"trainers2: cost over {seen} frames in level {os.environ.get('MOON_COST_LEVEL', LEVEL)}: at most {worst['counts']} ticks "
          f"({worst['counts'] / 46875:.2f} ms), {worst['faces']} faces, {worst['cands']} candidates, flags "
          f"{worst['flags']:#x}, {built} builds")
    if seen < 100:
        emu.fail(f"the aim was live on only {seen} frames")
    if worst["faces"] < MANY_FACES:
        emu.fail(f"the check needs a level with many faces: the buffer held {worst['faces']}")
    if worst["counts"] > CEILING:
        emu.fail(f"the aim took {worst['counts']} ticks in one game frame, over the ceiling of {CEILING}")


def run(emu, sym, args):
    r = Rig(emu, sym)
    yield from boot_to_hub(emu, sym, int(os.environ.get("MOON_COST_LEVEL", LEVEL)))
    yield from cost_check(r)
