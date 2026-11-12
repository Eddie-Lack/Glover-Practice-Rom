"""Face info and the wireframe of the collision view in Atlantis 1 (practice/src/collview.c).

- Face info with the view off: the practice text is drawn on every frame, the glove's face is one of the faces it
  touches, its actor holds the face at the number given, and its kind and terrain are the face's. The face at the
  screen's centre is the nearest face that the last walk's ray meets, worked out here over every face of every
  collision actor (no bounding spheres), at Glover's start and at three other places.
- After Glover leaves the ground, the glove's face stays as an earlier frame's.
- The wireframe: "over faces" draws the scene with the faces' render mode, then again with the outline mode
  (coverage as alpha, set with shift 3 and length 29); "alone" draws it only in the outline mode; "on surfaces"
  uses the outline mode with Z compare.
Writes info0/1.png, wire1_0/1.png and wire2_0/1.png (needs --gfx angrylion)."""
import struct
import trace
from tools import C_ALL, press, wait_for

RUN_TELEPORT = 0x10      # Run.teleport, then teleportPos (practice/include/actions.h)
RUN_TELEPORT_POS = 0x14
RUN_COLL_VIEW = 0x2C
RUN_COLL_WIRE, RUN_COLL_INFO = 0x55, 0x56
PRACTICE_TEXT_FRAMES = 0x08
# CollFaceInfo and the Ray in collview.c
INFO_SIZE = 0x20
I_FACE, I_ACTOR, I_NORMAL, I_DIST, I_NUMBER, I_STATE, I_TERRAIN, I_KIND, I_PLATFORM, I_BACK = (
    0x00, 0x04, 0x08, 0x14, 0x18, 0x1A, 0x1B, 0x1C, 0x1D, 0x1E)
RAY_RUNNING = 0x46
ST_STATE, ST_INFO_COUNTS = 0x10, 0x24
FACE_SIZE = 0x78
EXTRA_SIZE = 232
RENDER_WIRE, RENDER_WIRE_Z, RENDER_XLU = 0x50006248, 0x50006A58, 0x00504240
SET_RENDER_MODE = 0xB900031D  # G_SETOTHERMODE_L, shift 3, length 29
G_DL, G_ENDDL = 0x06, 0xB8


def run(emu, sym, args):
    gi = sym("gameInfo")
    fc = lambda: emu.r32(sym("frameCount"))  # noqa: E731
    mode = lambda: emu.r8(gi)  # noqa: E731
    level = lambda: emu.r8(gi + 1)  # noqa: E731
    run_addr = sym("run")
    stats = sym("collStats")
    info = sym("collInfo")
    ray = sym("ray")
    hand = sym("hand")

    def settle(n=3):
        start = fc()
        yield from wait_for(emu, lambda: fc() >= start + n, 200, f"{n} game frames")

    def vec(a):
        return [emu.rf(a + 4 * i) for i in range(3)]

    def face_actors():
        """(actor, faces, count, platform) for every collision actor, as the view walks them."""
        out = []
        head = sym("platformList") + 4
        p = emu.r32(head)
        n = 0
        while p != head and n < 256 and p & 0x80000000:
            actor = p + 0x80
            if not emu.r32(p + 0x14) & 0x84 and emu.r32(actor + 0xE0):
                out.append((actor, 1))
            p = emu.r32(p)
            n += 1
        cl = sym("collisionList")
        a = emu.r32(cl + 8)
        n = 0
        while a != cl and n < 1024 and a & 0x80000000:
            out.append((a, 0))
            a = emu.r32(a + 8)
            n += 1
        result = []
        for actor, platform in out:
            inst = emu.r32(actor + 0xC8)
            if not inst & 0x80000000:
                continue
            obj = emu.r32(inst + 0xC)
            mesh = emu.r32(obj + 0x14) if obj & 0x80000000 else 0
            faces = emu.r32(actor + 0xE0)
            if not mesh & 0x80000000 or not faces & 0x80000000:
                continue
            count = emu.r16s(mesh)
            if 0 < count <= 1024:
                result.append((actor, faces, count, platform))
        return result

    def cast(o, d):
        """The nearest face that the ray from O along D meets: (t, face address)."""
        best = (1e30, 0)
        far = emu.r16s(sym("farPlaneDist"))
        for actor, faces, count, platform in face_actors():
            raw = emu.read(faces, count * FACE_SIZE)
            for i in range(count):
                f = struct.unpack_from(">13f", raw, i * FACE_SIZE)
                n, dd, c = f[0:3], f[3], (f[4:7], f[7:10], f[10:13])
                den = sum(n[k] * d[k] for k in range(3))
                if abs(den) < 1e-4:
                    continue
                t = -(sum(n[k] * o[k] for k in range(3)) + dd) / den
                if not 0 < t < min(best[0], far):
                    continue
                q = [o[k] + d[k] * t for k in range(3)]
                inside = True
                for j in range(3):
                    a, b = c[j], c[(j + 1) % 3]
                    e = [b[k] - a[k] for k in range(3)]
                    w = [q[k] - a[k] for k in range(3)]
                    x = (e[1] * w[2] - e[2] * w[1], e[2] * w[0] - e[0] * w[2], e[0] * w[1] - e[1] * w[0])
                    if sum(x[k] * n[k] for k in range(3)) <= -0.01:
                        inside = False
                if inside:
                    best = (t, faces + i * FACE_SIZE)
        return best

    def check_info(where, glove=True):
        """The two faces of the frame just drawn, against the game's data. GLOVE: also the glove's face."""
        g = info
        if glove:
            check_glove(where)
        return check_centre(where)

    def check_glove(where):
        g = info
        if emu.r8(g + I_STATE) != 1:
            emu.fail(f"{where}: no glove face (state {emu.r8(g + I_STATE)})")
        face, actor, number = emu.r32(g + I_FACE), emu.r32(g + I_ACTOR), emu.r16(g + I_NUMBER)
        obj = emu.r32(hand + 0xD8)
        planes = [emu.r32(obj + 4 * k) for k in range(4)]
        if face not in planes:
            emu.fail(f"{where}: the glove's face {face:08X} is not one it touches {planes}")
        if actor == 0 or emu.r32(actor + 0xE0) + number * FACE_SIZE != face:
            emu.fail(f"{where}: the glove's face {face:08X} is not face {number} of actor {actor:08X}")
        ny = emu.rf(face + 4)
        terrain = emu.r8(face + 0x74)
        if emu.r8(g + I_TERRAIN) != terrain or abs(emu.rf(g + I_NORMAL + 4) - ny) > 1e-6:
            emu.fail(f"{where}: the glove's face has the wrong terrain or normal")
        kind = emu.r8(g + I_KIND)
        if (ny > 0.3) != (kind <= 1) or (ny < -0.3) != (kind == 3):
            emu.fail(f"{where}: kind {kind} for a normal's y of {ny:.3f}")
        print(f"collinfo {where}: glove face {number} of {actor:08X} (kind {kind}, terrain {terrain})")

    def check_centre(where):
        c = info + INFO_SIZE
        o, d = vec(ray), vec(ray + 0xC)
        t, want = cast(o, d)
        got = emu.r32(c + I_FACE) if emu.r8(c + I_STATE) == 1 else 0
        dist = emu.rf(c + I_DIST)
        print(f"collinfo {where}: centre {got:08X} at {dist:.1f}, expected {want:08X} at {t:.1f}; "
              f"{emu.r32(stats + ST_INFO_COUNTS)} counts")
        if got != want and not (got and abs(dist - t) < 0.05):
            emu.fail(f"{where}: the centre's face is {got:08X}, the ray meets {want:08X} first")
        if got and emu.r32(emu.r32(c + I_ACTOR) + 0xE0) + emu.r16(c + I_NUMBER) * FACE_SIZE != got:
            emu.fail(f"{where}: the centre's face number is wrong")
        return got

    def last_extra():
        b = 1 - (emu.r8(sym("draw_buffer")) & 1)
        addr = sym("extra") + b * EXTRA_SIZE * 8
        out = []
        for i in range(EXTRA_SIZE):
            w0, w1 = emu.r32(addr + 8 * i), emu.r32(addr + 8 * i + 4)
            out.append((w0, w1))
            if w0 >> 24 == G_ENDDL:
                break
        return out, sym("scene") + b * 896 * 8

    def passes():
        """The render mode of each call of the scene, in order."""
        out, scene = last_extra()
        modes, current = [], None
        for w0, w1 in out:
            if w0 >> 24 == 0xB9:
                if w0 not in (SET_RENDER_MODE,):
                    emu.fail(f"a render mode set with {w0:08X}: not shift 3, length 29")
                current = w1
            if w0 >> 24 == G_DL and w1 & 0xFFFFFF == scene & 0xFFFFFF:
                modes.append(current)
        return modes

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

    # 1. Face info alone, at the start and at three other places.
    text = sym("practice") + PRACTICE_TEXT_FRAMES
    before = emu.r32(text)
    emu.w8(run_addr + RUN_COLL_INFO, 1)
    obj = emu.r32(hand + 0xD8)
    yield from wait_for(emu, lambda: emu.r8(obj + 0x188) and emu.r8(info + I_STATE) == 1, 300, "the glove's face")
    yield from settle()
    if emu.r32(text) - before < 4:
        emu.fail("the practice text is not drawn with face info on")
    yield from wait_for(emu, lambda: emu.r8(ray + RAY_RUNNING) == 0, 100, "a finished walk")
    check_info("start")
    shot("info")
    start = vec(hand + 0x34)
    hits = 0
    for k, (dx, dz) in enumerate(((300, 0), (0, -400), (-250, 250))):
        for i, v in enumerate((start[0] + dx, start[1] + 60, start[2] + dz)):
            emu.wf(run_addr + RUN_TELEPORT_POS + 4 * i, v)
        emu.w8(run_addr + RUN_TELEPORT, 1)
        yield from settle(30)
        yield from wait_for(emu, lambda: emu.r8(ray + RAY_RUNNING) == 0, 100, "a finished walk")
        hits += bool(check_info(f"place {k + 1}", emu.r8(info + I_STATE) == 1))
    if hits == 0:
        emu.fail("the ray met no face at any place")

    # 2. In the air the glove's face is kept as an earlier frame's.
    p = vec(hand + 0x34)
    for i, v in enumerate((p[0], p[1] + 400, p[2])):
        emu.wf(run_addr + RUN_TELEPORT_POS + 4 * i, v)
    emu.w8(run_addr + RUN_TELEPORT, 1)
    yield from wait_for(emu, lambda: emu.r8(info + I_STATE) == 2, 100, "the glove's face kept in the air")

    # 3. The wireframe.
    emu.w8(run_addr + RUN_COLL_VIEW, 1)
    yield from wait_for(emu, lambda: emu.r8(stats + ST_STATE) == 2 and emu.r16(stats) > 0, 1200, "the cache")
    yield from settle()
    if passes() != [RENDER_XLU, RENDER_XLU]:
        emu.fail(f"wireframe off: scene passes {[hex(m) for m in passes()]}")
    emu.w8(run_addr + RUN_COLL_WIRE, 1)
    yield from settle()
    if passes() != [RENDER_XLU, RENDER_XLU, RENDER_WIRE]:
        emu.fail(f"over faces: scene passes {[hex(m) for m in passes()]}")
    shot("wire1_")
    emu.w8(run_addr + RUN_COLL_WIRE, 2)
    yield from settle()
    if passes() != [RENDER_WIRE]:
        emu.fail(f"alone: scene passes {[hex(m) for m in passes()]}")
    shot("wire2_")
    emu.w8(run_addr + RUN_COLL_VIEW, 2)
    yield from settle()
    if passes() != [RENDER_WIRE_Z]:
        emu.fail(f"alone, on surfaces: scene passes {[hex(m) for m in passes()]}")
    print("collinfo wireframe: off 2 passes, over faces 3, alone 1, on surfaces with Z compare")
    emu.w8(run_addr + RUN_COLL_WIRE, 0)
    emu.w8(run_addr + RUN_COLL_VIEW, 0)
    emu.w8(run_addr + RUN_COLL_INFO, 0)
    yield from settle()
