"""The Fear boss sweep (Inspect, Fear sweep: start): from the title, the sweep warps to Fear 3, takes its exit with
the thunder flashes it sets, lobs the ball in the Fear boss's opening and records both bursts. The test lets RUNS runs
finish (the environment variable SWEEP_RUNS, default 2) and prints each result. With SWEEP_EXPECT=FILE it compares
them with lines "rain=K ... ballPain=P bursts=F1:SEED1 F2:SEED2 result=..." from earlier emulator runs."""
import os
import re

from menunav import goto
from tools import press, wait_for

C_ALL = ("CUP", "CDOWN", "CLEFT", "CRIGHT")
RESULTS = 0x8077F000
SIZE = 0x18
STATUS = ["-", "miss", "HIT", "fail"]


def result(emu, k):
    a = RESULTS + k * SIZE
    return dict(rain=emu.r8(a), pain=emu.r8(a + 1), carried=emu.r8(a + 2), status=STATUS[emu.r8(a + 3) & 3],
                burst1=emu.r16s(a + 4), burst2=emu.r16s(a + 6), aura=emu.r16s(a + 8), end=emu.r16s(a + 10),
                seed1=emu.r32(a + 12), seed2=emu.r32(a + 16), nearest=emu.r16(a + 20) / 10)


def run(emu, sym, args):
    gi = sym("gameInfo")
    fc = sym("frameCount")
    sweep = sym("sweep")
    runs = int(os.environ.get("SWEEP_RUNS", "2"))

    yield from wait_for(emu, lambda: emu.r8(gi + 1) == 43 and emu.r32(fc) >= 200, 4000, "the logos")
    yield from press(emu, "START", frames=6)
    yield from wait_for(emu, lambda: emu.r8(gi + 1) == 44 and emu.r8(gi) == 4 and emu.r32(fc) >= 30, 3000,
                        "the title flythrough")
    yield from press(emu, *C_ALL)
    yield from goto(emu, sym, "Inspect", "Fear sweep: start")
    yield from wait_for(emu, lambda: emu.r8(sweep) != 0, 200, "the sweep to start")
    yield from wait_for(emu, lambda: emu.r8(sweep + 2) >= runs or emu.r8(sweep) == 0, 400000, f"{runs} runs")

    expect = {}
    if os.environ.get("SWEEP_EXPECT"):
        for line in open(os.environ["SWEEP_EXPECT"]):
            m = re.search(r"rain=(\d+) .*ballPain=(\d+) bursts=(\d+):(\w+) (\d+):(\w+) result=(\w+)", line)
            if m:
                expect[(int(m[1]), int(m[2]))] = (int(m[3]), int(m[4], 16), int(m[5]), int(m[6], 16), m[7])
    bad = 0
    for k in range(emu.r8(sweep + 2)):
        r = result(emu, k)
        line = (f"run {k}: rain {r['rain']} pain {r['pain']} carried {r['carried']} burst1 {r['burst1']}:"
                f"{r['seed1']:08x} burst2 {r['burst2']}:{r['seed2']:08x} aura {r['aura']} end {r['end']} "
                f"{r['status']} nearest {r['nearest']:.1f}")
        e = expect.get((r["rain"], r["pain"]))
        if e is not None:
            same = (e[0], e[1], e[2], e[3]) == (r["burst1"], r["seed1"], r["burst2"], r["seed2"])
            line += "  same as the emulator run" if same else f"  EMULATOR RUN: {e}"
            bad += not same
        print(line, flush=True)
        if r["status"] == "fail":
            bad += 1
    if bad:
        emu.fail(f"{bad} runs differ or failed")
