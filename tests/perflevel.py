"""One level of the performance baseline (tests/perf.py runs this for each level, and for the retail ROM).

Environment: PERF_LEVEL (the level number to start; default 12, Atlantis 3), PERF_ONLY (a comma list of group names
to run; default all), PERF_WINDOW (video interrupts measured for each scenario; default 120), PERF_NAMES (a comma list of scenario names
to run; default all of the groups), PERF_REPEAT (runs of each scenario, to see the noise; default 1).
Writes OUT/perf.json.

The level starts through the level select (gameInfo.mode 2), which the retail ROM has too. Each scenario writes the
practice code's switches in RAM, lets the game settle, then measures a window of video interrupts:

- the practice code's time (practice/include/perf.h): ticks of the CPU counter by stage, mean and longest;
- the game's pace: video interrupts for each game frame, from frameCount and __osViIntrCount (this works on the retail
  ROM too) and, on the practice ROM, the ROM's own histogram of the interrupts between game frames;
- the views' own counters (collStats.counts, hideStats.counts).
The input lag trials press C-up on controller 1 and count the video interrupts and game frames until pad[0] shows it.
The retail ROM runs the scenario "off" and the lag trials only."""
import json
import os
import random

import layout
import offsets
import trace
from menunav import Page, to_item  # noqa: F401
from tools import C_ALL, press, wait_for

ACTION_RESET, ACTION_WARP, ACTION_SAVE_STATE, ACTION_LOAD_STATE, ACTION_RECORD, ACTION_PLAY = 0, 1, 8, 9, 10, 11
READY = 2
PAD_BUTTON_CUP = 12  # pad[].button index of C-up
PAD_BUTTON = 0x1A    # Pad.button (practice/include/game.h)
PAD_SIZE = 0x190
HUD_NAMES = ["timer", "input", "glove", "ball", "action", "frames", "lag", "rng", "last", "game", "watch"]
HIDE_NAMES = ["landscape", "scenery", "water", "platforms", "glove", "ball", "enemies", "collect", "debris", "sky",
              "hud", "shadows", "footprints", "trails", "lightning"]
STAGES = ["clear", "input", "draw", "draw_all", "ctrl"]  # perf.h, PerfStageId

RUN_FIELDS = ["frameAdvance", "slowMotion", "hudHidden", "warpLevel", "rngFrozen", "rngValue", "collView", "collRadius",
              "collSpheres", "frameLog", "fogOff", "moonJump", "noclip", "noclipSpeed", "collEnemies", "collEdges",
              "actorsMod", "missedTaps", "chaos", "freeCam", "ghost", "trainers", "collHoles", "cameraSpeed",
              "padDisplay", "hide", "collFaces", "collColour", "collBack", "collWire", "collInfo"]
SETTINGS_FIELDS = ["menuPauses", "debugPad", "inputHistory", "hudShadow", "timerFrames"]
U16 = {"hide"}
U32 = {"rngValue"}


class Scenario:
    def __init__(self, group, name, run=(), settings=(), hud=(), special=None, lag=False, cache=False):
        self.group, self.name, self.run, self.settings, self.hud = group, name, dict(run), dict(settings), tuple(hud)
        self.special, self.lag, self.cache = special, lag, cache


def scenarios():
    """The scenarios in the order they run. A switch of the collision view is tried with the view on."""
    S = Scenario
    view = {"collView": 1}
    # The savestate scenario comes first: the first save of a level makes its base, and Record also saves.
    out = [S("off", "off"), S("state", "savestate", special="state")]
    out += [S("coll", "view see-through", view, lag=False, cache=True),
            S("coll", "view surfaces", {"collView": 2}, cache=True),
            S("coll", "view, radius 1000", {**view, "collRadius": 1}, cache=True),
            S("coll", "view, radius 500", {**view, "collRadius": 2}, cache=True),
            S("coll", "view, radius 250", {**view, "collRadius": 3}, cache=True),
            S("coll", "view + spheres", {**view, "collSpheres": 1}, cache=True),
            S("coll", "view + enemy spheres", {**view, "collSpheres": 1, "collEnemies": 1}, cache=True),
            S("coll", "view + open edges", {**view, "collEdges": 1}, cache=True),
            S("coll", "view + holes", {**view, "collHoles": 1}, cache=True),
            S("coll", "view + wire over", {**view, "collWire": 1}, cache=True),
            S("coll", "wire only", {**view, "collWire": 2}, cache=True),
            S("coll", "view + face info", {**view, "collInfo": 1}, cache=True),
            S("coll", "face info alone", {"collInfo": 1}, cache=True),
            S("coll", "view, colours by object", {**view, "collColour": 2}, cache=True),
            S("coll", "view, back faces hidden", {**view, "collBack": 1}, cache=True),
            S("coll", "view, back faces same", {**view, "collBack": 2}, cache=True),
            S("coll", "view, floors left out", {**view, "collFaces": 1}, cache=True),
            S("coll", "view, everything on", {**view, "collSpheres": 1, "collEnemies": 1, "collEdges": 1,
                                              "collHoles": 1, "collWire": 1, "collInfo": 1, "collColour": 2}, lag=True,
              cache=True)]
    out += [S("cache", "cache: view", view, special="cache"),
            S("cache", "cache: view + edges + enemies", {**view, "collEdges": 1, "collEnemies": 1, "collSpheres": 1},
              special="cache"),
            S("cache", "cache: everything", {**view, "collSpheres": 1, "collEnemies": 1, "collEdges": 1, "collHoles": 1,
                                             "collWire": 1, "collInfo": 1}, special="cache")]
    out += [S("hide", f"hide {n}", {"hide": 1 << i}) for i, n in enumerate(HIDE_NAMES)]
    out += [S("hide", "hide everything", {"hide": 0x7FFF})]
    out += [S("hud", f"HUD {n}", hud=[i]) for i, n in enumerate(HUD_NAMES)]
    out += [S("hud", "HUD input + history", settings={"inputHistory": 1}, hud=[1]),
            S("hud", "HUD all, shadow", settings={"hudShadow": 1, "timerFrames": 1, "inputHistory": 1},
              hud=range(11), lag=True)]
    out += [S("tools", "frame log", {"frameLog": 1}, lag=True),
            S("tools", "missed taps", {"missedTaps": 1}, lag=True),
            S("tools", "chaos rare", {"chaos": 1}), S("tools", "chaos often", {"chaos": 3}, lag=True),
            S("tools", "free camera", {"freeCam": 3}, lag=True),
            S("tools", "noclip", {"noclip": 1}, lag=True), S("tools", "moon jump", {"moonJump": 1}),
            S("tools", "fog off", {"fogOff": 1}),
            S("tools", "camera fast", {"cameraSpeed": 1}), S("tools", "camera faster", {"cameraSpeed": 2}),
            S("tools", "frozen random numbers", {"rngFrozen": 1, "rngValue": 0x6151F}),
            S("tools", "debug pad", settings={"debugPad": 1}, lag=True),
            S("tools", "MODIFIED marker", {"actorsMod": 1}),
            S("tools", "8 frozen pokes", special="pokes"),
            S("tools", "controller display (after a reset)", {"padDisplay": 1}, special="reset", lag=True)]
    out += [S("trainers", n, {"trainers": b}) for n, b in
            (("trainer L mash", 1), ("trainer line keeper", 2), ("trainer MBZ-L", 4), ("trainer Moon Pound aim", 8),
             ("trainer boss windows", 0x10), ("trainer wall clip", 0x40), ("all trainers", 0x5F))]
    out += [S("menu", "menu root, game paused", special="menu:()"),
            S("menu", "menu root, game runs", settings={"menuPauses": 0}, special="menu:()", lag=True),
            S("menu", "menu Actors view", settings={"menuPauses": 0}, special="menu:(5,0)"),
            S("menu", "menu Memory view", settings={"menuPauses": 0}, special="menu:(5,1)"),
            S("menu", "menu Frame log view", {"frameLog": 1}, settings={"menuPauses": 0}, special="menu:(5,3)")]
    out += [S("rec", "recording", special="record", lag=True), S("rec", "playback", special="play"),
            S("rec", "ghost", {"ghost": 1}, special="ghost")]
    out += [S("off", "off (again)")]
    # The survey of the collision view's buffers: only when PERF_ONLY names it (tests/perf.py --survey).
    out += [S("survey", "survey: everything", {**view, "collSpheres": 1, "collEnemies": 1, "collEdges": 1, "collHoles": 1,
                                               "collWire": 1, "collInfo": 1, "collColour": 2, "trainers": 8}, cache=True)]
    return out


def run(emu, sym, args):
    level_id = int(os.environ.get("PERF_LEVEL", "12"))
    only = os.environ.get("PERF_ONLY")
    only = set(only.split(",")) if only else None
    window = int(os.environ.get("PERF_WINDOW", "120"))
    practice_rom = sym.has("perf")
    gi = sym("gameInfo")
    fc = lambda: emu.r32(sym("frameCount"))  # noqa: E731
    vi = lambda: emu.r32(sym("__osViIntrCount"))  # noqa: E731
    mode = lambda: emu.r8(gi)  # noqa: E731
    level = lambda: emu.r8(gi + 1)  # noqa: E731
    desired = emu.r8(sym("desiredFrameRate"))
    result = {"level": level_id, "practice": practice_rom, "window": window, "scenarios": [], "lag": {}, "state": {},
              "cache": {}}

    if practice_rom:
        ro = offsets.offsets("actions.h", "Run", RUN_FIELDS)
        so = offsets.offsets("settings.h", "Settings", SETTINGS_FIELDS)
        po = offsets.offsets("perf.h", "Perf", ["interval", "lastVi", "gapTicks", "gapFrames"])
        run_addr, settings, perf = sym("run"), sym("settings"), sym("perf")
        practice = sym("practice")
        stats, hide_stats, requests = sym("collStats"), sym("hideStats"), sym("requests")
        recording = sym("recording")

        def wr(base, table, name, v):
            a = base + table[name]
            (emu.w16 if name in U16 else emu.w32 if name in U32 else emu.w8)(a, v)

        def clear_all():
            for n in RUN_FIELDS:
                if n not in ("warpLevel", "noclipSpeed"):
                    wr(run_addr, ro, n, 0)
            wr(run_addr, ro, "noclipSpeed", 1)
            for n in ("debugPad", "inputHistory", "hudShadow", "timerFrames"):
                wr(settings, so, n, 0)
            wr(settings, so, "menuPauses", 1)
            for i in range(layout.HUD_COUNT):
                emu.w8(settings + layout.SETTINGS_HUD + layout.HUD_PLACE_SIZE * i, 0)

        def request(action):
            emu.w16(requests, emu.r16(requests) | 1 << action)

    # Boot to the level through the level select.
    yield from wait_for(emu, lambda: level() == trace.PRESENT and fc() >= 200, 4000, "the logos")
    yield from press(emu, "START", frames=6)
    yield from wait_for(emu, lambda: level() == trace.FLYTHRU and mode() == 4 and fc() >= 60, 3000, "the title")
    emu.w8(gi, 2)
    yield 200
    emu.w32(sym("selectedLevel"), level_id)
    emu.hold("START")
    yield 40
    emu.release()
    yield from wait_for(emu, lambda: level() == level_id and mode() == 4 and fc() >= 60, 4000, f"level {level_id}")
    if practice_rom:
        clear_all()

    # ---- measuring ----
    def snap():
        s = {"vi": vi(), "fc": fc()}
        if practice_rom:
            s["stage"] = [(emu.r32(perf + 16 * i), emu.r32(perf + 16 * i + 4)) for i in range(5)]
            s["hist"] = [emu.r32(perf + po["interval"] + 4 * i) for i in range(8)]
            s["gap"] = (emu.r32(perf + po["gapTicks"]), emu.r32(perf + po["gapFrames"]))
        return s

    def mean_max(samples):
        return {"mean": sum(samples) / len(samples), "max": max(samples)} if samples else {"mean": 0, "max": 0}

    def measure(name, group, polls=None):
        if practice_rom:
            for i in range(5):
                emu.w32(perf + 16 * i + 8, 0)
        a = snap()
        coll, hide, info = [], [], []
        seen = emu.r32(practice + 4) if practice_rom else 0
        n = 0
        # The gaps between game frames, polled on every emulator frame: the same on the retail ROM and the practice ROM.
        gaps, last_fc, last_vi = [0] * 8, fc(), vi()
        while vi() - a["vi"] < window:
            yield 1
            n += 1
            f_now = fc()
            if f_now != last_fc:
                v_now = vi()
                gaps[min(v_now - last_vi, 7)] += 1
                last_fc, last_vi = f_now, v_now
            if n > window * 6:
                emu.fail(f"{name}: the window did not pass ({vi() - a['vi']} interrupts in {n} frames)")
            if practice_rom:
                d = emu.r32(practice + 4)
                if d != seen:
                    seen = d
                    coll.append(emu.r32(stats + 0x1C))
                    info.append(emu.r32(stats + 0x24))
                    hide.append(emu.r32(hide_stats + 0x18))
        b = snap()
        r = {"name": name, "group": group, "vi": b["vi"] - a["vi"], "game_frames": b["fc"] - a["fc"]}
        r["vi_per_frame"] = r["vi"] / r["game_frames"] if r["game_frames"] else None
        r["gaps"] = gaps
        if practice_rom:
            st = {}
            for i, sname in enumerate(STAGES):
                cnt = (b["stage"][i][0] - a["stage"][i][0]) & 0xFFFFFFFF
                tot = (b["stage"][i][1] - a["stage"][i][1]) & 0xFFFFFFFF
                st[sname] = {"n": cnt, "mean": tot / cnt if cnt else 0, "max": emu.r32(perf + 16 * i + 8), "sum": tot}
            r["stage"] = st
            r["hist"] = [(b["hist"][i] - a["hist"][i]) & 0xFFFFFFFF for i in range(8)]
            gt = (b["gap"][0] - a["gap"][0]) & 0xFFFFFFFF
            gn = (b["gap"][1] - a["gap"][1]) & 0xFFFFFFFF
            r["ticks_per_frame"] = gt / gn if gn else None
            r["coll"], r["info"], r["hide"] = mean_max(coll), mean_max(info), mean_max(hide)
            r["menu_open"] = emu.r8(sym("menuOpen"))
        result["scenarios"].append(r)
        late = ""
        if practice_rom:
            h = r["hist"]
            late = f" hist {h}"
        late += f" gaps {gaps}"
        print(f"perf {name}: {r['game_frames']} game frames in {r['vi']} interrupts"
              + (f" ({r['vi_per_frame']:.3f} each)" if r["vi_per_frame"] else "") + late)
        return r

    def settle(vi_n=30):
        v = vi()
        yield from wait_for(emu, lambda: vi() - v >= vi_n, vi_n * 6, "settling")

    def wait_cache():
        yield from wait_for(emu, lambda: emu.r8(stats + 0x10) == READY, 2400, "the collision cache")

    # ---- the input lag trials: C-up on controller 1 until pad[0] shows it ----
    def lag_trials(name, count=24):
        rng = random.Random(7)
        pad_btn = sym("pad") + PAD_BUTTON + PAD_BUTTON_CUP
        vis, fcs, missed = [], [], 0
        for _ in range(count):
            yield rng.randint(4, 11)  # a different phase against the game's frame each time
            v0, f0 = vi(), fc()
            emu.hold("CUP")
            got = False
            for _ in range(40):
                yield 1
                if emu.r8(pad_btn):
                    vis.append(vi() - v0)
                    fcs.append(fc() - f0)
                    got = True
                    break
            missed += not got
            emu.release()
            for _ in range(40):
                yield 1
                if not emu.r8(pad_btn):
                    break
        r = {"vi_mean": sum(vis) / len(vis) if vis else None, "vi_max": max(vis) if vis else None,
             "frames_mean": sum(fcs) / len(fcs) if fcs else None, "frames_max": max(fcs) if fcs else None,
             "trials": count, "missed": missed, "vi_all": vis}
        result["lag"][name] = r
        print(f"perf lag {name}: {r['vi_mean']} interrupts on average, {r['vi_max']} at most, "
              f"{r['frames_mean']} game frames, {missed} missed of {count}")

    # ---- the scenarios ----
    todo = [s for s in scenarios() if (only is None and s.group != "survey") or (only is not None and s.group in only)]
    if not practice_rom:
        todo = [s for s in todo if s.group == "off"][:1]
    names = os.environ.get("PERF_NAMES")
    if names:
        todo = [s for s in todo if s.name in names.split(",")]
    todo = [s for s in todo for _ in range(int(os.environ.get("PERF_REPEAT", "1")))]
    yield from settle(60)
    path = os.path.join(args.out, "perf.json")

    def save():
        with open(path, "w") as f:
            json.dump(result, f, indent=1)

    for sc in todo:
        save()
        if practice_rom:
            clear_all()
        if sc.group == "state":
            yield from state_group(emu, sym, result, request, fc, vi, wait_for, settle)
            continue
        if practice_rom:
            for n, v in sc.run.items():
                wr(run_addr, ro, n, v)
            for n, v in sc.settings.items():
                wr(settings, so, n, v)
            for i in sc.hud:
                emu.w8(settings + layout.SETTINGS_HUD + layout.HUD_PLACE_SIZE * i, 1)
            if sc.special == "pokes":
                co = sym("corrupt")
                for i in range(8):
                    emu.w32(co + 16 * i, sym("selectedLevel"))
                    emu.w32(co + 16 * i + 4, 5)
                    emu.w8(co + 16 * i + 12, 2)
                    emu.w8(co + 16 * i + 13, 4)
                    emu.w8(co + 16 * i + 14, 32)
                emu.w8(co + 0x94, 8)
            if sc.special == "cache":
                # A restart of the level makes the cache again. Count the interrupts and the late game frames from the
                # first frames of the level until the cache is ready.
                request(ACTION_RESET)
                yield from wait_for(emu, lambda: fc() < 20, 600, "the level to restart")
                a = snap()
                builds = emu.r16(stats + 0x1A)
                yield from wait_for(emu, lambda: emu.r8(stats + 0x10) == READY and emu.r16(stats + 0x1A) > builds,
                                    3000, "the collision cache")
                b = snap()
                hist = [(b["hist"][i] - a["hist"][i]) & 0xFFFFFFFF for i in range(8)]
                c = {"vi_to_ready": b["vi"] - a["vi"], "game_frames": b["fc"] - a["fc"],
                     "build_counts": emu.r32(stats + 0x20), "builds": emu.r16(stats + 0x1A) - builds,
                     "faces": emu.r16(stats), "shapes": emu.r16(stats + 0x12), "records": emu.r16(stats + 0x14),
                     "vertices": emu.r16(stats + 0x16), "commands": emu.r16(stats + 0x18), "hist": hist,
                     "full": emu.r8(stats + 0x11)}
                result["cache"][sc.name] = c
                print(f"perf {sc.name}: ready after {c['vi_to_ready']} interrupts ({c['game_frames']} game frames), "
                      f"{c['build_counts']} counts, hist {hist}")
                continue
            if sc.special == "reset":
                request(ACTION_RESET)
                yield from wait_for(emu, lambda: fc() < 20, 600, "the level to restart")
                yield from wait_for(emu, lambda: mode() == 4 and level() == level_id and fc() >= 40, 3000,
                                    "the level after the reset")
                yield from settle(150)
            if sc.special and sc.special.startswith("menu:"):
                menu_path = eval(sc.special[5:])
                yield from press(emu, *C_ALL, frames=12)
                if not emu.r8(sym("menuOpen")):
                    emu.fail("the menu did not open")
                for row in menu_path:
                    yield from to_item(emu, sym, row)
                    yield from press(emu, "A", frames=4)
            if sc.special == "record":
                request(ACTION_RECORD)
                yield from wait_for(emu, lambda: emu.r8(recording), 3000, "the recording to start")
            if sc.special in ("play", "ghost"):
                if emu.r32(recording + 4) < 120:
                    emu.fail("no recording of 120 game frames for the playback")
                if sc.special == "play":
                    request(ACTION_PLAY)
                    yield from wait_for(emu, lambda: emu.r8(recording + 1), 3000, "the playback to start")
                else:
                    # The ghost runs from the state that a load brings back; Record's own copy is not in a slot.
                    before = emu.r32(sym("stateSaves"))
                    request(ACTION_SAVE_STATE)
                    yield from wait_for(emu, lambda: emu.r32(sym("stateSaves")) > before, 3000, "the state save")
                    yield 5
                    before = emu.r32(sym("stateLoads"))
                    request(ACTION_LOAD_STATE)
                    yield from wait_for(emu, lambda: emu.r32(sym("stateLoads")) > before, 3000, "the state load")
            if "collView" in sc.run:
                yield from wait_cache()
        yield from settle()
        r = yield from measure(sc.name, sc.group)
        if sc.group == "survey" and practice_rom:
            # collStats: peaks since boot (0x28 on), and what the cache holds now.
            result["survey"] = {"moving": emu.r16(stats + 0x28), "holes": emu.r16(stats + 0x2A),
                                "spheres": emu.r16(stats + 0x2C), "scene": emu.r16(stats + 0x2E),
                                "fullEver": emu.r8(stats + 0x30), "shapes": emu.r16(stats + 0x12),
                                "records": emu.r16(stats + 0x14), "vertices": emu.r16(stats + 0x16),
                                "commands": emu.r16(stats + 0x18), "faces": emu.r16(stats),
                                "cacheState": emu.r8(stats + 0x10)}
            print(f"perf survey: {result['survey']}")
        if sc.lag and practice_rom:
            yield from lag_trials(sc.name)
        if sc.special == "record":
            # The recording goes on to 150 game frames: the playback and the ghost scenarios use it.
            f0 = fc()
            yield from wait_for(emu, lambda: emu.r32(recording + 4) >= 150 or fc() > f0 + 400, 2400, "150 game frames")
            request(ACTION_RECORD)
            yield from wait_for(emu, lambda: not emu.r8(recording), 300, "the recording to stop")
        if sc.special and sc.special.startswith("menu:"):
            for _ in range(4):
                yield from press(emu, "B", frames=4)
            if emu.r8(sym("menuOpen")):
                yield from press(emu, *C_ALL, frames=12)
        if sc.special == "play":
            yield from wait_for(emu, lambda: not emu.r8(recording + 1), 1500, "the playback to end")
        if sc.group == "off" and sc.name == "off":
            yield from lag_trials("off")
    if practice_rom:
        clear_all()
    save()
    print("perf: wrote " + path)


def state_group(emu, sym, result, request, fc, vi, wait_for, settle):
    """Savestate times: the first save in a level (makes the base), a second save, a load; and the stall in video
    interrupts and game frames from the request to the end."""
    out = []
    for what in ("save (first)", "save (second)", "load"):
        yield from settle(30)
        if what.startswith("save"):
            before = emu.r32(sym("stateSaves"))
            counter, ticks = "stateSaves", "stateSaveTicks"
            action = ACTION_SAVE_STATE
        else:
            before = emu.r32(sym("stateLoads"))
            counter, ticks = "stateLoads", "stateLoadTicks"
            action = ACTION_LOAD_STATE
        v0, f0 = vi(), fc()
        request(action)
        yield from wait_for(emu, lambda: emu.r32(sym(counter)) > before, 3000, what)
        yield from settle(30)
        t = emu.r32(sym(ticks))
        r = {"what": what, "ticks": t, "ms": t / 46875.0, "stall_vi": vi() - v0 - 30, "game_frames": fc() - f0}
        out.append(r)
        print(f"perf state {what}: {t} ticks ({r['ms']:.0f} ms)")
    result["state"] = out
