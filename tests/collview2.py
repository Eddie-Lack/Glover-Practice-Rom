"""The collision view's switches in Atlantis 1, read from the display lists that it builds
(practice/src/collview.c):

- Enemy spheres: the mallet enemy's spheres are drawn in its colour.
- Open edges: a list of pink bands is built for the meshes, and the frame's scene calls it.
- Back faces: "faint" draws the scene a second time with the front faces culled, "hide" once with the back faces
  culled, "same" once with no culling.
- Colours: by kind gives at most three colours, by object one for each actor.
- The cache is built again after a reset of the level after a warp and after a savestate load, and draws in the new level.
Writes enemies0/1.png and edges0/1.png (the two frame buffers; needs --gfx angrylion)."""
import struct
import trace
from tools import C_ALL, press, wait_for

RUN_WARP_LEVEL = 0x04
RUN_TELEPORT = 0x10      # Run.teleport, then teleportPos (practice/include/actions.h)
RUN_TELEPORT_POS = 0x14
RUN_COLL_VIEW = 0x2C     # collView, collRadius, collSpheres
RUN_COLL_ENEMIES = 0x3B  # collEnemies; collEdges is 2 further on
RUN_COLL_FACES = 0x52    # collFaces, collColour, collBack
ACTION_RESET, ACTION_WARP, ACTION_SAVE_STATE, ACTION_LOAD_STATE = 0, 1, 8, 9
# CollStats (practice/include/collview.h)
ST_FACES, ST_ACTORS, ST_SPHERES, ST_ENEMIES, ST_EDGES = 0x0, 0x2, 0x4, 0x6, 0x8
ST_STATE, ST_FULL, ST_SHAPES, ST_BUILDS, ST_COUNTS = 0x10, 0x11, 0x12, 0x1A, 0x1C
READY = 2
SHAPE_SIZE, NO_LIST = 44, 0xFFFF
SCENE_SIZE, EXTRA_SIZE = 896, 232
EDGE_RGBA = (0xFF, 0x20, 0xA0, 0xFF)
ENEMY_PRIM = 0xFF6030
GEOM_CULL_FRONT, GEOM_CULL_BACK = 0x1000, 0x2000


def run(emu, sym, args):
    gi = sym("gameInfo")
    fc = lambda: emu.r32(sym("frameCount"))  # noqa: E731
    mode = lambda: emu.r8(gi)  # noqa: E731
    level = lambda: emu.r8(gi + 1)  # noqa: E731
    run_addr = sym("run")
    stats = sym("collStats")
    stat = lambda o: emu.r16(stats + o)  # noqa: E731
    requests = sym("requests")

    def settle(n=3):
        start = fc()
        yield from wait_for(emu, lambda: fc() >= start + n, 200, f"{n} game frames")

    def ready():
        yield from wait_for(emu, lambda: emu.r8(stats + ST_STATE) == READY and stat(ST_FACES) > 0, 1200, "the cache")
        yield from settle()

    def commands(addr, size):
        """The commands of a list, up to its end: (w0, w1) pairs."""
        out = []
        for _ in range(size):
            w0, w1 = emu.r32(addr), emu.r32(addr + 4)
            addr += 8
            out.append((w0, w1))
            if w0 >> 24 == 0xB8:
                break
        return out

    def last_lists():
        """The scene and the extra list of the frame buffer drawn last (the other one is being built)."""
        b = 1 - (emu.r8(sym("draw_buffer")) & 1)
        return (commands(sym("scene") + b * SCENE_SIZE * 8, SCENE_SIZE),
                commands(sym("extra") + b * EXTRA_SIZE * 8, EXTRA_SIZE), b)

    def shot(name):
        if args.gfx == "angrylion":
            emu.fb_png(name + "0", sym("cfb_16_a"))
            emu.fb_png(name + "1", sym("cfb_16_b"))

    yield from wait_for(emu, lambda: level() == trace.PRESENT and fc() >= 200, 4000, "the logos")
    yield from press(emu, "START", frames=6)
    yield from wait_for(emu, lambda: level() == trace.FLYTHRU and mode() == 4 and fc() >= 30, 3000, "the title")
    yield from press(emu, *C_ALL)
    for b in ("A", "DOWN", "DOWN", "A"):
        yield from press(emu, b)
    yield from wait_for(emu, lambda: level() == trace.ATLANTIS1 and mode() == 4 and fc() >= 100, 3000, "Atlantis 1")

    # The mallet is the enemy at (182, 99, -1181); Glover lands 50 units above it.
    mallet = None
    al = sym("actorList")
    a = emu.r32(al)
    while a != al:
        if emu.r16s(a + 0x20) == 10:
            mallet = [emu.rf(a + 0x34 + 4 * i) for i in range(3)]
        a = emu.r32(a)
    if mallet is None:
        emu.fail("no mallet in Atlantis 1")
    for i in range(3):
        emu.wf(run_addr + RUN_TELEPORT_POS + 4 * i, mallet[i] + (50 if i == 1 else 0))
    emu.w8(run_addr + RUN_TELEPORT, 1)
    emu.w8(run_addr + RUN_COLL_VIEW, 1)
    yield from ready()
    builds = stat(ST_BUILDS)

    # 1. Enemy spheres.
    if stat(ST_ENEMIES):
        emu.fail(f"{stat(ST_ENEMIES)} enemy spheres with the switch off")
    emu.w8(run_addr + RUN_COLL_ENEMIES, 1)
    yield from settle()
    _, extra, _ = last_lists()
    prims = {w1 >> 8 for w0, w1 in extra if w0 >> 24 == 0xFA}
    print(f"collview2 enemies: {stat(ST_ENEMIES)} enemy spheres, {stat(ST_SPHERES)} spheres in all; "
          f"enemy colour set: {ENEMY_PRIM in prims}")
    if stat(ST_ENEMIES) == 0 or ENEMY_PRIM not in prims:
        emu.fail("the enemy spheres were not drawn")
    shot("enemies")
    emu.w8(run_addr + RUN_COLL_ENEMIES, 0)

    # 2. Open edges.
    if stat(ST_EDGES):
        emu.fail(f"{stat(ST_EDGES)} open edges with the switch off")
    emu.w8(run_addr + RUN_COLL_ENEMIES + 2, 1)
    yield from settle(6)
    shapes, cache_gfx = sym("shapes"), sym("cacheGfx")
    edge_lists = set()
    pink = 0
    for i in range(stat(ST_SHAPES)):
        start = emu.r16(shapes + i * SHAPE_SIZE + 36)
        if start == NO_LIST:
            continue
        edge_lists.add(cache_gfx + start * 8)
        for w0, w1 in commands(cache_gfx + start * 8, 2000):
            if w0 >> 24 == 0x04:
                n = (w0 >> 10) & 0x3F
                raw = emu.read(w1 | 0x80000000, n * 16)
                pink += sum(1 for k in range(n) if tuple(raw[k * 16 + 12:k * 16 + 16]) == EDGE_RGBA)
    scene, _, _ = last_lists()
    called = sum(1 for w0, w1 in scene if w0 >> 24 == 0x06 and (w1 | 0x80000000) in edge_lists)
    print(f"collview2 edges: {stat(ST_EDGES)} open edges drawn, {len(edge_lists)} edge lists, {pink} pink "
          f"vertices, {called} calls in the scene; full {emu.r8(stats + ST_FULL)}; {emu.r32(stats + ST_COUNTS)} counts")
    if stat(ST_EDGES) == 0 or not edge_lists or pink == 0 or called == 0:
        emu.fail("the open edges were not drawn")
    shot("edges")
    emu.w8(run_addr + RUN_COLL_ENEMIES + 2, 0)
    yield from settle()
    if stat(ST_EDGES):
        emu.fail("open edges were drawn after the switch was turned off")

    # 3. Back faces, see-through and on surfaces.
    for view in (1, 2):
        emu.w8(run_addr + RUN_COLL_VIEW, view)
        for back, name in ((0, "faint"), (1, "hide"), (2, "same")):
            emu.w8(run_addr + RUN_COLL_FACES + 2, back)
            yield from settle()
            scene, extra, b = last_lists()
            scene_addr = sym("scene") + b * SCENE_SIZE * 8
            calls = sum(1 for w0, w1 in extra if w0 >> 24 == 0x06 and (w1 | 0x80000000) == scene_addr)
            culls = [(w0 >> 24, w1) for w0, w1 in extra if w0 >> 24 in (0xB6, 0xB7) and w1 & 0x3000]
            want = {0: 2, 1: 1, 2: 1}[back]
            print(f"collview2 view {view} back faces {name}: the scene drawn {calls} times; culling {culls}")
            if calls != want:
                emu.fail(f"view {view}, back faces {name}: the scene was drawn {calls} times, not {want}")
            if back == 2 and any(op == 0xB7 for op, w1 in culls):
                emu.fail("back faces \"same\" culled faces")
            if back == 1 and (0xB7, GEOM_CULL_BACK) not in culls:
                emu.fail("back faces \"hide\" did not cull the back faces")
            if back == 0 and (0xB7, GEOM_CULL_FRONT) not in culls:
                emu.fail("back faces \"faint\" did not draw the back faces")
    emu.w8(run_addr + RUN_COLL_FACES + 2, 0)

    # 4. Colours.
    found = {}
    for colour, name in ((0, "terrain"), (1, "kind"), (2, "object")):
        emu.w8(run_addr + RUN_COLL_FACES + 1, colour)
        yield from settle()
        scene, _, _ = last_lists()
        found[name] = {w1 >> 8 for w0, w1 in scene if w0 >> 24 == 0xFA}
        print(f"collview2 colours by {name}: {len(found[name])} colours")
    emu.w8(run_addr + RUN_COLL_FACES + 1, 0)
    if len(found["kind"]) > 3 or len(found["object"]) < 6 or not found["terrain"]:
        emu.fail(f"the colour modes: {found}")

    # 5. A new cache after a reset of the level, and after a warp to Atlantis 2.
    emu.w16(requests, emu.r16(requests) | 1 << ACTION_RESET)
    yield from wait_for(emu, lambda: stat(ST_BUILDS) > builds, 3000, "a new build after the reset")
    yield from ready()
    print(f"collview2 reset: build {stat(ST_BUILDS)}, {stat(ST_FACES)} faces")
    builds = stat(ST_BUILDS)
    emu.w8(run_addr + RUN_WARP_LEVEL, trace.ATLANTIS1 + 1)
    emu.w16(requests, emu.r16(requests) | 1 << ACTION_WARP)
    yield from wait_for(emu, lambda: level() == trace.ATLANTIS1 + 1 and mode() == 4 and fc() > 30, 6000, "Atlantis 2")
    yield from ready()
    print(f"collview2 warp: build {stat(ST_BUILDS)}, {stat(ST_FACES)} faces, {stat(ST_ACTORS)} actors in Atlantis 2")
    if stat(ST_BUILDS) <= builds:
        emu.fail("the cache was not built again after the warp")

    # 6. A new cache after a savestate load.
    start = fc()
    yield from wait_for(emu, lambda: fc() >= start + 60, 1200, "60 game frames")
    emu.w16(requests, emu.r16(requests) | 1 << ACTION_SAVE_STATE)
    yield from wait_for(emu, lambda: emu.r8(sym("stateSaved")), 600, "the state to be saved")
    builds = stat(ST_BUILDS)
    emu.w16(requests, emu.r16(requests) | 1 << ACTION_LOAD_STATE)
    yield from wait_for(emu, lambda: stat(ST_BUILDS) > builds, 3000, "a new build after the load")
    yield from ready()
    print(f"collview2 load: build {stat(ST_BUILDS)}, {stat(ST_FACES)} faces")
    emu.w8(run_addr + RUN_COLL_VIEW, 0)
    start = fc()
    yield 60
    if fc() <= start:
        emu.fail("the game stopped after the view was turned off")
