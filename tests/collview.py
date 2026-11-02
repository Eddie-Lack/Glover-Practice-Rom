"""The collision view in Atlantis 1. With the view on (see-through, then on surfaces) and the spheres on, the
frame shows faces and spheres, and the game goes on while Glover walks about. Writes see0.png and see1.png, and
surf0.png and surf1.png (the two frame buffers; needs --gfx angrylion)."""
import trace
from tools import C_ALL, press, wait_for

RUN_COLL_VIEW = 0x2C  # Run.collView, then collRadius and collSpheres (practice/include/actions.h)
STATS_FACES = 0x0     # CollStats (practice/include/collview.h)
STATS_SPHERES = 0x2


def run(emu, sym, args):
    gi = sym("gameInfo")
    fc = lambda: emu.r32(sym("frameCount"))  # noqa: E731
    mode = lambda: emu.r8(gi)  # noqa: E731
    level = lambda: emu.r8(gi + 1)  # noqa: E731
    run_addr = sym("run")
    stats = sym("collStats")

    yield from wait_for(emu, lambda: level() == trace.PRESENT and fc() >= 200, 4000, "the logos")
    yield from press(emu, "START", frames=6)
    yield from wait_for(emu, lambda: level() == trace.FLYTHRU and mode() == 4 and fc() >= 30, 3000, "the title")
    yield from press(emu, *C_ALL)
    for b in ("A", "DOWN", "DOWN", "A"):
        yield from press(emu, b)
    yield from wait_for(emu, lambda: level() == trace.ATLANTIS1 and mode() == 4 and fc() >= 100, 3000, "Atlantis 1")

    for view, name in ((1, "see"), (2, "surf")):
        emu.w8(run_addr + RUN_COLL_VIEW, view)
        emu.w8(run_addr + RUN_COLL_VIEW + 2, 1)
        start = fc()
        most = spheres_seen = 0
        # Walk about: the stick round in a circle, a few seconds.
        for i in range(300):
            x, y = [(0, 80), (80, 0), (0, -80), (-80, 0)][i // 75]
            emu.hold(x=x, y=y)
            most = max(most, emu.r16(stats + STATS_FACES))
            spheres_seen = max(spheres_seen, emu.r16(stats + STATS_SPHERES))
            yield 1
        emu.release()
        faces, spheres = emu.r16(stats + STATS_FACES), emu.r16(stats + STATS_SPHERES)
        print(f"collview {name}: {faces} faces and {spheres} spheres now, at most {most} faces; "
              f"game frames {start} to {fc()}")
        # More faces than Glover and the ball can touch (8): the landscape and the platforms are read.
        if most < 20 or spheres_seen == 0:
            emu.fail(f"the collision view drew {most} faces and {spheres_seen} spheres in mode {name}")
        if fc() < start + 30:
            emu.fail(f"the game did not go on with the collision view ({start} to {fc()})")
        if args.gfx == "angrylion":
            emu.fb_png(f"{name}0", sym("cfb_16_a"))
            emu.fb_png(f"{name}1", sym("cfb_16_b"))
    emu.w8(run_addr + RUN_COLL_VIEW, 0)
    start = fc()
    yield 60
    if fc() <= start:
        emu.fail("the game stopped after the collision view was turned off")
