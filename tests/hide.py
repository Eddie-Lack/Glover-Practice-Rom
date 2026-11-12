"""Hide objects (Inspect, Hide objects) in Atlantis 1:

1. With nothing hidden, the frame's display lists hold vertices, triangles and rectangles, and the practice code
   notes nothing (HideStats).
2. Each kind on its own: the practice code turns some of the frame's drawing commands into no-ops for the kinds that
   are in view at the level's start (the landscape, the glove, the ball, the garibs, the shadows, the HUD), the
   frame's lists hold fewer commands than with nothing hidden (not for the landscape and the HUD, which these lists do not show), and the game goes on. The other kinds are reported.
3. Every kind at once: the 3D objects' task keeps under a tenth of its triangles (the fade and the star
   transition are drawn later and stay) and under a fifth of its display list calls, and the game goes on while Glover walks
   about. The run no longer counts for a best time (BT_OTHER). Writes land0.png (the landscape hidden), all0.png
   and all1.png (needs --gfx angrylion).
4. The menu: Inspect, Hide objects, Glove hides the glove and shows it again; Show everything clears every kind."""
import struct

import menunav
import trace
from menunav import goto
from tools import C_ALL, press, wait_for

RUN_HIDE = 0x50  # Run.hide (practice/include/actions.h)
# HideStats (practice/include/hide.h)
STATS_FRAME, STATS_LAST, STATS_LAST_COUNTS = 0x00, 0x0C, 0x18
BEST_TAINT = 0x01  # BestTimesState.taint
BT_OTHER = 0x80
DYNAMIC_SIZE = 0x2B230
TASK_DATA_PTR, TASK_DATA_SIZE = 0x40, 0x44
VTX, DL, TRI1, TRI2, TEXRECT, SPRITE2D_DRAW = 0x04, 0x06, 0xBF, 0xB1, 0xE4, 0xBD

KINDS = ["landscape", "scenery", "water", "platforms", "glove", "ball", "enemies", "pick-ups and garibs",
         "debris", "sky", "HUD", "shadows", "footprints", "trails", "lightning"]
# Kinds that are in view at the start of Atlantis 1.
MUST = {"landscape", "glove", "ball", "pick-ups and garibs", "shadows", "HUD"}
# Kinds that the lists read here do not show: the landscape draws through prebuilt lists, and the HUD's commands
# are not in the lists of the tasks that draw_commands reads.
NOT_IN_LISTS = {"landscape", "HUD"}


def draw_commands(emu, sym):
    """Vertex loads, triangles and rectangles in the tasks of the last frame that each buffer built, and the
    triangles and display list calls of the largest task (the 3D objects, CreateObjects) in "world" and "dl". The
    landscape draws through prebuilt lists: its triangles are not in the frame's lists."""
    counts = {"vtx": 0, "tri": 0, "rect": 0, "world": 0, "dl": 0}
    largest = 0
    for buf in range(2):
        base = sym("dynamic") + buf * DYNAMIC_SIZE
        for i in range(1, emu.r8(sym("currentTask"))):
            task = emu.r32(sym("tlistPointers") + 4 * (10 * buf + i))
            ptr, size = emu.r32(task + TASK_DATA_PTR), emu.r32(task + TASK_DATA_SIZE)
            if not base <= ptr < base + DYNAMIC_SIZE or size > 60000 or ptr % 8:
                continue
            # RDRAM is kept in host-order words: a command is two little-endian words here.
            tris = dls = 0
            for w0, _ in struct.iter_unpack("<II", bytes(emu.raw(ptr, size & ~7))):
                op = w0 >> 24
                if op == VTX:
                    counts["vtx"] += 1
                elif op in (TRI1, TRI2):
                    tris += 1
                elif op == DL:
                    dls += 1
                elif op in (TEXRECT, SPRITE2D_DRAW):
                    counts["rect"] += 1
            counts["tri"] += tris
            if size > largest:
                largest, counts["world"], counts["dl"] = size, tris, dls
    return counts


def stats(emu, sym):
    """(actors, sprites, commands) hidden on the last complete frame, and the CPU counts of the work."""
    a = sym("hideStats") + STATS_LAST
    return emu.r16(a), emu.r16(a + 2), emu.r16(a + 4), emu.r32(sym("hideStats") + STATS_LAST_COUNTS)


def sample(emu, sym, frames=12):
    """The most of each of the practice code's counts, and the median of each draw count over the frames whose lists could be read."""
    most = [0, 0, 0, 0]
    seen = {"vtx": [], "tri": [], "rect": [], "world": [], "dl": []}
    for _ in range(frames):
        most = [max(m, s) for m, s in zip(most, stats(emu, sym))]
        c = draw_commands(emu, sym)
        if any(c.values()):  # the lists of a frame can be gone when they are read
            for k, v in c.items():
                seen[k].append(v)
        yield 2
    return most, {k: sorted(v)[len(v) // 2] if v else 0 for k, v in seen.items()}


def run(emu, sym, args):
    gi = sym("gameInfo")
    fc = lambda: emu.r32(sym("frameCount"))  # noqa: E731
    mode = lambda: emu.r8(gi)  # noqa: E731
    level = lambda: emu.r8(gi + 1)  # noqa: E731
    hide = sym("run") + RUN_HIDE

    yield from wait_for(emu, lambda: level() == trace.PRESENT and fc() >= 200, 4000, "the logos")
    yield from press(emu, "START", frames=6)
    yield from wait_for(emu, lambda: level() == trace.FLYTHRU and mode() == 4 and fc() >= 30, 3000, "the title")
    yield from press(emu, *C_ALL)
    for b in ("A", "DOWN", "DOWN", "A"):
        yield from press(emu, b)
    yield from wait_for(emu, lambda: level() == trace.ATLANTIS1 and mode() == 4 and fc() >= 100, 3000, "Atlantis 1")

    # 1. Nothing hidden.
    if emu.r16(hide):
        emu.fail("a kind is hidden at the start")
    frame = emu.r32(sym("hideStats") + STATS_FRAME)
    _, base = yield from sample(emu, sym)
    print(f"hide: nothing hidden: {base}")
    if base["world"] < 20 or base["dl"] < 10:
        emu.fail(f"the frame draws too little to test: {base}")
    if emu.r32(sym("hideStats") + STATS_FRAME) != frame:
        emu.fail("the practice code noted a frame with nothing hidden")

    # 2. One kind at a time.
    for bit, name in enumerate(KINDS):
        emu.w16(hide, 1 << bit)
        start = fc()
        yield 6
        most, cmds = yield from sample(emu, sym)
        print(f"hide: {name}: {most[0]} actors, {most[1]} sprites, {most[2]} commands hidden; left {cmds}")
        if name in MUST and most[2] == 0:
            emu.fail(f"hiding {name} hid nothing")
        if name in MUST and name not in NOT_IN_LISTS and sum(cmds.values()) >= sum(base.values()):
            emu.fail(f"hiding {name} left the frame's lists at {cmds}, from {base}")
        if name not in MUST or name in NOT_IN_LISTS:
            print(f"hide: {name}: not checked in the frame's lists here")
        if fc() < start + 5:
            emu.fail(f"the game stopped with {name} hidden")
        if name == "landscape" and args.gfx == "angrylion":
            emu.fb_png("land0", sym("cfb_16_a"))
    emu.w16(hide, 0)
    yield 6

    # 3. Everything.
    emu.w16(hide, (1 << len(KINDS)) - 1)
    yield 6
    most, cmds = yield from sample(emu, sym)
    print(f"hide: everything: {most[0]} actors, {most[1]} sprites, {most[2]} commands hidden; left {cmds}; "
          f"at most {most[3]} counts ({most[3] / 46875:.2f} ms) a frame")
    if cmds["world"] * 10 > base["world"] or cmds["dl"] * 5 > base["dl"]:
        emu.fail(f"with everything hidden {cmds['world']} of {base['world']} triangle commands and "
                 f"{cmds['dl']} of {base['dl']} list calls are left")
    start = fc()
    for i in range(200):
        x, y = [(0, 80), (80, 0), (0, -80), (-80, 0)][i // 50]
        emu.hold(x=x, y=y)
        yield 1
    emu.release()
    if fc() < start + 30:
        emu.fail(f"the game did not go on with everything hidden ({start} to {fc()})")
    if not emu.r8(sym("bestTimesState") + BEST_TAINT) & BT_OTHER:
        emu.fail("a run with hidden objects still counts for a best time")
    if args.gfx == "angrylion":
        emu.fb_png("all0", sym("cfb_16_a"))
        emu.fb_png("all1", sym("cfb_16_b"))
    emu.w16(hide, 0)
    yield 6
    _, back = yield from sample(emu, sym)
    if back["world"] * 10 < base["world"] * 9 or back["dl"] * 10 < base["dl"] * 9:
        emu.fail(f"the frame did not come back: {back} after {base}")

    # 4. The menu.
    yield from press(emu, *C_ALL)
    yield from goto(emu, sym, "Inspect", "Hide objects", "Glove")
    if emu.r16(hide) != 1 << KINDS.index("glove"):
        emu.fail(f"Glove set run.hide to {emu.r16(hide):#x}")
    yield from press(emu, "A")
    if emu.r16(hide) != 0:
        emu.fail("Glove a second time did not show the glove again")
    emu.w16(hide, 0x5A5)
    yield 2
    yield from goto(emu, sym, "Ball", "Show everything")
    if emu.r16(hide) != 0:
        emu.fail(f"Show everything left run.hide at {emu.r16(hide):#x}")
    yield from menunav.top(emu, sym)
    yield from press(emu, *C_ALL)
