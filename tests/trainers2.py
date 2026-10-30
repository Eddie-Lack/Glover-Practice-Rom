"""The line keeper (practice/include/trainers.h) in Atlantis 1.

1. With random C taps on a diagonal D-pad, the heading error E, E + 2 Edot and the advice that the trainer shows follow the
   model (tools/tricks/dpad_trainer.py) on every frame. The C buttons are the ones that the frame log says the game read.
2. Walking with no C, and with the C taps that the model asks for with the zero delay: the heading drift a frame
   and the bend of the path.

Picture: trainers2_line.png (needs --gfx angrylion). TRAINERS_DEBUG=1 prints every frame."""
import math
import os
import struct
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "tools", "tricks"))
import dpad_trainer  # noqa: E402  the line keeper's model (tools/tricks/dpad_trainer.py)
from framelog import read_log  # noqa: E402
from trainers import (ACTOR_POS, ACTOR_ROT, RUN_TRAINERS, TR_LINE, Rig, boot_to_atlantis, get_joined,  # noqa: E402
                      step)
from trainerlog import log_state  # noqa: E402

VIEW_SIZE = 0x24C
CONT_CLEFT, CONT_CRIGHT = 0x0002, 0x0001
RUN_FRAME_LOG = 0x31
LINE_OFF, MOON_OFF = 0xE8, 0x134
LKW_NO_STICK, LKW_JUMPED = 3, 5   # practice/include/trainers.h


def read_view(r):
    seq = r.emu.r32(r.sym("trainerViewSeq"))
    raw = bytes(r.emu.read(r.sym("trainerViews") + (seq & 1) * VIEW_SIZE, VIEW_SIZE))
    frame2, on, valid = struct.unpack_from(">IBB", raw, 0)
    lv = raw[LINE_OFF:LINE_OFF + 0x4C]
    live, advice, why, cells, share, expect, walking = struct.unpack_from(">7B", lv, 0)
    err, rate, lead, tap, cross, speed, dist = struct.unpack_from(">7f", lv, 0x30)
    line = dict(live=live, advice=advice, why=why, cells=cells, share=share, expect=expect, walking=walking,
                strip=list(lv[8:48]), err=err, rate=rate, lead=lead, tap=tap, cross=cross, speed=speed, dist=dist)
    mv = raw[MOON_OFF:MOON_OFF + 0x64]
    mlive, mwhy, aim, has = struct.unpack_from(">4B", mv, 0)
    cell = list(mv[4:12])
    zone, flags, faces, cands, rebuilds, counts = struct.unpack_from(">2BHHHI", mv, 0x0C)
    f = struct.unpack_from(">16f", mv, 0x18)
    moon = dict(live=mlive, why=mwhy, aim=aim, has=has, cell=cell, zone=zone, flags=flags, faces=faces, cands=cands,
                rebuilds=rebuilds, counts=counts, col=f[0:2], vtx=f[2:5], target=f[5:7], dx=f[7], dz=f[8], dist=f[9],
                phase=f[10], fwd=f[11], right=f[12], stick=f[13:15], hover=f[15])
    moon["strip_counts"], moon["sims"], moon["calls"], moon["iters"] = struct.unpack_from(">IHHH", mv, 0x58)
    return dict(seq=seq, frame2=frame2, on=on, valid=valid, line=line, moon=moon)



ADV = {0: None, 1: "R", 2: "L"}


def cam_state(r):
    """The camera as the hook sees it: the state that the last game frame left."""
    e = r.emu
    ci = r.sym("cameraInfo")
    return dict(yaw=e.rf(r.cam + ACTOR_ROT + 4), vel=(e.rf(r.cam + 0x58), e.rf(r.cam + 0x60)),
                pos=(e.rf(r.cam + 0x34), e.rf(r.cam + 0x3C)), tgt=(e.rf(ci + 4 + 0x34), e.rf(ci + 4 + 0x3C)),
                ball=(e.rf(r.ball + ACTOR_POS), e.rf(r.ball + ACTOR_POS + 8)), mode=e.r32(ci + 0x1A4))


def walk(r, n, pick, stick=("UP", "RIGHT"), shots=None):
    """Walks for N game frames with the D-pad. PICK(k, view) gives the C buttons for frame k from the view of frame
    k - 1. Returns a row for each frame: the input, the view after it and the state before it (the state that the
    trainer saw)."""
    rows = []
    before = cam_state(r)
    view = read_view(r)
    for k in range(n):
        c = pick(k, view, before, rows[-1]['c'] if rows else ())
        yield from step(r, tuple(stick) + tuple(c))
        view = read_view(r)
        # The buttons the game read for this frame, from the frame log (the harness's timing is not exact).
        held = read_log(r.emu, r.sym)[-1][1]
        used = (("CLEFT",) if held & CONT_CLEFT else ()) + (("CRIGHT",) if held & CONT_CRIGHT else ())
        rows.append(dict(k=k, c=used, asked=tuple(c), view=view["line"], before=before))
        if os.environ.get("TRAINERS_DEBUG"):
            e = read_log(r.emu, r.sym)
            print("walk", k, "asked", c, "log frames", [x[0] for x in e[-3:]], "held", [hex(x[1]) for x in e[-3:]])
        before = cam_state(r)
        if shots and k in shots:
            r.picture(shots[k])
    r.emu.release()
    rows.append(dict(after=before))
    return rows


def model_rows(rows):
    """Feeds the model the states that the trainer saw. Returns (k, view, model E, model lead, model advice) for each
    frame on which the trainer is live."""
    tr = dpad_trainer.Trainer()
    out, armed, last_c = [], False, ()
    for row in rows:
        if "after" in row:
            break
        v, b = row["view"], row["before"]
        if not v["live"]:
            armed = False
        else:
            if not armed:
                tr.arm(b["yaw"])
                armed = True
                last_c = ()
            tapped = "R" if "CRIGHT" in last_c else "L" if "CLEFT" in last_c else None
            adv = tr.update(b["yaw"], b["vel"], b["pos"], b["tgt"], tapped)
            out.append((row["k"], v, tr.E, tr.lead, adv))
        last_c = row["c"]
    return out


def line_model_check(r, name, rows):
    emu = r.emu
    got = model_rows(rows)
    if len(got) < 20:
        emu.fail(f"{name}: the line keeper was live on only {len(got)} frames")
    worst_e = worst_lead = 0.0
    wrong = 0
    for k, v, e, lead, adv in got:
        if os.environ.get("TRAINERS_DEBUG"):
            print(k, rows[k]["c"], "E %.4f/%.4f lead %.4f/%.4f rate %.4f adv %s/%s tap %.4f" % (
                v["err"], e, v["lead"], lead, v["rate"], ADV[v["advice"]], adv, v["tap"]))
        worst_e = max(worst_e, abs(v["err"] - e))
        worst_lead = max(worst_lead, abs(v["lead"] - lead))
        if abs(abs(lead) - dpad_trainer.Trainer().thr) > 1e-3 and ADV[v["advice"]] != adv:
            wrong += 1
    print(f"trainers2: {name}: {len(got)} live frames, E differs from the model by {worst_e:.1e}, "
          f"E + 2 Edot by {worst_lead:.1e}, advice differs on {wrong}")
    if worst_e > 2e-4 or worst_lead > 5e-4 or wrong:
        emu.fail(f"{name}: the line keeper does not follow the model")
    return got


def line_check(r):
    """(b) The line keeper."""
    emu = r.emu
    emu.w8(r.run + RUN_FRAME_LOG, 1)
    emu.w8(r.run + RUN_TRAINERS, TR_LINE)
    import random
    rng = random.Random(3)

    # 1. Random C taps: the shown E, E + 2 Edot and the advice follow the model on every frame.
    yield from get_joined(r)
    pattern = [rng.choice([(), (), (), ("CLEFT",), ("CRIGHT",)]) for _ in range(60)]
    rows = yield from walk(r, 60, lambda k, v, b, c: pattern[k])
    got = line_model_check(r, "random taps", rows)
    if not any(v["advice"] for _, v, _, _, _ in got):
        emu.fail("the line keeper gave no advice in a run of random taps")

    # 2. Walking with no C, and with the C taps that the model asks for. The harness's input reaches the game one
    # frame after the call. A pilot that reads the view would act one frame late. The pilot runs the model on the
    # state that the trainer will see in frame k and presses for frame k + 1: the zero delay case, the least that a person can do.
    def drift(rows, a, b):
        ya, yb = rows[a]["before"]["yaw"], rows[b]["before"]["yaw"]
        return abs(dpad_trainer.wrap(yb - ya)) / (b - a)

    yield from get_joined(r)
    rows0 = yield from walk(r, 56, lambda k, v, b, c: ())
    latch = next(x["k"] for x in rows0 if x.get("view") and x["view"]["live"])
    yield from get_joined(r)
    model = dpad_trainer.Trainer()

    def pilot(k, v, before, last):
        if k < latch:
            return ()
        if k == latch:
            model.arm(before["yaw"])
        tapped = "R" if "CRIGHT" in last else "L" if "CLEFT" in last else None
        adv = model.update(before["yaw"], before["vel"], before["pos"], before["tgt"], tapped)
        return ("CRIGHT",) if adv == "R" else ("CLEFT",) if adv == "L" else ()

    rows1 = yield from walk(r, 56, pilot, shots={30: "trainers2_line"})
    def residual(rows, a, b):
        """The largest distance of the ball's path (frames A to B) from its best straight line, in units."""
        pts = [rows[k]["before"]["ball"] for k in range(a, b)]
        mx = sum(p[0] for p in pts) / len(pts)
        mz = sum(p[1] for p in pts) / len(pts)
        sxx = sum((p[0] - mx) ** 2 for p in pts)
        szz = sum((p[1] - mz) ** 2 for p in pts)
        sxz = sum((p[0] - mx) * (p[1] - mz) for p in pts)
        th = 0.5 * math.atan2(2 * sxz, sxx - szz)
        nx, nz = -math.sin(th), math.cos(th)
        return max(abs((p[0] - mx) * nx + (p[1] - mz) * nz) for p in pts)

    d0, d1 = drift(rows0, 12, 52), drift(rows1, 12, 52)
    c0, c1 = residual(rows0, 12, 52), residual(rows1, 12, 52)
    share = sum(1 for x in rows1[12:52] if x["c"]) / 40
    shown0, shown1 = rows0[52]["view"]["cross"], rows1[52]["view"]["cross"]
    print(f"trainers2: no taps: {d0:.4f} rad a frame, the path bends {c0:.1f} units from its best line (shown {shown0:.1f} off); "
          f"following the advice: {d1:.4f} rad a frame, {c1:.1f} units (shown {shown1:.1f} off), "
          f"C down on {share:.0%} of the frames")
    if os.environ.get("TRAINERS_DEBUG"):
        for x in rows1[:56]:
            v = x["view"]
            print(x["k"], x["c"], "E %.3f lead %.3f adv %d cross %.2f" % (v["err"], v["lead"], v["advice"], v["cross"]))
    if d1 > 0.002 or c1 > 6 or d0 < 3 * d1 or c0 < 3 * c1 or abs(shown1) > 15 or abs(shown0) < 3 * abs(shown1):
        emu.fail("following the line keeper does not keep the line")

    # 3. A stick on one axis only (the D-pad straight right) is not a diagonal: silent, with the reason NO_STICK.
    yield from get_joined(r)
    yield from walk(r, 20, lambda k, v, b, c: ())
    if not read_view(r)["line"]["live"]:
        emu.fail("the line keeper is not live after 20 frames on the diagonal")
    for _ in range(3):
        yield from step(r, ("RIGHT",))
    line = read_view(r)["line"]
    if line["live"] or line["why"] != LKW_NO_STICK:
        emu.fail(f"the line keeper is not silent on a straight D-pad: live {line['live']} why {line['why']}")
    lk = [l for l in log_state(emu, r.sym)[3] if l[1].startswith("LK ") and "DRIFT" in l[1]]
    if not lk:
        emu.fail(f"the end of a walk left no line in the trainer log: {log_state(emu, r.sym)[3]}")
    print(f"trainers2: the walk's end is in the trainer log: {lk[0][1]!r}")

    # 4. A teleport (a respawn, a checkpoint) is not a move: the line is dropped and the off-the-line sum stays.
    yield from get_joined(r)
    yield from walk(r, 20, lambda k, v, b, c: ())
    line = read_view(r)["line"]
    if not line["live"]:
        emu.fail("the line keeper is not live after 20 frames on the diagonal")
    emu.wf(r.ball + ACTOR_POS, emu.rf(r.ball + ACTOR_POS) + 100.0)
    yield from step(r, ("UP", "RIGHT"))
    after = read_view(r)["line"]
    if after["live"] or after["why"] != LKW_JUMPED or abs(after["cross"] - line["cross"]) > 30:
        emu.fail(f"the line keeper took a teleport for a move: live {after['live']} why {after['why']} "
                 f"off the line {line['cross']:.1f} to {after['cross']:.1f}")
    print("trainers2: a straight D-pad is silent; a teleport drops the line and leaves the drift as it was")




def run(emu, sym, args):
    r = Rig(emu, sym)
    yield from boot_to_atlantis(emu, sym)
    yield 60
    yield from line_check(r)
