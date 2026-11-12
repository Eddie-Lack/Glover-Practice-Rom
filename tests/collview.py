"""The collision view in Atlantis 1: the cache of the level's collision meshes (practice/src/collview.c).

With the view on (whole level), the cache is built over a few frames. Then:
- Every face that the RSP would draw, worked out from the cache's vertices, its display lists and each landscape
  actor's matrix, is one of the game's collision faces (CollFace corners), and every face of the game is drawn.
- The faces drawn stay the same on every frame while Glover stands still (the old view's scan stopped at a
  different face each frame).
- Each platform's matrix of the frame is at the platform's place: a moving platform's faces move with it.
- A smaller radius draws fewer faces, and the face filter leaves out each kind.
- The game goes on while Glover walks about, see-through and on surfaces.
Writes see0.png, see1.png, surf0.png and surf1.png (needs --gfx angrylion)."""
import struct
import trace
from tools import C_ALL, press, wait_for

RUN_COLL_VIEW = 0x2C   # Run.collView, then collRadius and collSpheres (practice/include/actions.h)
RUN_COLL_FACES = 0x52  # collFaces, collColour, collBack
# CollStats (practice/include/collview.h)
ST_FACES, ST_ACTORS, ST_BAD, ST_STATE, ST_FULL = 0x0, 0x2, 0xE, 0x10, 0x11
ST_SHAPES, ST_RECORDS, ST_VERTS, ST_CMDS, ST_COUNTS, ST_BUILD = 0x12, 0x14, 0x16, 0x18, 0x1C, 0x20
READY = 2
# Record and Shape in collview.c
RECORD_SIZE, SHAPE_SIZE = 20, 44
NO_LIST = 0xFFFF
EDGE_W1 = 0xFF967F3F  # the edges' combiner (EDGE_COMBINE), second word
FACE_W1 = 0xFF75FEFF  # the faces' combiner (FACE_COMBINE)


def mtx(raw):
    """A 16.16 fixed-point Mtx as a 4 by 4 list of floats."""
    hi = struct.unpack(">16h", raw[:32])
    lo = struct.unpack(">16H", raw[32:64])
    return [[(hi[i * 4 + j] * 65536 + lo[i * 4 + j]) / 65536.0 for j in range(4)] for i in range(4)]


def apply(m, v):
    return [v[0] * m[0][k] + v[1] * m[1][k] + v[2] * m[2][k] + m[3][k] for k in range(3)]


def run(emu, sym, args):
    gi = sym("gameInfo")
    fc = lambda: emu.r32(sym("frameCount"))  # noqa: E731
    mode = lambda: emu.r8(gi)  # noqa: E731
    level = lambda: emu.r8(gi + 1)  # noqa: E731
    run_addr = sym("run")
    stats = sym("collStats")
    stat = lambda o: emu.r16(stats + o)  # noqa: E731

    def settle():
        start = fc()
        yield from wait_for(emu, lambda: fc() >= start + 3, 120, "three game frames")

    def phys(a):
        return a | 0x80000000

    def read_list(addr):
        """The triangles of one cached list: (corners in shape space, is an edge band)."""
        tris = []
        loaded = []
        edge = False
        for _ in range(4000):
            w0, w1 = emu.r32(addr), emu.r32(addr + 4)
            op = w0 >> 24
            addr += 8
            if op == 0xB8:
                return tris
            if op == 0x04:
                n = (w0 >> 10) & 0x3F
                raw = emu.read(phys(w1), n * 16)
                loaded = [struct.unpack(">hhh", raw[i * 16:i * 16 + 6]) for i in range(n)]
            elif op == 0xFC:
                edge = w1 == EDGE_W1
            elif op in (0xBF, 0xB1):
                for word in ((w1,) if op == 0xBF else (w0, w1)):
                    idx = [(word >> s & 0xFF) // 2 for s in (16, 8, 0)]
                    tris.append(([loaded[i] for i in idx], edge))
        emu.fail(f"a cached list at {addr:08X} has no end")

    yield from wait_for(emu, lambda: level() == trace.PRESENT and fc() >= 200, 4000, "the logos")
    yield from press(emu, "START", frames=6)
    yield from wait_for(emu, lambda: level() == trace.FLYTHRU and mode() == 4 and fc() >= 30, 3000, "the title")
    yield from press(emu, *C_ALL)
    for b in ("A", "DOWN", "DOWN", "A"):
        yield from press(emu, b)
    yield from wait_for(emu, lambda: level() == trace.ATLANTIS1 and mode() == 4 and fc() >= 100, 3000, "Atlantis 1")

    emu.w8(run_addr + RUN_COLL_VIEW, 1)
    emu.w8(run_addr + RUN_COLL_VIEW + 1, 0)  # the whole level
    yield from wait_for(emu, lambda: emu.r8(stats + ST_STATE) == READY and stat(ST_FACES) > 0, 600, "the cache")
    yield from settle()
    print(f"collview cache: {stat(ST_SHAPES)} meshes, {stat(ST_RECORDS)} actors, {stat(ST_VERTS)} vertices, "
          f"{stat(ST_CMDS)} commands, built in {emu.r32(stats + ST_BUILD)} counts; frame {emu.r32(stats + ST_COUNTS)} "
          f"counts; full {emu.r8(stats + ST_FULL)}, bad {stat(ST_BAD)}")
    if emu.r8(stats + ST_FULL):
        emu.fail("the cache is full in Atlantis 1")

    # 1. The cache against the game's faces, for every landscape actor.
    records, shapes, static = sym("records"), sym("shapes"), sym("staticMtx")
    cache_gfx = sym("cacheGfx")
    checked = drawn = 0
    for i in range(stat(ST_RECORDS)):
        r = records + i * RECORD_SIZE
        actor, shape, platform, matrix = emu.r32(r), emu.r8(r + 8), emu.r8(r + 10), emu.r16(r + 14)
        if shape == 0xFF or platform:
            continue
        s = shapes + shape * SHAPE_SIZE
        nf = emu.r16s(s + 8)
        shift = emu.r8(s + 41)
        shift = shift - 256 if shift > 127 else shift
        m = mtx(emu.read(static + matrix * 64, 64))
        game = []
        raw = emu.read(emu.r32(actor + 0xE0), nf * 0x78)
        for f in range(nf):
            c = struct.unpack(">9f", raw[f * 0x78 + 0x10:f * 0x78 + 0x34])
            game.append([c[0:3], c[3:6], c[6:9]])
        mine = []
        for g in range(4):
            start = emu.r16(s + 20 + 2 * g)
            if start != NO_LIST:
                mine += [t for t, edge in read_list(cache_gfx + start * 8) if not edge]
        placed = [[apply(m, v) for v in t] for t in mine]
        tol = 1.0

        def same(a, b):
            return all(abs(a[j][k] - b[j][k]) <= tol for j in range(3) for k in range(3))
        left = list(game)
        for t in placed:
            hit = next((x for x in left if same(t, x)), None)
            if hit is None:
                emu.fail(f"actor {actor:08X}: a drawn face {[[round(c) for c in p] for p in t]} is not a game face")
            left.remove(hit)
        # The faces not drawn are degenerate: two corners less than 0.01 units apart.
        for x in left:
            d = min(sum((x[a][k] - x[b][k]) ** 2 for k in range(3)) for a, b in ((0, 1), (1, 2), (0, 2)))
            area = 0
            if d > 1e-4:
                e1 = [x[1][k] - x[0][k] for k in range(3)]
                e2 = [x[2][k] - x[0][k] for k in range(3)]
                n = [e1[1] * e2[2] - e1[2] * e2[1], e1[2] * e2[0] - e1[0] * e2[2], e1[0] * e2[1] - e1[1] * e2[0]]
                area = sum(v * v for v in n)
            if area > 1e-4:
                emu.fail(f"actor {actor:08X}: the game face {[[round(c) for c in p] for p in x]} is not drawn")
        checked += 1
        drawn += len(placed)
    print(f"collview: {checked} landscape actors, {drawn} faces, each a game face; every game face drawn")
    if checked < 10:
        emu.fail(f"only {checked} landscape actors were checked")

    # 2. The same faces on every frame while Glover stands still.
    counts = set()
    for _ in range(60):
        counts.add((stat(ST_FACES), stat(ST_ACTORS)))
        yield 1
    print(f"collview steady: faces and actors drawn over 60 frames: {sorted(counts)}")
    if len(counts) != 1:
        emu.fail(f"the faces drawn changed while Glover stood still: {sorted(counts)}")
    whole = stat(ST_FACES)

    # 3. Each platform's matrix is at its place on the frame that drew it. The scene list of the frame buffer that
    # was drawn last holds a matrix load for each platform, in the order of platformList.
    scene, moving = sym("scene"), sym("movingMtx")
    worst = 0.0
    seen = 0
    for _ in range(30):
        yield 1
        b = 1 - (emu.r8(sym("draw_buffer")) & 1)
        mats = []
        a = scene + b * 896 * 8
        for _ in range(896):
            w0, w1 = emu.r32(a), emu.r32(a + 4)
            a += 8
            if w0 >> 24 == 0xB8:
                break
            lo, hi = moving + b * 64 * 64, moving + (b + 1) * 64 * 64
            if w0 >> 24 == 0x01 and lo <= phys(w1) < hi:
                mats.append(mtx(emu.read(phys(w1), 64)))
        head = sym("platformList") + 4
        p = emu.r32(head)
        places = []
        while p != head and p:
            act = p + 0x80
            if not emu.r32(p + 0x14) & 0x84 and emu.r32(act + 0xE0):
                places.append([emu.rf(act + 0x34 + 4 * k) for k in range(3)])
            p = emu.r32(p)
        for m in mats:
            d = min(max(abs(m[3][k] - q[k]) for k in range(3)) for q in places)
            worst = max(worst, d)
        seen = max(seen, len(mats))
    print(f"collview platforms: {seen} platform matrices a frame, each at most {worst:.2f} units from a platform")
    if seen == 0 or worst > 20:
        emu.fail(f"the platforms' matrices are not at the platforms ({seen} matrices, {worst:.2f} units)")

    # 4. A smaller radius and the face filter.
    emu.w8(run_addr + RUN_COLL_VIEW + 1, 3)  # 250
    yield from settle()
    near = stat(ST_FACES)
    emu.w8(run_addr + RUN_COLL_VIEW + 1, 0)
    for bit, name in ((1, "floors"), (2, "walls"), (4, "ceilings"), (0x40, "landscape"), (0x80, "platforms")):
        emu.w8(run_addr + RUN_COLL_FACES, bit)
        yield from settle()
        n = stat(ST_FACES)
        print(f"collview filter: without the {name}, {n} faces")
        if n >= whole:
            emu.fail(f"leaving out the {name} drew {n} faces of {whole}")
    emu.w8(run_addr + RUN_COLL_FACES, 0)
    print(f"collview radius: {whole} faces for the whole level, {near} within 250 units")
    if not 0 < near < whole:
        emu.fail(f"a radius of 250 drew {near} faces of {whole}")

    # 5. Walk about, see-through, then on surfaces, with every colour and back face mode.
    for view, name in ((1, "see"), (2, "surf")):
        emu.w8(run_addr + RUN_COLL_VIEW, view)
        emu.w8(run_addr + RUN_COLL_VIEW + 2, 1)
        start = fc()
        for i in range(300):
            x, y = [(0, 80), (80, 0), (0, -80), (-80, 0)][i // 75]
            emu.hold(x=x, y=y)
            emu.w8(run_addr + RUN_COLL_FACES + 1, (i // 25) % 3)
            emu.w8(run_addr + RUN_COLL_FACES + 2, (i // 50) % 3)
            yield 1
        emu.release()
        emu.w8(run_addr + RUN_COLL_FACES + 1, 0)
        emu.w8(run_addr + RUN_COLL_FACES + 2, 0)
        yield from settle()
        print(f"collview {name}: {stat(ST_FACES)} faces, {stat(4)} spheres; game frames {start} to {fc()}")
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
