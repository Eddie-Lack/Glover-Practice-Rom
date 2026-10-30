"""Spawn or move actors in Atlantis 1 (practice/include/spawn.h).

1. Spawn, through the menu: Inspect, Spawn actor, the first kind (a garib). A garib is not an actor; it joins the
   group that enemies use for the garibs they drop, and numGaribs goes up by one.
2. Spawn every other kind that the view offers (power-ups and enemies): a new actor of the right type appears in the
   actor list, in front of Glover; the game keeps running for four seconds.
3. Move, through the menu: Inspect, Actors, the details of an enemy, Z. The enemy goes in front of the glove, and
   the game goes on.
4. Move a platform with no motion (flag 0x20000): it goes to Glover, the game drops the flag, and its collision
   faces follow (the corners of its first face moved by the same amount as its position).
5. The HUD's MODIFIED marker: no text is drawn before the first use; after it the marker shows, and a restart of the
   level clears it.

Writes the frame buffers after the spawns as actors_*.png (needs --gfx angrylion)."""
import layout
import trace
from tools import C_ALL, press, wait_for

RUN_SPAWN_KIND, RUN_ACTORS_MOD, RUN_MOVE_ACTOR = 0x3E, 0x3F, 0x40
PRACTICE_TEXT_FRAMES = 0x08  # Practice.textFrames
ACTOR_TYPE, ACTOR_FLAGS, ACTOR_POS, ACTOR_ROT = 0x20, 0x28, 0x34, 0x78
PLATFORM_FLAGS, PLATFORM_ACTOR = 0x14, 0x80
STATIC = 0x20000
SPAWN_GARIB, SPAWN_LIFE, SPAWN_PICKUP, SPAWN_ENEMY = 0, 1, 2, 17
TYPE_PLATFORM, TYPE_COLLECTABLE = 59, 61
FRONT = 70.0
PICKUP_NAMES = ["stun", "death", "frog", "suction", "hercules", "speed up", "rotor blades", "bowling ball",
                "power ball", "ball bearing", "beach ball", "snow ball", "crystal ball", "boomerang ball",
                "vanish ball"]


def run(emu, sym, args):
    gi = sym("gameInfo")
    fc = lambda: emu.r32(sym("frameCount"))  # noqa: E731
    mode = lambda: emu.r8(gi)  # noqa: E731
    level = lambda: emu.r8(gi + 1)  # noqa: E731
    hand = sym("hand")
    run_ = sym("run")
    settings = sym("settings")
    al = sym("actorList")
    text_frames = lambda: emu.r32(sym("practice") + PRACTICE_TEXT_FRAMES)  # noqa: E731
    vec = lambda a: [emu.rf(a + 4 * i) for i in range(3)]  # noqa: E731
    hpos = lambda: vec(hand + ACTOR_POS)  # noqa: E731

    def actors():
        out = []
        a = emu.r32(al)
        while a != al and a and len(out) < 500:
            out.append(a)
            a = emu.r32(a)
        return out

    def type_of(a):
        return emu.r16s(a + ACTOR_TYPE)

    def platforms():
        head = sym("platformList") + 4
        p = emu.r32(head)
        while p != head and p:
            yield p
            p = emu.r32(p)

    def in_front(pos):
        """The distance of pos from Glover in the plane. A new actor is FRONT units away."""
        h = hpos()
        return ((pos[0] - h[0]) ** 2 + (pos[2] - h[2]) ** 2) ** 0.5

    def shot(name):
        if args.gfx == "angrylion":
            emu.fb_png("actors_" + name, sym("cfb1"))
            emu.fb_png("actors_" + name + "_0", sym("cfb0"))

    def alive(seconds, what):
        f0 = fc()
        yield int(60 * seconds)
        print(f"actors: {what}: {fc() - f0} game frames in {seconds} s")
        if fc() - f0 < 20 * seconds * 0.25:
            emu.fail(f"{what}: the game stopped (frameCount advanced {fc() - f0} in {seconds} s)")

    yield from wait_for(emu, lambda: level() == trace.PRESENT and fc() >= 200, 4000, "the logos")
    yield from press(emu, "START", frames=6)
    yield from wait_for(emu, lambda: level() == trace.FLYTHRU and mode() == 4 and fc() >= 30, 3000, "the title")
    yield from press(emu, *C_ALL)
    for b in ("A", "DOWN", "DOWN", "A"):  # Warp, Go
        yield from press(emu, b)
    yield from wait_for(emu, lambda: level() == trace.ATLANTIS1 and mode() == 4 and fc() >= 100, 3000, "Atlantis 1")
    emu.w8(settings + layout.SETTINGS_HUD, 0)  # the timer off: text is drawn only for the marker
    yield 120

    def marker_drawn(frames=30):
        before = text_frames()
        yield frames
        return text_frames() - before >= 2

    # 5. Nothing is drawn before the first use, and the tool does not run.
    if emu.r8(run_ + RUN_ACTORS_MOD) or (yield from marker_drawn()):
        emu.fail("the MODIFIED marker shows with the tool unused")

    n_enemies = emu.r32s(sym("numberOfEnemies"))
    enemy_types = [emu.r16s(sym("enemies") + 0x300 * i + ACTOR_TYPE) for i in range(n_enemies)]
    print(f"actors: {len(actors())} actors, {n_enemies} enemies (types {enemy_types}), "
          f"{emu.r32(sym('platformList'))} platforms")
    if not enemy_types:
        emu.fail("Atlantis 1 has no enemies")

    # 1. Spawn through the menu: Inspect, the third last item (UP three times from the first), the first kind.
    garibs = emu.r32s(sym("numGaribs"))
    yield from press(emu, *C_ALL)
    for b in ["DOWN"] * 5 + ["A", "UP", "UP", "UP", "A"]:  # Inspect (6th item), then its third last item, Spawn actor
        yield from press(emu, b)
    count = emu.r32(sym("spawnKindCount"))
    kinds = [emu.r8(sym("spawnKinds") + i) for i in range(count)]
    print(f"actors: the view offers {count} kinds: {kinds}")
    if args.gfx == "angrylion":
        emu.fb_png("actors_view", sym("cfb1"))
    if not kinds or kinds[0] != SPAWN_GARIB or SPAWN_LIFE not in kinds:
        emu.fail(f"the view does not offer the garibs first: {kinds}")
    yield from press(emu, "A")  # makes a garib
    yield 12
    group = emu.r32(sym("NMEGaribGroup"))
    if not group:
        emu.fail("no garib group after the spawn")
    newest = emu.r32(group + 0x08 + 4)  # the garib list's head.prev
    gpos = vec(newest + 0x24)
    print(f"actors: garib at {[round(v) for v in gpos]}, numGaribs {garibs} -> {emu.r32s(sym('numGaribs'))}")
    if emu.r32s(sym("numGaribs")) != garibs + 1:
        emu.fail("numGaribs did not go up by one")
    if abs(in_front(gpos) - FRONT) > 25:
        emu.fail(f"the garib is {in_front(gpos):.0f} units from Glover, not about {FRONT:.0f}")
    yield from alive(2, "spawn garib")
    shot("garib")

    # 2. Every other kind, by the request word. The pick-ups and enemies are found by the actor list.
    def spawn(kind, label):
        old = set(actors())
        emu.w8(run_ + RUN_SPAWN_KIND, kind + 1)
        yield 12
        new = [a for a in actors() if a not in old]
        if len(new) != 1:
            emu.fail(f"{label}: {len(new)} new actors, not one")
        return new[0]

    spawned = 0
    for kind in kinds:
        if kind == SPAWN_GARIB:
            continue
        if kind == SPAWN_LIFE:
            n = emu.r32s(sym("numGaribs"))
            emu.w8(run_ + RUN_SPAWN_KIND, kind + 1)
            yield 12
            if emu.r32s(sym("numGaribs")) != n:
                emu.fail("a life garib changed numGaribs")
            continue
        if kind < SPAWN_ENEMY:
            label = "power-up " + PICKUP_NAMES[kind - SPAWN_PICKUP]
            want = TYPE_COLLECTABLE
        else:
            want = kind - SPAWN_ENEMY
            label = f"enemy type {want}"
        n_before = emu.r32s(sym("numberOfEnemies"))
        a = yield from spawn(kind, label)
        pos = vec(a + ACTOR_POS)
        print(f"actors: {label}: actor {a:#x} type {type_of(a)} at {[round(v) for v in pos]}")
        if type_of(a) != want:
            emu.fail(f"{label}: the new actor has type {type_of(a)}, not {want}")
        if abs(in_front(pos) - FRONT) > 40:
            emu.fail(f"{label}: the new actor is {in_front(pos):.0f} units from Glover, not about {FRONT:.0f}")
        if kind >= SPAWN_ENEMY and emu.r32s(sym("numberOfEnemies")) != n_before + 1:
            emu.fail(f"{label}: numberOfEnemies did not go up by one")
        spawned += 1
        yield from alive(1, label)
        shot(label.replace(" ", "_"))
    if spawned < 2:
        emu.fail(f"only {spawned} actor kinds were spawned")
    # Four seconds with everything in the level: the game keeps running and the actors keep sane positions.
    yield from alive(4, "all spawned")
    for a in actors():
        if not all(abs(v) < 20000 for v in vec(a + ACTOR_POS)):
            emu.fail(f"an actor at {a:#x} has the position {vec(a + ACTOR_POS)}")

    # 3. Move an enemy through the menu: Inspect, Actors, DOWN to the enemy, A, Z.
    enemy = sym("enemies")  # the first enemy's actor
    order = actors()
    if enemy not in order:
        emu.fail("the first enemy is not in the actor list")
    index = order.index(enemy)
    yield 1
    # Glover is not moved. The enemy is far away at first.
    before = vec(enemy + ACTOR_POS)
    yield from press(emu, *C_ALL)
    # The menu opens on Inspect (where it was left), and a sub menu opens on its first item, Actors.
    for b in ["A", "A"] + ["DOWN"] * index + ["A"]:
        yield from press(emu, b)
    shot_menu = args.gfx == "angrylion"
    if shot_menu:
        emu.fb_png("actors_details", sym("cfb1"))
    yield from press(emu, "Z")
    seen = []
    for _ in range(12):
        seen.append(vec(enemy + ACTOR_POS))
        yield 1
    near = min(in_front(p) for p in seen)
    print(f"actors: enemy 0 was at {[round(v) for v in before]}, then {[round(v) for v in seen[0]]}; "
          f"Glover at {[round(v) for v in hpos()]}")
    if emu.r8(sym("menuOpen")):
        emu.fail("the menu stayed open after Z")
    if in_front(before) < 200:
        emu.fail("the enemy was already near Glover at the start")
    if abs(near - FRONT) > 30:
        emu.fail(f"the enemy came {near:.0f} units from Glover, not about {FRONT:.0f}")
    if not emu.r8(run_ + RUN_ACTORS_MOD):
        emu.fail("no MODIFIED marker state after the move")
    if emu.r32(run_ + RUN_MOVE_ACTOR):
        emu.fail("the move request was not taken")
    yield from alive(3, "move enemy")
    shot("moved_enemy")
    if not (yield from marker_drawn()):
        emu.fail("the MODIFIED marker is not drawn after a move")

    # 4. Move a platform with no motion.
    platform = None
    for p in platforms():
        act = p + PLATFORM_ACTOR
        fl = emu.r32(p + PLATFORM_FLAGS)
        inst = emu.r32(act + 0xC8)
        mesh = emu.r32(emu.r32(inst + 0xC) + 0x14) if inst else 0
        faces = emu.r32(act + 0xE0)
        if fl & STATIC and not fl & 0x84 and mesh and faces and emu.r16s(mesh) > 0 and emu.rf(act + 0xA8) == 1.0:
            platform = p
            break
    if platform is None:
        emu.fail("Atlantis 1 has no platform with a static mesh")
    act = platform + PLATFORM_ACTOR
    faces = emu.r32(act + 0xE0)
    corner = lambda: vec(faces + 0x10)  # noqa: E731
    pos0, corner0 = vec(act + ACTOR_POS), corner()
    emu.w32(run_ + RUN_MOVE_ACTOR, act)
    yield 12
    pos1, corner1 = vec(act + ACTOR_POS), corner()
    print(f"actors: platform at {[round(v) for v in pos0]} moved to {[round(v) for v in pos1]}; its first corner "
          f"{[round(v) for v in corner0]} -> {[round(v) for v in corner1]}; flags {emu.r32(platform + PLATFORM_FLAGS):#x}")
    if abs(in_front(pos1) - FRONT) > 30:
        emu.fail(f"the platform came {in_front(pos1):.0f} units from Glover, not about {FRONT:.0f}")
    if emu.r32(platform + PLATFORM_FLAGS) & STATIC:
        emu.fail("the platform still has the static flag")
    for i in range(3):
        if abs((corner1[i] - corner0[i]) - (pos1[i] - pos0[i])) > 1.0:
            emu.fail(f"the platform's collision face did not follow: it moved {[b - a for a, b in zip(corner0, corner1)]},"
                     f" the platform {[b - a for a, b in zip(pos0, pos1)]}")
    yield from alive(2, "move platform")

    # 5. The marker is drawn, and a restart of the level clears it.
    if not (yield from marker_drawn()):
        emu.fail("the MODIFIED marker is not drawn after the spawns")
    yield from press(emu, "UP", x=0, y=80)  # the default binding: reset level
    yield from wait_for(emu, lambda: fc() < 20, 2000, "the level to restart")
    yield from wait_for(emu, lambda: level() == trace.ATLANTIS1 and mode() == 4 and fc() >= 60, 3000, "Atlantis 1")
    yield 30
    if emu.r8(run_ + RUN_ACTORS_MOD):
        emu.fail("the marker state stayed after the level restarted")
    if (yield from marker_drawn()):
        emu.fail("the marker is still drawn after the level restarted")
    yield from alive(2, "after the restart")
