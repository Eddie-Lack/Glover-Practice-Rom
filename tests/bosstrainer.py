"""The boss window trainer (practice/include/bosswin.h) in the Atlantis boss, the Prehistoric boss and the last Space fight,
and its quiet cases.

The windows were measured on the retail ROM (docs/game.md, "The bosses"). Each trial here puts a contact at a chosen game
frame after a hit, the way the measurements did, and reads what the trainer made of it:

1. Atlantis: after a whale hit in pass H, the ball goes into the whale in pass H+48 (a touch before the window: "EARLY 1",
   no hit) and in pass H+49 (a hit, gap 49, on time). The window line counts down 48 to 0 and then reads OPEN.
2. Prehistoric: a snowball in the boss in pass H+33 (lost, the boss is hurt, hits unchanged) and in pass H+34 (a hit, gap 34,
   on time).
3. Space: the game's own contact results are written (a shot's touch on a shoulder, the flag on the body), 19 and 20 passes
   after a hit: lost "1 EARLY" and a hit, gap 20, on time. Both the shoulder and the head.
4. Quiet: nothing is shown (the view is empty) with the switch off, in a level that is not a boss fight, in a pause and
   in the intro; the other trainers' state is not touched.

Pictures: bosstrainer_atlantis.png, bosstrainer_prehist.png, bosstrainer_space.png (needs --gfx angrylion)."""
import os
import struct
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "tools", "tricks"))
from tools import C_ALL, press, wait_for  # noqa: E402
from trainers import ACTOR_POS, ACTOR_PREV_POS, ACTOR_VEL, RUN_TRAINERS, VIEW_SIZE, Rig  # noqa: E402
from trainers_moon import boot_to_hub  # noqa: E402
from trainerlog import log_state  # noqa: E402


def toggle_trainer(emu, r, item, bit):
    """The Game menu's last item, Trainers: the item-th entry (0 first) is switched."""
    yield from press(emu, *C_ALL)
    for b in ("DOWN",) * 4 + ("A", "UP", "A") + ("DOWN",) * item + ("A", "B", "B"):
        yield from press(emu, b)
    yield from press(emu, *C_ALL)
    yield 20
    if not emu.r8(r.run + RUN_TRAINERS) & bit:
        emu.fail(f"the menu did not turn trainer bit {bit} on")

TR_BOSS = 0x10
ENEMY, ENEMY_STATE, ENEMY_FLAGS, ENEMY_SPIN = 0x300, 0xD4, 0x28, 0x94
BRAIN_SCRATCH, BRAIN_DELAY = 0x26C, 0x278
TOUCHED = 0x04000000
BOSS_OFFSET = 0x198      # TrainerView.boss
ACTION_WARP = 1          # practice/include/actions.h
RUN_WARP_LEVEL = 0x04
ATLANTIS_BOSS, PREHIST_BOSS, SPACE_BOSS3, ATLANTIS1 = 13, 28, 40, 10

BOSS_NONE, BOSS_ATLANTIS, BOSS_PREHIST, BOSS_SPACE = range(4)
BEK_NONE, BEK_HIT, BEK_SPLIT, BEK_KILL, BEK_EARLY, BEK_LOST = range(6)
BCH_WHALE, BCH_CRAB, BCH_FISH, BCH_PREHIST, BCH_LEFT, BCH_RIGHT, BCH_HEAD = range(7)
BWY_NONE, BWY_EARLY, BWY_HURT, BWY_SLOW, BWY_SMALL, BWY_UNKNOWN = range(6)
BWS_NONE, BWS_READY, BWS_CLOSED, BWS_OPEN = range(4)
KINDS = ["NONE", "HIT", "SPLIT", "KILL", "EARLY", "LOST"]
STATES = ["NONE", "READY", "CLOSED", "OPEN"]


class BossRig(Rig):
    def fcount(self):
        return self.emu.r32(self.sym("frameCount"))

    def enemy(self, i):
        return self.sym("enemies") + ENEMY * i

    def hits_addr(self):
        return self.emu.r32(self.enemy(0) + ENEMY_STATE)

    def boss_view(self):
        """The newest BossView as a dict. It was made in the game frame before the one that is next to run."""
        emu = self.emu
        seq = emu.r32(self.sym("trainerViewSeq"))
        raw = bytes(emu.read(self.sym("trainerViews") + (seq & 1) * VIEW_SIZE, VIEW_SIZE))
        on, valid = raw[4], raw[5]
        b = raw[BOSS_OFFSET:BOSS_OFFSET + 0x74]
        live, boss, hits, max_hits, p0, p1, p2, n_events, flags, phase, shots, n_win = struct.unpack_from(">12B", b, 0)
        frame2, last_frame2 = struct.unpack_from(">II", b, 0x0C)
        wins = [dict(state=b[0x14 + 4 * i], n=struct.unpack_from(">h", b, 0x16 + 4 * i)[0]) for i in range(3)]
        lasers, away, reload, crab, fish = struct.unpack_from(">5h", b, 0x20)
        radius, speed = struct.unpack_from(">ff", b, 0x2C)
        events = []
        for i in range(n_events):
            kind, chan, why, late, gap, n = struct.unpack_from(">BBBbhh", b, 0x34 + 8 * i)
            events.append(dict(kind=kind, chan=chan, why=why, late=late, gap=gap, n=n))
        return dict(seq=seq, on=on, valid=valid, live=live, boss=boss, hits=hits, max_hits=max_hits, part=[p0, p1, p2],
                    flags=flags, phase=phase, shots=shots, frame2=frame2, last_frame2=last_frame2, win=wins,
                    lasers=lasers, away=away, reload=reload, crab=crab, fish=fish, ball_radius=radius,
                    ball_speed=speed, events=events)

    def place_ball(self, pos, vel=(0, 0, 0)):
        for off in (ACTOR_POS, ACTOR_PREV_POS):
            self.setvec(self.ball + off, pos)
        self.setvec(self.ball + ACTOR_VEL, list(vel))

    def newpass(self, limit=80):
        """Waits until frameCount changes; returns the new value F (pass F - 1 is done, pass F is next)."""
        f = self.fcount()
        for _ in range(limit):
            yield 1
            n = self.fcount()
            if n != f:
                return n
        self.emu.fail("no new pass (frameCount %d, gameInfo %02x %02x, paused %d)" % (f, self.emu.r8(self.sym("gameInfo")), self.emu.r8(self.sym("gameInfo") + 1), self.emu.r8(self.sym("gamePaused"))))

    def platform(self, tag):
        head = self.sym("platformList") + 4
        p = self.emu.r32(head)
        for _ in range(400):
            if p == head:
                break
            if self.emu.r16s(p + 0x6C) == tag:
                return p
            p = self.emu.r32(p)
        self.emu.fail(f"no platform with tag {tag}")

    def moving_addr(self, tag):
        return self.emu.r32(self.platform(tag) + 0x38) + 0x50


def text_of(e):
    return f"{KINDS[e['kind']]} chan {e['chan']} why {e['why']} gap {e['gap']} late {e['late']} n {e['n']}"


def warp(r, level, min_frames=0):
    """The Warp menu's warp, as the practice ROM's own action does it."""
    emu = r.emu
    gi = r.sym("gameInfo")
    emu.w8(r.run + RUN_WARP_LEVEL, level)
    requests = r.sym("requests")
    emu.w16(requests, emu.r16(requests) | 1 << ACTION_WARP)
    yield 10
    yield from wait_for(emu, lambda: emu.r8(gi + 1) == level and emu.r8(gi) == 4 and r.fcount() < 60, 3000,
                        f"the warp to level {level}")


def level_ready(r, frames, control=True):
    """Waits until the intro is over (and, with CONTROL, the player has control: Glover never has it in the Space fight)."""
    emu = r.emu
    gi = r.sym("gameInfo")
    hc = r.sym("handControl")
    yield from wait_for(emu, lambda: r.fcount() >= frames and (not control or emu.r16s(hc + 0x24) <= 0) and
                        emu.r16(gi + 0x12) == 0, 9000,
                        "the intro to end")


def expect(emu, cond, msg):
    if not cond:
        emu.fail(msg)


def picture(r, name):
    """Two pictures after a few video frames: the frame buffer with more colours (fb_png) and the displayed frame (shot)."""
    yield 9
    r.picture(name)
    r.emu.shot(name + "_shot")


# ------------------------------------------------------------------------------------------------------------------
# Atlantis

def atlantis_check(r):
    emu = r.emu
    en = r.enemy(0)
    wa = en
    yield from level_ready(r, 420)
    emu.hold("Z")
    yield 6
    emu.release()
    yield 15
    emu.w8(r.run + RUN_TRAINERS, TR_BOSS)
    yield from r.newpass()
    yield from r.newpass()
    v = r.boss_view()
    expect(emu, v["live"] and v["boss"] == BOSS_ATLANTIS and v["on"] == TR_BOSS,
           f"the trainer is not live in the Atlantis boss: {v}")
    expect(emu, v["hits"] == 5 and v["max_hits"] == 5 and v["phase"] == 0, f"title data: {v}")
    # Before any hit the whale can be hit at any time.
    expect(emu, v["win"][0]["state"] == BWS_READY, f"the window before the first hit: {v['win'][0]}")
    yield from picture(r, "bosstrainer_atlantis_ready")
    print("bosstrainer: Atlantis, live before the first hit")

    def whale_pos():
        return r.vec(wa + ACTOR_POS)

    def inside():
        wp = whale_pos()
        r.place_ball([wp[0] + 30, wp[1] + 20, wp[2]], (-2, -2, 0))

    def park():
        wp = whale_pos()
        r.place_ball([wp[0] + 250, wp[1] + 200, wp[2]], (0, 0, 0))

    results = {}
    for d in (48, 49):
        st = r.hits_addr()
        emu.w8(st, 5)  # hits back to 5: the trainer starts again
        for _ in range(200):
            park()
            yield from r.newpass()
            if emu.r16s(wa + BRAIN_DELAY) == 0 and abs(emu.rf(wa + ENEMY_SPIN)) < 0.05:
                break
        # Hit 1, by keeping the ball inside the whale.
        F = r.fcount()
        for _ in range(40):
            if emu.r8(st) != 5:
                break
            inside()
            F = yield from r.newpass()
        expect(emu, emu.r8(st) == 4, f"trial {d}: the first hit did not come")
        H = F - 1
        views = {}
        hit = None
        while True:
            F = r.fcount()
            if F - H == d:
                inside()
            else:
                park()
            F2 = yield from r.newpass()
            views[F2 - 1 - H] = r.boss_view()  # made at the hook of pass F2 - 1
            if emu.r8(st) != 4:
                hit = F2 - 1 - H
                yield from r.newpass()
                views["after"] = r.boss_view()
                break
            if F2 - H > 80:
                break
        results[d] = (hit, views)
        print(f"bosstrainer: Atlantis contact at H+{d}: hit at H+{hit}")
        if d == 48:
            expect(emu, hit is None or hit > 49, f"the game took a hit at H+{hit} for a contact at H+48")
            # The window line counts the passes down and opens at H+49.
            for k in range(2, 49):
                w = views[k]["win"][0]
                expect(emu, w["state"] == BWS_CLOSED and w["n"] == 49 - k, f"H+{k}: window {w}, expected IN {49 - k}")
            expect(emu, views[48]["events"] and views[48]["events"][0]["kind"] == BEK_EARLY and
                   views[48]["events"][0]["n"] == 1, f"no TOUCH 1 EARLY at H+48: {[text_of(e) for e in views[48]['events']]}")
            w = views[49]["win"][0]
            expect(emu, w["state"] == BWS_OPEN and w["n"] == 0, f"H+49: window {w}, expected OPEN +0")
            w = views[51]["win"][0]
            expect(emu, w["state"] == BWS_OPEN and w["n"] == 2, f"H+51: window {w}, expected OPEN +2")
            ev = views[48]["events"]
            expect(emu, len(ev) == 2 or len(ev) == 1, f"unexpected events {[text_of(e) for e in ev]}")
        else:
            expect(emu, hit == 49, f"a contact at H+49 gave a hit at H+{hit}")
            e = views["after"]["events"][0]
            expect(emu, e["kind"] == BEK_HIT and e["chan"] == BCH_WHALE and e["gap"] == 49 and e["late"] == 0,
                   f"the hit at H+49 reads {text_of(e)}, expected HIT gap 49 late 0")
            expect(emu, all(x["kind"] != BEK_EARLY for x in views["after"]["events"]),
                   "a touch in the open window was called early")
            expect(emu, views["after"]["win"][0]["state"] == BWS_CLOSED and views["after"]["win"][0]["n"] >= 47,
                   f"after the hit the window should count down: {views['after']['win'][0]}")
            yield from picture(r, "bosstrainer_atlantis")
            print("bosstrainer: Atlantis hit at H+49 reads", text_of(e))
    # The ball inside for a longer time before the window gives one early event, not one for each pass.
    st = r.hits_addr()
    emu.w8(st, 5)
    for _ in range(200):
        park()
        yield from r.newpass()
        if emu.r16s(wa + BRAIN_DELAY) == 0 and abs(emu.rf(wa + ENEMY_SPIN)) < 0.05:
            break
    F = r.fcount()
    for _ in range(40):
        if emu.r8(st) != 5:
            break
        inside()
        F = yield from r.newpass()
    H = F - 1
    for k in range(1, 40):
        F = r.fcount()
        if 30 <= F - H <= 36:
            inside()
        else:
            park()
        yield from r.newpass()
    early = [e for e in r.boss_view()["events"] if e["kind"] == BEK_EARLY]
    expect(emu, len(early) <= 2, f"{len(early)} early events for one stay of the ball in the whale")
    print("bosstrainer: Atlantis, one early event for a long stay:", [text_of(e) for e in early])
    emu.w8(r.run + RUN_TRAINERS, 0)


# ------------------------------------------------------------------------------------------------------------------
# Prehistoric

def prehist_check(r):
    emu = r.emu
    sym = r.sym
    ea = r.enemy(0)
    yield from level_ready(r, 450)
    emu.w8(r.run + RUN_TRAINERS, TR_BOSS)
    emu.hold("Z")
    yield 6
    emu.release()
    yield 15
    yield from r.newpass()
    yield from r.newpass()
    v = r.boss_view()
    expect(emu, v["live"] and v["boss"] == BOSS_PREHIST, f"the trainer is not live in the Prehistoric boss: {v}")
    expect(emu, v["hits"] == 3 and v["max_hits"] == 3, f"title data: {v}")
    bo = emu.r32(r.ball + 0xD8)

    def set_snowball():
        emu.w32(sym("player") + 0x5C, 30)
        emu.wf(bo + 0x58 + 0x18, 12.0)
        emu.wf(bo + 0x58 + 0x1C, 12.0)

    def inside():
        bp = r.vec(ea + ACTOR_POS)
        r.place_ball([bp[0] + 40, bp[1] + 60, bp[2] + 30], (5, 0, 0))
        set_snowball()

    def park():
        bp = r.vec(ea + ACTOR_POS)
        r.place_ball([bp[0] + 200, bp[1] + 200, bp[2]], (0, 0, 0))
        set_snowball()

    st = r.hits_addr()
    an = emu.r32(ea + 0xDC)
    # Let the animation settle, then play both trials from the same state: after a lost touch the boss's animation
    # runs on differently, and the trials would not be alike.
    for _ in range(300):
        park()
        yield from r.newpass()
        if emu.r16(an + 2) not in (12, 3) and emu.r16(st + 0x30) == 0 and emu.r8(st) == 3:
            break
    state = emu.save("bosstrainer_prehist_start")
    yield 3
    for d in (33, 34):
        emu.load(state)
        yield 3
        yield from r.newpass()
        yield from r.newpass()
        # Hit 1 by a snowball.
        F = r.fcount()
        for _ in range(120):
            if emu.r8(st) != 3:
                break
            inside()
            F = yield from r.newpass()
        expect(emu, emu.r8(st) == 2, f"trial {d}: the first hit did not come")
        H = F - 1
        views = {}
        hit = None
        while True:
            F = r.fcount()
            if F - H == d:
                inside()
            else:
                park()
            F2 = yield from r.newpass()
            views[F2 - 1 - H] = r.boss_view()
            if emu.r8(st) != 2:
                hit = F2 - 1 - H
                yield from r.newpass()
                views["after"] = r.boss_view()
                break
            if F2 - H > d + 6:
                yield from r.newpass()
                views["after"] = r.boss_view()
                break
        print(f"bosstrainer: Prehistoric contact at H+{d}: hit at H+{hit}")
        # The window line: closed until the boss's animation ends.
        w = views[10]["win"][0]
        expect(emu, w["state"] == BWS_CLOSED and 20 <= w["n"] <= 25, f"H+10: window {w}, expected IN about 24")
        if d == 33:
            expect(emu, hit is None, f"the game took a hit at H+{hit} for a contact at H+33")
            lost = [e for e in views["after"]["events"] if e["kind"] == BEK_LOST]
            expect(emu, len(lost) == 1 and lost[0]["why"] == BWY_HURT and lost[0]["chan"] == BCH_PREHIST,
                   f"H+33: expected one LOST, boss hurt: {[text_of(e) for e in views['after']['events']]}")
            print("bosstrainer: Prehistoric H+33 reads", text_of(lost[0]))
            yield from picture(r, "bosstrainer_prehist_lost")
        else:
            expect(emu, hit == 34, f"a contact at H+34 gave a hit at H+{hit}")
            e = views["after"]["events"][0]
            expect(emu, e["kind"] == BEK_HIT and e["chan"] == BCH_PREHIST and e["gap"] == 34 and e["late"] == 0,
                   f"the hit at H+34 reads {text_of(e)}, expected HIT gap 34 late 0")
            expect(emu, all(x["kind"] != BEK_LOST for x in views["after"]["events"]),
                   "a hit was also called lost")
            print("bosstrainer: Prehistoric H+34 reads", text_of(e))
            yield from picture(r, "bosstrainer_prehist")
    # A touch with a ball that is not a snowball, and one that is too slow, in an open window.
    st = r.hits_addr()
    for label, radius, vel, why in (("small", 8.0, (30, 0, 0), BWY_SMALL), ("slow", 12.0, (1, 0, 0), BWY_SLOW)):
        # The ball's speed in the pass of the touch is the game's own (the floor can speed it up). A touch that the game
        # counts as a hit reads HIT, and is tried again.
        for attempt in range(6):
            # Wait until the boss is calm: the window is open and no touch is pending.
            emu.w8(st, 3)
            for _ in range(300):
                park()
                yield from r.newpass()
                if emu.r16(emu.r32(ea + 0xDC) + 2) not in (12, 3) and emu.r16(st + 0x30) == 0 and \
                        not emu.r32(ea + ENEMY_FLAGS) & TOUCHED:
                    break
            for _ in range(3):
                park()
                yield from r.newpass()
            mark = r.boss_view()["last_frame2"]
            bp = r.vec(ea + ACTOR_POS)
            emu.w32(sym("player") + 0x5C, 0)
            # The touch is the game's own flag, as in the Space check; the ball is far away with the chosen speed and size.
            r.place_ball([bp[0] + 200, bp[1] + 200, bp[2]], vel)
            emu.wf(bo + 0x58 + 0x18, radius)
            emu.wf(bo + 0x58 + 0x1C, radius)
            yield from r.newpass()
            emu.w32(ea + ENEMY_FLAGS, emu.r32(ea + ENEMY_FLAGS) | TOUCHED)
            for k in range(12):
                yield from r.newpass()
                if r.boss_view()["last_frame2"] != mark:
                    break
            v = r.boss_view()
            game_hit = emu.r8(st) != 3
            if v["events"] and v["events"][0]["kind"] == BEK_HIT:
                expect(emu, game_hit, f"{label} ball: HIT read, the game's hits are unchanged")
                continue
            break
        expect(emu, not game_hit, f"{label} ball: the game took a hit {attempt + 1} times")
        lost = [e for e in v["events"] if e["kind"] == BEK_LOST]
        expect(emu, lost and lost[0]["why"] == why, f"{label} ball: expected why {why}, events {[text_of(e) for e in v['events']]}")
        print(f"bosstrainer: Prehistoric {label} ball reads", text_of(lost[0]))
        bw = [l for l in log_state(emu, r.sym)[3] if l[1].startswith("BW ")]
        expect(emu, bw and "LOST" in bw[0][1], f"{label} ball: the trainer log's newest boss line is {bw[:1]}")
    emu.w8(r.run + RUN_TRAINERS, 0)


# ------------------------------------------------------------------------------------------------------------------
# Space

def space_check(r):
    emu = r.emu
    sym = r.sym
    ea = r.enemy(0)
    imm, ph = sym("immuneCount"), sym("portionHealth")
    yield from level_ready(r, 700, control=False)
    emu.w8(r.run + RUN_TRAINERS, TR_BOSS)
    yield from r.newpass()
    yield from r.newpass()
    v = r.boss_view()
    expect(emu, v["live"] and v["boss"] == BOSS_SPACE, f"the trainer is not live in the Space fight: {v}")
    expect(emu, v["part"] == [3, 3, 3], f"parts {v['part']}")
    expect(emu, v["win"][0]["state"] == BWS_READY and v["win"][1]["state"] == BWS_READY and
           v["win"][2]["state"] == BWS_NONE, f"windows {v['win']}")
    print("bosstrainer: Space, live, parts 3 3 3")
    yield from picture(r, "bosstrainer_space_ready")
    st = r.hits_addr()
    mv = r.moving_addr(1)

    def touch_shoulder():
        emu.w16(mv, 1)

    def touch_head():
        emu.w32(ea + ENEMY_FLAGS, emu.r32(ea + ENEMY_FLAGS) | TOUCHED)

    def quiet_boss():
        for _ in range(100):
            yield from r.newpass()
            if emu.r8(imm) == 0 and emu.r8(imm + 2) == 0:
                break

    for part, touch, label in ((0, touch_shoulder, "left shoulder"), (2, touch_head, "head")):
        for X in (19, 20):
            yield from quiet_boss()
            # A new state: the part's health goes up, and the trainer starts again.
            emu.w8(ph, 3)
            emu.w8(ph + 1, 3)
            emu.w8(ph + 2, 3)
            emu.w8(st, 1 if part == 2 else 3)
            if part == 2:
                emu.w8(ph, 0)
                emu.w8(ph + 1, 0)
            yield from r.newpass()
            touch()
            F = yield from r.newpass()
            H = F - 1
            expect(emu, emu.r8(imm + part) in (20, 19), f"{label}: no baseline hit (immune {emu.r8(imm + part)})")
            views = {}
            while True:
                F = r.fcount()
                if F == H + X:
                    touch()
                F2 = yield from r.newpass()
                views[F2 - 1 - H] = r.boss_view()
                if F2 - H > X + 3:
                    break
            health = emu.r8(ph + part)
            ev = views[X + 1]["events"]
            chan = BCH_LEFT if part == 0 else BCH_HEAD
            if X == 19:
                expect(emu, health == 2, f"{label} X={X}: the game took a hit (health {health})")
                lost = [e for e in ev if e["kind"] == BEK_LOST]
                expect(emu, len(lost) == 1 and lost[0]["chan"] == chan and lost[0]["why"] == BWY_EARLY and lost[0]["n"] == 1,
                       f"{label} X=19: expected LOST 1 EARLY, events {[text_of(e) for e in ev]}")
                print(f"bosstrainer: Space {label} X=19 reads", text_of(lost[0]))
            else:
                expect(emu, health == 1, f"{label} X={X}: the game did not take the hit (health {health})")
                e = ev[0]
                expect(emu, e["kind"] == BEK_HIT and e["chan"] == chan and e["gap"] == 20 and e["late"] == 0,
                       f"{label} X=20: expected HIT gap 20 late 0, events {[text_of(x) for x in ev]}")
                print(f"bosstrainer: Space {label} X=20 reads", text_of(e))
            # The window line: the countdown after the baseline hit.
            w = views[10]["win"][0 if part == 0 else 2]
            expect(emu, w["state"] == BWS_CLOSED and w["n"] == 10, f"{label} H+10: window {w}, expected IN 10")
            if part == 2 and X == 20:
                yield from picture(r, "bosstrainer_space")
    emu.w8(r.run + RUN_TRAINERS, 0)


# ------------------------------------------------------------------------------------------------------------------
# Quiet

def quiet_check(r):
    emu = r.emu
    # In a level that is not a boss fight nothing runs.
    yield from warp(r, ATLANTIS1)
    gi = r.sym("gameInfo")
    yield from wait_for(emu, lambda: emu.r8(gi) == 4 and r.fcount() >= 120, 9000, "Atlantis 1 to run")
    for _ in range(60):
        yield from r.newpass()
    emu.w8(r.run + RUN_TRAINERS, TR_BOSS)
    for _ in range(30):
        yield from r.newpass()
    v = r.boss_view()
    expect(emu, not v["live"], f"the trainer shows in Atlantis 1: {v}")
    print("bosstrainer: quiet in Atlantis 1")
    # In the boss fight with the switch off: no hook work and an empty view.
    yield from warp(r, ATLANTIS_BOSS)
    yield from level_ready(r, 420)
    yield from r.newpass()
    expect(emu, r.boss_view()["live"], "the trainer is not live in the Atlantis boss")
    # A pause shows nothing.
    emu.w8(r.sym("gamePaused"), 1)
    for _ in range(6):
        yield 3
    v = r.boss_view()
    expect(emu, not v["live"], f"the trainer shows in a pause: {v}")
    emu.w8(r.sym("gamePaused"), 0)
    for _ in range(4):
        yield from r.newpass()
    expect(emu, r.boss_view()["live"], "the trainer does not come back after the pause")
    print("bosstrainer: quiet in a pause, back after it")
    # Turned off: the view empties and the hook is not counted.
    emu.w8(r.run + RUN_TRAINERS, 0)
    for _ in range(4):
        yield from r.newpass()
    d0 = r.diag()
    for _ in range(10):
        yield from r.newpass()
    d1 = r.diag()
    expect(emu, not r.boss_view()["live"], "the view is not empty with the switch off")
    expect(emu, d1["hook_calls"] == d0["hook_calls"] and d1["steps"] == d0["steps"],
           f"the switch is off and the trainers still work: {d0} {d1}")
    print("bosstrainer: nothing runs with the switch off")
    # The menu has the switch: the last entry of the Trainers menu.
    emu.w8(r.run + RUN_TRAINERS, 0)
    yield from toggle_trainer(emu, r, 4, TR_BOSS)
    for _ in range(4):
        yield from r.newpass()
    print("bosstrainer: the Trainers menu's fifth entry turns the boss windows on")
    emu.w8(r.run + RUN_TRAINERS, 0)


def run(emu, sym, args):
    only = os.environ.get("BOSS_ONLY")
    names = only.split(",") if only else ["atlantis", "prehist", "space", "quiet"]
    r = BossRig(emu, sym)
    first = {"atlantis": ATLANTIS_BOSS, "prehist": PREHIST_BOSS, "space": SPACE_BOSS3, "quiet": ATLANTIS1}[names[0]]
    yield from boot_to_hub(emu, sym, first)
    for name in names:
        if name == "atlantis":
            if name != names[0]:
                yield from warp(r, ATLANTIS_BOSS)
            yield from atlantis_check(r)
        elif name == "prehist":
            if name != names[0]:
                yield from warp(r, PREHIST_BOSS)
            yield from prehist_check(r)
        elif name == "space":
            if name != names[0]:
                yield from warp(r, SPACE_BOSS3)
            yield from space_check(r)
        elif name == "quiet":
            yield from quiet_check(r)
