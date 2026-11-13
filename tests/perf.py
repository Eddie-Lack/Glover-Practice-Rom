#!/usr/bin/env python3
"""The performance baseline: what each practice tool costs, in the heaviest levels.

    perf.py [--levels NAME,...] [--gfx rice|angrylion] [--md FILE] [--only GROUPS] [--window N] [--quick]

For each level it runs tests/perflevel.py in the emulator (one process for each level, and one for the retail ROM),
then writes a report. Levels: Atlantis 3, Carnival 1, Pirates 3, Out of this World 3 and the hub (the default); the
names are matched by their first letters. --only takes the scenario groups of perflevel.py (off, coll, cache, hide, hud,
tools, trainers, menu, rec, state). The report has:

- the frame budget, the practice code's time by thread for every scenario, the longest single call, the game's pace
  and late game frames, the input lag against the retail ROM, savestate times and the collision cache's build;
- the memory map of the practice RAM area (also written by itself with --memory).

--quick runs Atlantis 3 with a few scenarios and fails when the practice code costs more than the limits below, or a
game frame is late. The emulator (gloveworld/tools/emu/setup.sh) and a display on :99 are needed; run it under
`flock /tmp/emu.lock` when other jobs share the machine. The times are ticks of the CPU counter (46.875 MHz) in
mupen64plus, which counts the CPU's instructions and does not model the RSP, the RDP or memory waits. A console is
not measured."""
import argparse
import fcntl
import json
import os
import re
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)
import harness  # noqa: E402

LEVELS = [("Atlantis 3", 12), ("Carnival 1", 15), ("Pirates 3", 22), ("Out of this World 3", 37), ("Hub", 1)]
TICKS_PER_MS = 46875.0
PRACTICE_START, PRACTICE_END = 0x80400000, 0x80480000
STAGES = ["clear", "input", "draw", "ctrl"]  # the practice work, by thread (the retail work is not in these)
STAGE_NAMES = {"clear": "game, start of a message", "input": "game, around ProcessInput",
               "draw": "draw, after PrintTimers", "draw_all": "draw, whole list", "ctrl": "controller"}
# --quick fails over these limits (ticks): the mean practice work of a game frame in a scenario of the quick set, and the
# longest single call of the draw thread's practice work.
QUICK_ONLY = "off,coll,hide,hud,tools,trainers"
QUICK_MEAN_LIMIT = 700_000   # 15 ms of the 50 ms frame
QUICK_CALL_LIMIT = 1_000_000


def run_level(level, gfx, only, window, rom=None, elf=None):
    out = os.path.join(ROOT, "build", "tests", "perf" + ("_retail_" if rom else "") + str(level))
    env = dict(os.environ, PERF_LEVEL=str(level), PERF_WINDOW=str(window), DISPLAY=os.environ.get("DISPLAY", ":99"))
    if only:
        env["PERF_ONLY"] = only
    cmd = [sys.executable, os.path.join(HERE, "harness.py"), "perflevel", "--gfx", gfx, "--out", out]
    if rom:
        cmd += ["--rom", rom, "--elf", elf]
    proc = subprocess.run(cmd, env=env, capture_output=True, text=True)
    for line in proc.stdout.splitlines():
        if line.startswith("perf "):
            print(line, flush=True)
    if proc.returncode != 0:
        raise SystemExit(f"perflevel failed for level {level}:\n{proc.stdout[-1500:]}\n{proc.stderr[-1500:]}")
    return json.load(open(os.path.join(out, "perf.json")))


def cost(r, desired=3):
    """The practice work of one frame budget (a game frame at full speed), in ticks, by thread."""
    if "stage" not in r or not r["vi"]:
        return None
    scale = desired / r["vi"]
    return {k: r["stage"][k]["sum"] * scale for k in STAGES + ["draw_all"]}


def late_frames(r, desired):
    return sum(r["hist"][desired + 1:]) if "hist" in r else 0


# ---------------------------------------------------------------------------------------------------------
# The memory map

def _map_sections(map_path):
    """[(address, size, object path, kind)] of the input sections in the practice RAM area, from the linker map."""
    out = []
    pending = None
    kinds = ((".text", "text"), (".rodata", "rodata"), (".data", "data"), (".sdata", "data"), (".bss", "bss"),
             (".sbss", "bss"), ("COMMON", "bss"))
    for line in open(map_path):
        m = re.match(r"^ (\S+)\s*$", line)
        if m:
            pending = m.group(1)
            continue
        m = re.match(r"^ (\S+)\s+0x([0-9a-f]+)\s+0x([0-9a-f]+)\s+(\S+)$", line)
        if m:
            name, addr, size, obj = m.group(1), int(m.group(2), 16), int(m.group(3), 16), m.group(4)
        else:
            m = re.match(r"^\s+0x([0-9a-f]+)\s+0x([0-9a-f]+)\s+(\S+)$", line)
            if not m or pending is None:
                continue
            name, addr, size, obj = pending, int(m.group(1), 16), int(m.group(2), 16), m.group(3)
        pending = None
        if PRACTICE_START <= addr < PRACTICE_END and obj.endswith(".o") and size:
            kind = next((k for p, k in kinds if name.startswith(p)), None)
            if kind:
                out.append((addr, size, obj, kind))
    return out


PURPOSES = {
    "overlayList": "text overlay display lists of the HUD and the menu, one for each frame buffer",
    "bufs": "Moon Pound aim: the faces near the glove and the hole candidates, an active and a staging buffer",
    "cacheVtx": "collision view: the vertex cache of the level's meshes",
    "cacheGfx": "collision view: the display list commands of the cache",
    "scene": "collision view: the frame's display lists, one for each frame buffer",
    "movingMtx": "collision view: matrices of moving platforms, two frames",
    "holeVtx": "collision view: vertices of the Moon Pound hole posts, two frames",
    "staticMtx": "collision view: matrices of static actors",
    "shapes": "collision view: the cache's meshes",
    "sphereMtx": "collision view: matrices of the collision spheres, two frames",
    "lzWork": "savestate compressor: the match table",
    "baseSums": "savestate: the chunk sums of each base",
    "newSums": "savestate: the chunk sums of the base being made",
    "extra": "collision view: display list commands for open edges, spheres and contacts, two frames",
    "records": "collision view: one record for each cached actor",
    "atlas": "text: the font atlas",
    "cellSim": "Moon Pound aim: the work of the cell simulation",
    "threadStack": "missed taps: the extra controller reader's thread stack",
    "holeViews": "Moon Pound aim: the views that the draw thread draws",
    "frameLog": "frame log: one entry for each game frame of the glove",
    "idx": "Moon Pound aim: face indexes",
    "batch": "collision view: a batch of faces for a job",
    "bw": "boss windows: the state of the boss trainer",
    "trainerViews": "trick trainers: two views for the hand-off to the draw thread",
    "held": "trick trainers: the views of the trainers that hold a result",
    "kept": "save file editor / EEPROM: a copy of the save data",
    "stateSlots": "savestate: the slot table",
    "trainerLog": "trick trainers: the log of results",
    "contactVtx": "collision view: vertices of the contact faces, two frames",
    "faceBytes": "collision view: the faces of the mesh being built",
    "tabNotes": "Controller Pak: the note table", "tabIndex": "Controller Pak: the index table",
    "tabPage0": "Controller Pak: the ID area", "scratch": "Controller Pak: the second index table",
    "lists": "ghost: the ghost's display lists", "handVerts": "ghost: the glove's vertices",
    "ballVerts": "ghost: the ball's vertices", "modelView": "ghost: model-view matrices",
    "threadMem": "missed taps: the extra controller reader's thread", "actorSlots": "collision view: actor slot table",
    "local.0": "collision view: a hole view's local copy (static in a function)",
    "local.4": "trick trainers: a local copy of the log (static in a function)",
    "local.6": "trick trainers: a local copy of a view (static in a function)",
    "kept.1": "savestate: the game's save data, kept across a load",
}


def memory_map(elf=None, map_path=None):
    """The memory map of the practice RAM area as Markdown lines, and a dict of its sizes."""
    elf = elf or harness.PRACTICE_ELF
    map_path = map_path or os.path.join(ROOT, "build", "glover-practice.map")
    sections = _map_sections(map_path)
    # The sizes of each file come from its object. The linker merges equal strings of all files into one section, so
    # the totals come from the linker's own symbols instead.
    by_obj = {}
    objdir = os.path.join(ROOT, "build", "practice")
    for f in sorted(os.listdir(objdir)):
        if not f.endswith(".o"):
            continue
        size = subprocess.run(["mips-linux-gnu-size", "-A", os.path.join(objdir, f)], check=True, capture_output=True,
                              text=True).stdout
        row = {"text": 0, "rodata": 0, "data": 0, "bss": 0}
        for line in size.splitlines():
            p = line.split()
            if len(p) == 3 and p[1].isdigit():
                for prefix, kind in ((".text", "text"), (".rodata", "rodata"), (".data", "data"), (".sdata", "data"),
                                     (".bss", "bss"), (".sbss", "bss")):
                    if p[0].startswith(prefix):
                        row[kind] += int(p[1])
        by_obj[f[:-2] + ".c"] = row
    syms = subprocess.run(["mips-linux-gnu-nm", elf], check=True, capture_output=True, text=True).stdout
    sym = {l.split()[2]: int(l.split()[0], 16) for l in syms.splitlines() if len(l.split()) == 3}
    image = sym["practice_ROM_END"] - sym["practice_ROM_START"]   # code, rodata and data in the ROM and in RAM
    bss = sym["practice_BSS_END"] - sym["practice_BSS_START"]
    nm = subprocess.run(["mips-linux-gnu-nm", "-S", "-n", elf], check=True, capture_output=True, text=True).stdout
    symbols = []
    for line in nm.splitlines():
        p = line.split()
        if len(p) == 4:
            a, sz = int(p[0], 16), int(p[1], 16)
            if PRACTICE_START <= a < PRACTICE_END and sz >= 256 and p[2] in "bBdDrR":
                where = next((os.path.basename(o)[:-2] + ".c" for s, z, o, k in sections if k in ("bss", "data") and s <= a < s + z), "?")
                symbols.append((sz, a, p[2], p[3], where))
    symbols.sort(reverse=True)
    sums = {k: sum(r[k] for r in by_obj.values()) for k in ("text", "rodata", "data", "bss")}
    area = PRACTICE_END - PRACTICE_START
    totals = {"text": sums["text"], "data": sums["data"], "rodata": image - sums["text"] - sums["data"], "bss": bss}
    used = image + bss
    lines = ["## Memory map of the practice RAM area (0x80400000 to 0x80480000)", ""]
    lines += [f"Code and data (the part that the loader copies from the ROM) {image:,} bytes, bss {bss:,} bytes; together "
              f"{used:,} of {area:,} ({100 * used / area:.1f}%), **{area - used:,} bytes free**. Read-only data is "
              f"{totals['rodata']:,} bytes after the linker has merged equal strings (the files' own sums add up to "
              f"{sums['rodata']:,}).", ""]
    lines += ["### Code and data by source file", "",
              "Sizes from each file's object, before the link: code, read-only data (strings, tables), data and bss.", "",
              "| File | Code | Rodata | Data | Bss | Total |", "| --- | ---: | ---: | ---: | ---: | ---: |"]
    for name, r in sorted(by_obj.items(), key=lambda kv: -sum(kv[1].values())):
        lines.append(f"| {name} | {r['text']:,} | {r['rodata']:,} | {r['data']:,} | {r['bss']:,} | {sum(r.values()):,} |")
    lines += [f"| **all, as linked** | {totals['text']:,} | {totals['rodata']:,} | {totals['data']:,} | {totals['bss']:,} | "
              f"{used:,} |", ""]
    lines += ["### Data and bss symbols of 256 bytes or more", "", "| Symbol | Size | Address | File | Purpose |",
              "| --- | ---: | --- | --- | --- |"]
    for sz, a, t, name, where in symbols:
        lines.append(f"| `{name}` | {sz:,} | 0x{a:08X} | {where} | {PURPOSES.get(name, '')} |")
    big = sum(s[0] for s in symbols if s[2] in 'bB')
    lines += ["", f"The {len(symbols)} symbols above hold {big:,} bytes of the {bss:,} bytes of bss ({100 * big / bss:.0f}%). "
              f"The other {bss - big:,} bytes are in smaller symbols.", ""]
    lines += ["### The rest of the expansion RAM", "",
              "| Range | Use |", "| --- | --- |",
              "| 0x80480000 to 0x80747FFF | the savestate pool (2.8 MB) and its work tables |",
              "| 0x80748000 to 0x8074BFFF | the SD card test mailbox (test ROM only) |",
              "| 0x8074C000 to 0x8075BFFF | the work of a save and a load, the card stage (two blocks of 16 KB) |",
              "| 0x8075C000 to 0x807DBFFF | the ghosts (four slots of 128 KB) |",
              "| 0x807DC000 to 0x807FFFFF | the input recording (144 KB) |", ""]
    return lines, {"by_obj": by_obj, "totals": totals, "symbols": symbols}


# ---------------------------------------------------------------------------------------------------------
# The report

def fmt(n):
    return f"{n:,.0f}"


def pct(n, budget):
    return f"{100 * n / budget:.1f}%"


def report(results, retail, desired, lines_out):
    names = [n for n, _ in LEVELS if n in results]
    lv = lambda n: results[n]  # noqa: E731
    off = {n: next(s for s in lv(n)["scenarios"] if s["name"] == "off") for n in names if lv(n)["scenarios"]}
    ticks = [s["ticks_per_frame"] for s in off.values() if s.get("ticks_per_frame")]
    budget = sum(ticks) / len(ticks) if ticks else 2_343_750
    L = lines_out
    L += ["# Performance baseline", "",
          f"Practice ROM from the commit under test, measured in mupen64plus on {len(names)} levels. Times are ticks of "
          f"the CPU counter (46.875 MHz; 46,875 ticks are 1 ms). The emulator counts the CPU's instructions and does "
          f"not model the RSP, the RDP or memory waits, so a console takes longer.", ""]
    L += ["## Frame budget", "",
          f"- The game runs one game frame every {desired} video interrupts (60 a second, so 20 game frames a second).",
          f"- One video interrupt is 781,250 ticks. **One game frame at full speed is {fmt(budget)} ticks "
          f"({budget / TICKS_PER_MS:.1f} ms)**, measured between game frames ({fmt(budget)} ticks).",
          f"- The game thread and the draw thread share the CPU. A game frame is late when both together need more than "
          f"the budget: the next one then starts {desired + 1} interrupts later.",
          f"- Rules of thumb in ticks: 1% of a frame is {fmt(budget / 100)}; 1 ms is 46,875; a lag frame is visible "
          f"as a game frame of {desired + 1} or more interrupts.", ""]

    # The game without practice work.
    L += ["## Baseline: every feature off", "",
          "Mean and longest time of each stage of the practice code per call (ticks), and the game's own draw time.", "",
          "| Stage | " + " | ".join(names) + " |", "| --- | " + " | ".join("---:" for _ in names) + " |"]
    for st in STAGES + ["draw_all"]:
        label = STAGE_NAMES[st] + (" (the whole list, the game's own drawing too)" if st == "draw_all" else "")
        L.append(f"| {label}: mean / longest | " + " | ".join(
            f"{fmt(off[n]['stage'][st]['mean'])} / {fmt(off[n]['stage'][st]['max'])}" for n in names) + " |")
    L.append("| practice work per game frame, all threads | " + " | ".join(
        f"{fmt(sum(cost(off[n], desired)[k] for k in STAGES))} ({pct(sum(cost(off[n], desired)[k] for k in STAGES), budget)})"
        for n in names) + " |")
    L.append("| draw thread, whole list per game frame | " + " | ".join(
        f"{fmt(cost(off[n], desired)['draw_all'])} ({pct(cost(off[n], desired)['draw_all'], budget)})" for n in names) + " |")
    L.append("| video interrupts per game frame | " + " | ".join(f"{off[n]['vi_per_frame']:.3f}" for n in names) + " |")
    again = {n: next((s for s in lv(n)["scenarios"] if s["name"] == "off (again)"), None) for n in names}
    if all(again.values()):
        L.append("| the same at the end of the run | " + " | ".join(f"{again[n]['vi_per_frame']:.3f}" for n in names) + " |")
    if retail:
        L.append("| retail ROM, video interrupts per game frame | " + " | ".join(
            f"{retail[n]['scenarios'][0]['vi_per_frame']:.3f}" if n in retail else "" for n in names) + " |")
    L.append("")

    # Cost by scenario.
    scen = []
    for s in lv(names[0])["scenarios"]:
        if s["name"] not in ("off", "off (again)") and s["name"] not in [x["name"] for x in scen]:
            scen.append(s)
    for n in names[1:]:
        for s in lv(n)["scenarios"]:
            if s["name"] not in ("off", "off (again)") and s["name"] not in [x["name"] for x in scen]:
                scen.append(s)
    by = {n: {s["name"]: s for s in lv(n)["scenarios"]} for n in names}

    def table(title, cell, note=None, groups=None):
        L.extend([f"## {title}", ""] + ([note, ""] if note else []))
        L.append("| Scenario | " + " | ".join(names) + " |")
        L.append("| --- | " + " | ".join("---:" for _ in names) + " |")
        for s in scen:
            if groups and s["group"] not in groups:
                continue
            row = [cell(by[n].get(s["name"])) if n in by else "" for n in names]
            L.append(f"| {s['name']} | " + " | ".join(row) + " |")
        L.append("")

    def total(r):
        c = cost(r, desired)
        return None if c is None else sum(c[k] for k in STAGES)

    table("Practice work per game frame (ticks, and the share of the frame budget)",
          lambda r: "" if r is None or total(r) is None else f"{fmt(total(r))} ({pct(total(r), budget)})",
          "The mean time of the practice code on all threads during one frame budget (the controller thread and the "
          "two game thread stages, the draw thread's work after the game's PrintTimers). With the menu open and the "
          "game paused there are no game frames; the time is per frame budget all the same.")
    table("Where the time goes: game thread, draw thread, controller thread (ticks per game frame)",
          lambda r: "" if r is None or "stage" not in r else " / ".join(
              fmt(cost(r, desired)[k]) for k in ("clear", "draw", "ctrl")) + (
              f" + input {fmt(cost(r, desired)['input'])}" if cost(r, desired)["input"] > 100 else ""),
          "Game thread (start of a message) / draw thread / controller thread, and the game thread's work around "
          "ProcessInput when it is not tiny.")
    table("Longest single call (ticks): draw thread's practice work, whole draw list",
          lambda r: "" if r is None or "stage" not in r else
          f"{fmt(r['stage']['draw']['max'])} / {fmt(r['stage']['draw_all']['max'])}",
          "Draw thread: the practice work after PrintTimers / the whole list. One call is one drawn frame. A load or a "
          "save can show as a long call. The longest call has only the window's few frames behind it.")
    table("Longest single call (ticks): game thread and controller thread",
          lambda r: "" if r is None or "stage" not in r else
          f"{fmt(r['stage']['clear']['max'])} / {fmt(r['stage']['input']['max'])} / {fmt(r['stage']['ctrl']['max'])}",
          "Start of a message / around ProcessInput / controller thread. The draw thread runs above the game thread and "
          "can add its time to the game thread's maximum.")
    table("The views' own counters (ticks of the draw thread, mean / longest per frame)",
          lambda r: "" if r is None or "coll" not in r else (
              f"view {fmt(r['coll']['mean'])} / {fmt(r['coll']['max'])}" if r["coll"]["max"] else "") + (
              f" face info {fmt(r['info']['mean'])} / {fmt(r['info']['max'])}" if r["info"]["max"] else "") + (
              f" hide {fmt(r['hide']['mean'])} / {fmt(r['hide']['max'])}" if r["hide"]["max"] else ""),
          "collStats.counts, collStats.infoCounts and hideStats.lastCounts, sampled on each drawn frame. They are part "
          "of the draw thread's work above (the hide counter is inside the game's own drawing, not the practice stage).")

    # Pace.
    L += ["## Frame rate: late game frames", "",
          f"A game frame is late when it starts more than {desired} video interrupts after the one before. The count is "
          f"over the window of each scenario ({lv(names[0])['window']} interrupts).", ""]
    bad = []
    for n in names:
        for s in lv(n)["scenarios"]:
            if "hist" in s and (late_frames(s, desired) or (s["vi_per_frame"] and abs(s["vi_per_frame"] - desired) > 0.01
                                                           and s["game_frames"])):
                bad.append(f"- {n}, {s['name']}: {s['game_frames']} game frames in {s['vi']} interrupts, "
                           f"histogram of the gaps {s['hist']}, {late_frames(s, desired)} late")
    L += bad if bad else [f"No scenario had a late game frame: every game frame came {desired} interrupts after the "
                           f"one before, in every level."]
    L.append("")

    # Input lag.
    L += ["## Input lag", "",
          "C-up is held on controller 1 at the end of a video frame; the trial counts the video interrupts and game "
          "frames until `pad[0]` shows it (24 trials; the harness's callbacks are phase-locked to the game frame, so "
          "this finds whole frames of lag, not parts of one). The retail ROM is the reference.", "",
          "| Scenario | " + " | ".join(f"{n}" for n in names) + " |", "| --- | " + " | ".join("---:" for _ in names) + " |"]
    lagnames = []
    for n in names:
        for k in lv(n)["lag"]:
            if k not in lagnames:
                lagnames.append(k)
    cellf = lambda r: "" if not r else (f"{r['vi_mean']:.2f} / {r['vi_max']} VI, {r['frames_mean']:.2f} frames, "
                                        f"{r['missed']} missed" if r["vi_mean"] is not None else f"none seen ({r['missed']} missed)")  # noqa: E731
    if retail:
        L.append("| retail ROM | " + " | ".join(cellf(retail[n]["lag"].get("off")) if n in retail else "" for n in names) + " |")
    for k in lagnames:
        L.append(f"| {k} | " + " | ".join(cellf(lv(n)["lag"].get(k)) for n in names) + " |")
    L += ["", "Cells: mean / longest in video interrupts, mean in game frames, trials with no response.", ""]

    # Savestates and cache.
    L += ["## Savestates", "", "| What | " + " | ".join(names) + " |", "| --- | " + " | ".join("---:" for _ in names) + " |"]
    for i, what in enumerate(("save (first)", "save (second)", "load")):
        row = []
        for n in names:
            st = lv(n)["state"]
            row.append(f"{fmt(st[i]['ticks'])} ticks, {st[i]['ms']:.0f} ms; stall {st[i]['stall_vi']} VI, "
                       f"{st[i]['game_frames']} game frames" if len(st) > i else "")
        L.append(f"| {what} | " + " | ".join(row) + " |")
    L += ["", "The ticks are the ROM's own (`stateSaveTicks`, `stateLoadTicks`) and do not include the frame that shows "
          "\"saving...\". The stall is from the request to 30 interrupts after the end.", ""]
    L += ["## Collision view: cache build after a level restart", "",
          "| Build | Level | Interrupts to ready | Build ticks | Shapes / records / vertices / commands | Gaps by interrupts |",
          "| --- | --- | ---: | ---: | --- | --- |"]
    for n in names:
        for k, c in lv(n)["cache"].items():
            L.append(f"| {k} | {n} | {c['vi_to_ready']} ({c['game_frames']} game frames) | {fmt(c['build_counts'])} "
                     f"({c['build_counts'] / TICKS_PER_MS:.0f} ms) | {c['shapes']} / {c['records']} / {c['vertices']} / "
                     f"{c['commands']} | {c['hist']} |")
    L.append("")
    return budget


def quick_check(results, desired):
    """The checks of --quick: returns a list of failures."""
    errors = []
    for n, res in results.items():
        for s in res["scenarios"]:
            if "stage" not in s:
                continue
            c = cost(s, desired)
            mean = sum(c[k] for k in STAGES)
            if mean > QUICK_MEAN_LIMIT:
                errors.append(f"{n}, {s['name']}: the practice work is {mean:,.0f} ticks a frame (limit {QUICK_MEAN_LIMIT:,})")
            if s["stage"]["draw"]["max"] > QUICK_CALL_LIMIT:
                errors.append(f"{n}, {s['name']}: one draw call took {s['stage']['draw']['max']:,} ticks")
            if s["game_frames"] and late_frames(s, desired):
                errors.append(f"{n}, {s['name']}: {late_frames(s, desired)} late game frames")
            if s["name"] == "off" and mean > 0.01 * 2_343_750:
                errors.append(f"{n}: with every feature off the practice work is {mean:,.0f} ticks a frame (over 1%)")
    return errors


# Every level that has a world to draw: the Level enum of gloveworld (enums.h), HUB1 to SPACEBONUS.
SURVEY_LEVELS = [("Hub %d" % (i + 1), i) for i in range(8)] + [("Cave", 8), ("A course", 9)] + [
    (n, 10 + i) for i, n in enumerate(
        "Atlantis 1,Atlantis 2,Atlantis 3,Atlantis boss,Atlantis bonus,Carnival 1,Carnival 2,Carnival 3,Carnival boss,"
        "Carnival bonus,Pirates 1,Pirates 2,Pirates 3,Pirates boss,Pirates bonus,Prehistoric 1,Prehistoric 2,"
        "Prehistoric 3,Prehistoric boss,Prehistoric bonus,Fear 1,Fear 2,Fear 3,Fear boss,Fear bonus,Space 2A,Space 2B,"
        "Space 3,Space boss 1,Space boss tween,Space boss 3,Space bonus".split(","))]
SURVEY_KEYS = ("moving", "holes", "spheres", "scene", "shapes", "records", "vertices", "commands", "faces")


def survey(a):
    """One run of the 'survey' scenario in each level: the peaks of what the collision view's fixed buffers hold. Writes a
    table, and the highest value of each column, to the --md file."""
    os.environ.setdefault("SDL_AUDIODRIVER", "dummy")
    want = [w.strip().lower() for w in a.levels.split(",")] if a.levels else None
    rows, failed = [], []
    for name, number in SURVEY_LEVELS:
        if want and not any(name.lower().startswith(w) for w in want):
            continue
        print(f"perf survey: {name} (level {number})", flush=True)
        try:
            with open("/tmp/emu.lock", "w") as lock:  # one level at a time, so other emulator jobs can run between
                fcntl.flock(lock, fcntl.LOCK_EX)
                res = run_level(number, a.gfx, "survey", a.window if a.window != 120 else 300, a.rom, a.elf)
        except SystemExit as e:
            failed.append((name, str(e).splitlines()[0]))
            print(f"perf survey: {name} failed: {failed[-1][1]}", flush=True)
            continue
        if "survey" in res:
            rows.append((name, res["survey"]))
    lines = ["# Collision view buffers: the survey", "",
             "Whole-level view with every switch on. Peaks over the run of each level.", "",
             "| Level | " + " | ".join(SURVEY_KEYS) + " | full |", "| --- | " + " | ".join("---:" for _ in SURVEY_KEYS) + " | ---: |"]
    for name, r in rows:
        lines.append(f"| {name} | " + " | ".join(str(r[k]) for k in SURVEY_KEYS) + f" | {r['fullEver']} |")
    top = {k: max((r[k] for _, r in rows), default=0) for k in SURVEY_KEYS}
    lines.append("| **highest** | " + " | ".join(f"**{top[k]}**" for k in SURVEY_KEYS) + " | |")
    lines += ["", "Failed: " + (", ".join(f"{n} ({why})" for n, why in failed) if failed else "none")]
    open(a.md, "w").write("\n".join(lines) + "\n")
    print("\n".join(lines))
    return 0


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--levels", default=None, help="names, comma list (first letters are enough)")
    ap.add_argument("--gfx", default="rice", choices=["rice", "angrylion"])
    ap.add_argument("--md", default=os.path.join(ROOT, "build", "perf", "baseline.md"))
    ap.add_argument("--only", default=None)
    ap.add_argument("--window", type=int, default=120)
    ap.add_argument("--quick", action="store_true")
    ap.add_argument("--memory", action="store_true", help="only write the memory map")
    ap.add_argument("--no-retail", action="store_true")
    ap.add_argument("--rom", default=None, help="--survey: a practice ROM other than build/glover-practice.z64")
    ap.add_argument("--elf", default=None, help="--survey: the ELF of --rom")
    ap.add_argument("--survey", action="store_true",
                    help="the peaks of the collision view's buffers in every level (whole-level view, every switch on)")
    a = ap.parse_args()
    os.makedirs(os.path.dirname(a.md), exist_ok=True)
    if a.memory:
        lines, _ = memory_map()
        open(a.md, "w").write("\n".join(lines) + "\n")
        print("\n".join(lines))
        return 0
    if a.survey:
        return survey(a)
    levels = LEVELS
    only = a.only
    if a.quick:
        levels = LEVELS[:1]
        only = only or QUICK_ONLY
    if a.levels:
        want = [w.strip().lower() for w in a.levels.split(",")]
        levels = [l for l in LEVELS if any(l[0].lower().startswith(w) for w in want)]
    os.environ.setdefault("SDL_AUDIODRIVER", "dummy")
    results, retail = {}, {}
    for name, number in levels:
        print(f"perf: {name} (level {number})", flush=True)
        results[name] = run_level(number, a.gfx, only, a.window)
        if not a.no_retail and not a.quick:
            retail[name] = run_level(number, a.gfx, "off", a.window, harness.RETAIL_ROM, harness.RETAIL_ELF)
    desired = 3
    lines = []
    budget = report(results, retail, desired, lines)
    mem, _ = memory_map()
    lines += mem
    open(a.md, "w").write("\n".join(lines) + "\n")
    json.dump({"results": results, "retail": retail}, open(os.path.splitext(a.md)[0] + ".json", "w"))
    print(f"perf: wrote {a.md}; the frame budget is {budget:,.0f} ticks")
    if a.quick:
        errors = quick_check(results, desired)
        for e in errors:
            print("FAIL perf: " + e)
        print("PASS perf" if not errors else f"FAIL perf: {len(errors)} limits passed")
        return 1 if errors else 0
    return 0


if __name__ == "__main__":
    sys.exit(main())
