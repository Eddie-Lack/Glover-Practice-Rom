"""The three extra parts of the collision view in Atlantis 1, each with its own switch (Inspect menu):
enemy spheres, up-to-date platforms and open edges.

Glover is moved to the mallet enemy's place (Run.teleport), 320 units from two bobbing platforms that the game
does not update at that distance. With all three switches off, nothing is computed. Each switch on draws its part:
the stats count it, and the vertex buffer holds it (hot pink for edges, the enemy colour for spheres, and a corner
of a platform placed from its mesh). The game goes on. Writes enemies0/1.png, platforms0/1.png and edges0/1.png
(the two frame buffers; needs --gfx angrylion)."""
import struct
import trace
from tools import C_ALL, press, wait_for

RUN_TELEPORT = 0x10     # Run.teleport, then teleportPos (practice/include/actions.h)
RUN_TELEPORT_POS = 0x14
RUN_COLL_VIEW = 0x2C    # collView, collRadius, collSpheres
RUN_COLL_ENEMIES = 0x3B  # then collFresh and collEdges
STATS_SPHERES = 0x2     # CollStats (practice/include/collview.h)
STATS_ENEMIES = 0xC
STATS_FRESH = 0xE
STATS_EDGES = 0x10
STATS_COUNTS = 0x14     # CPU counts of the last scan and list (the cost)
MAX_FACES, MAX_SPHERES, SPHERE_VERTS = 320, 40, 12
EDGE_BASE = MAX_FACES * 3 + MAX_SPHERES * SPHERE_VERTS  # vertices before the open edges
VTX_COUNT = EDGE_BASE + 128 * 4
EDGE_RGBA = (0xFF, 0x20, 0xA0, 0xFF)
ENEMY_RGB = (0xFF, 0x60, 0x30)
NOT_UPDATED = 0x20084   # Platform.flags: the game does not bring its faces up to date


def run(emu, sym, args):
    gi = sym("gameInfo")
    fc = lambda: emu.r32(sym("frameCount"))  # noqa: E731
    mode = lambda: emu.r8(gi)  # noqa: E731
    level = lambda: emu.r8(gi + 1)  # noqa: E731
    run_addr = sym("run")
    stats = sym("collStats")
    hand = sym("hand")
    verts = sym("vertices")

    def switches(view, enemies, fresh, edges):
        emu.w8(run_addr + RUN_COLL_VIEW, view)
        emu.w8(run_addr + RUN_COLL_ENEMIES, enemies)
        emu.w8(run_addr + RUN_COLL_ENEMIES + 1, fresh)
        emu.w8(run_addr + RUN_COLL_ENEMIES + 2, edges)

    def vertex_buffers():
        """Both buffers' vertices, as (x, y, z, rgba) lists."""
        out = []
        for b in range(2):
            raw = emu.read(verts + b * VTX_COUNT * 16, VTX_COUNT * 16)
            out.append([struct.unpack(">hhhxxxxxxBBBB", raw[i * 16:i * 16 + 16]) for i in range(VTX_COUNT)])
        return out

    def stat(offset):
        return emu.r16(stats + offset)

    def shot(name):
        if args.gfx == "angrylion":
            emu.fb_png(name + "0", sym("cfb_16_a"))
            emu.fb_png(name + "1", sym("cfb_16_b"))

    def platforms():
        head = sym("platformList") + 4
        p = emu.r32(head)
        while p != head and p:
            yield p
            p = emu.r32(p)

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
    yield 400
    print("collview2: Glover at", [round(emu.rf(hand + 0x34 + 4 * i)) for i in range(3)], "mallet at", mallet)

    # 1. The view on and every switch off: none of the three parts is computed.
    emu.w8(run_addr + RUN_COLL_VIEW + 1, 0)  # radius 500
    switches(1, 0, 0, 0)
    yield 40
    if stat(STATS_ENEMIES) or stat(STATS_FRESH) or stat(STATS_EDGES):
        emu.fail(f"with the switches off: {stat(STATS_ENEMIES)} enemy spheres, {stat(STATS_FRESH)} platforms, "
                 f"{stat(STATS_EDGES)} edges")
    if stat(0) < 10:
        emu.fail("the collision view drew no faces with the switches off")
    print(f"collview2 off: {stat(0)} faces, {emu.r32(stats + STATS_COUNTS)} counts")

    # 2. Enemy spheres.
    switches(1, 1, 0, 0)
    best = 0
    start = fc()
    for _ in range(60):
        best = max(best, stat(STATS_ENEMIES))
        yield 1
    shot("enemies")
    seen = any(v[3:6] == ENEMY_RGB for buf in vertex_buffers() for v in buf[:EDGE_BASE])
    print(f"collview2 enemies: at most {best} enemy spheres; enemy colour in a vertex buffer: {seen}; "
          f"{emu.r32(stats + STATS_COUNTS)} counts")
    if best == 0 or not seen:
        emu.fail(f"the enemy spheres were not drawn ({best} spheres, colour found: {seen})")
    if fc() < start + 5:
        emu.fail("the game did not go on with the enemy spheres")

    # 3. Up-to-date platforms. Glover stands on the bobbing platform at (3, 73, -683), 232 units from the next one,
    # which the game does not update at that distance. For each platform that the game did not update on this game
    # frame: its faces, placed from the mesh, must be in the frame's vertices. Some of them differ from the
    # game's own faces, which are out of date.
    switches(1, 0, 1, 0)
    for i, v in enumerate((3.0, 120.0, -683.0)):
        emu.wf(run_addr + RUN_TELEPORT_POS + 4 * i, v)
    emu.w8(run_addr + RUN_TELEPORT, 1)
    yield 400
    print("collview2: Glover at", [round(emu.rf(hand + 0x34 + 4 * i)) for i in range(3)])
    good = checked = moved = 0
    most_fresh = 0
    for _ in range(60):
        yield 1
        most_fresh = max(most_fresh, stat(STATS_FRESH))
        hp = [emu.rf(hand + 0x34 + 4 * i) for i in range(3)]
        bufs = vertex_buffers()
        for p in platforms():
            act = p + 0x80
            inst = emu.r32(act + 0xC8)
            mesh = emu.r32(emu.r32(inst + 0xC) + 0x14) if inst else 0
            if not mesh or emu.r32(p + 0x14) & NOT_UPDATED or emu.r16(p + 0x6E) == fc() & 0xFFFF:
                continue
            if emu.r16s(mesh) < 1:
                continue
            pos = [emu.rf(act + 0x34 + 4 * i) for i in range(3)]
            if sum((pos[i] - hp[i]) ** 2 for i in range(3)) > (400 + emu.rf(act + 0x30)) ** 2:
                continue
            q = [emu.rf(act + 0x9C + 4 * i) for i in range(4)]
            if abs(q[3] - 1.0) > 1e-6:
                continue  # an identity rotation keeps the check short
            sc = [emu.rf(act + 0xBC + 4 * i) for i in range(3)]
            verts_p, faces_p = emu.r32(mesh + 4), emu.r32(mesh + 8)
            game_faces = emu.r32(act + 0xE0)
            have = {v[:3] for buf in bufs for v in buf[:MAX_FACES * 3]}
            for i in range(min(emu.r16s(mesh), 40)):
                corner = []
                for j in range(3):
                    vi = emu.r16s(faces_p + 6 * i + j * 2)
                    corner.append([emu.rf(verts_p + 12 * vi + 4 * k) * sc[k] + pos[k] for k in range(3)])
                stale = [[emu.rf(game_faces + 0x78 * i + 0x10 + 12 * j + 4 * k) for k in range(3)] for j in range(3)]
                want = [tuple(round((c[k] - hp[k]) * 2) for k in range(3)) for c in corner]
                hit = all(any(all(abs(w[k] - h[k]) <= 3 for k in range(3)) for h in have) for w in want)
                differs = max(abs(corner[j][k] - stale[j][k]) for j in range(3) for k in range(3)) > 1.5
                checked += 1
                good += hit
                if hit and differs:
                    moved += 1
                    if moved <= 2:
                        print(f"collview2 platform {p:08X} face {i}: game corner y {stale[0][1]:.2f}, "
                              f"drawn at the placed y {corner[0][1]:.2f}")
    shot("platforms")
    print(f"collview2 platforms: at most {most_fresh} placed from the mesh; {good} of {checked} faces found in the "
          f"vertices, {moved} of them where the game's own face was out of date")
    if most_fresh == 0 or moved == 0:
        emu.fail(f"no out-of-date platform face was drawn at its place ({most_fresh} platforms counted, "
                 f"{good} faces found, {moved} out of date)")

    # 4. Open edges.
    switches(1, 0, 0, 1)
    most_edges = 0
    for _ in range(60):
        yield 1
        most_edges = max(most_edges, stat(STATS_EDGES))
    n = stat(STATS_EDGES)
    shot("edges")
    pink = sum(1 for buf in vertex_buffers() for v in buf[EDGE_BASE:EDGE_BASE + 8] if v[3:7] == EDGE_RGBA)
    print(f"collview2 edges: {n} open edges now, at most {most_edges}; pink vertices in the first 8 of each buffer: {pink}; "
          f"{emu.r32(stats + STATS_COUNTS)} counts")
    if most_edges == 0 or pink == 0:
        emu.fail(f"the open edges were not drawn ({most_edges} edges, {pink} pink vertices)")

    # 5. All three, with the glove's spheres, while Glover walks about.
    emu.w8(run_addr + RUN_COLL_VIEW + 2, 1)
    switches(2, 1, 1, 1)
    start = fc()
    for i in range(240):
        x, y = [(0, 80), (80, 0), (0, -80), (-80, 0)][i // 60]
        emu.hold(x=x, y=y)
        yield 1
    emu.release()
    shot("all")
    print(f"collview2 all: {stat(0)} faces, {stat(STATS_SPHERES)} spheres, {stat(STATS_EDGES)} edges; "
          f"game frames {start} to {fc()}")
    if fc() < start + 30:
        emu.fail(f"the game did not go on with all three switches on ({start} to {fc()})")
    switches(0, 0, 0, 0)
    start = fc()
    yield 60
    if fc() <= start:
        emu.fail("the game stopped after the view was turned off")
