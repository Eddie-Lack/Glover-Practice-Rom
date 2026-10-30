#!/usr/bin/env python3
"""The faithfulness test: plays the same route (trace.py) on the retail ROM and on the practice ROM, and compares.

    faithful.py [--with-features] [--reuse-retail]

The practice code costs a few CPU cycles on each frame. During a load the game's own work fills the CPU, so a
load can end a video frame earlier or later, depending on where the boundary falls. This does not change
gameplay (CLAUDE.md). The route therefore keys its input to the game's frames, and the test checks four things
separately:

- logic: in each level, the game's state on every game frame is the same as retail's (any of the video frames of
  a game frame may match, and a difference that is gone on the next sampled game frame is a sample taken in the
  middle of a game frame's work);
- lag in play: in each level, every game frame falls on the same video frame, counted from the level's first.
  A shift here would be input lag or a stutter, so none is allowed;
- the picture: in each level, the coarse picture of both frame buffers (trace.py, picture()) is within
  PICTURE_TOLERANCE on every game frame, by the same rule as the logic (only with the features off: the features
  draw on the screen);
- RAM: at the RAM checks (keyed to game frames), the RAM is the same outside the areas that record time and the
  picture's buffers (ramcheck.py). Words whose 16-bit halves differ by 1 at most are reported, not failed.

It reports how many video frames each load took on both ROMs; a difference there does not fail the test.

--with-features also runs the practice ROM with its default features on, and reports the same comparison.
The traces go to build/tests/faithful/. All runs use angrylion: the video plugin changes the game's timing in the
emulator, so traces from different plugins differ."""
import argparse
import json
import os
import pickle
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import harness  # noqa: E402
import ramcheck  # noqa: E402
import trace  # noqa: E402

OUT = os.path.join(harness.ROOT, "build", "tests", "faithful")


def record(name, rom, elf, features="off"):
    out = os.path.join(OUT, name)
    cmd = [sys.executable, os.path.join(HERE, "harness.py"), "trace", "--rom", rom, "--elf", elf, "--out", out,
           "--gfx", "angrylion", "--features", features]
    subprocess.run(cmd, check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    return load(name)


def load(name):
    out = os.path.join(OUT, name)
    return json.load(open(os.path.join(out, "trace.json"))), pickle.load(open(os.path.join(out, "ram.pickle"), "rb"))


def levels(t):
    """The levels played, in order: (level, first video frame, {game frame: (video frames since the level's
    first game frame, [the state on each video frame of that game frame])}). A level is a run of video frames in
    gameInfo.mode 4 with frameCount >= 1."""
    out = []
    current = None
    for v, s in enumerate(t):
        fc, mode, level = s[0], s[1], s[2]
        playing = mode == 4 and level < 43 and fc >= 1
        if playing and (current is None or current[0] != level):
            current = (level, v, {})
            out.append(current)
        elif not playing:
            current = None
        if current is not None:
            if fc not in current[2]:
                current[2][fc] = (v - current[1], [])
            current[2][fc][1].append(s)
    return out


LOGIC = len(trace.FIELDS)  # a sample is the logic's fields, then the coarse picture of each frame buffer
# The most a block of the coarse picture may differ, in 1/8ths of a 5-bit level (trace.py, picture()).
PICTURE_TOLERANCE = 4


def exact(x, y):
    return x == y


def similar(x, y):
    return max(abs(p - q) for p, q in zip(x, y)) <= PICTURE_TOLERANCE


def describe(fields_a, fields_b):
    return "; ".join(f"{name}: retail {trace.unpack(name, x)}, practice {trace.unpack(name, y)}"
                     for name, x, y in zip(trace.FIELDS, fields_a, fields_b) if x != y)


def differences(common, frames_a, frames_b, part, same=exact):
    """The game frames whose samples differ in part (a slice of the sample), and the number that differ only in
    the middle of a game frame. The game thread may be in the middle of a game frame when the emulator takes a
    video frame's sample, and the practice code's few cycles move that point. So a game frame counts as the same
    when any of its samples match. During loads at the start of a level the game thread can rest in the middle of
    a game frame for several video frames: a difference there that is gone on the next sampled game frame is such
    a sample, and one that stays is a real difference."""
    differ = [fc for fc in common
              if not any(same(x[part], y[part]) for x in frames_a[fc][1] for y in frames_b[fc][1])]
    real = [fc for fc in differ if common.index(fc) + 1 == len(common) or common[common.index(fc) + 1] in differ]
    return real, len(differ) - len(real)


def compare(label, retail, practice, retail_ram=None, practice_ram=None, check_picture=True):
    """Prints the comparison and returns True if the logic, the lag in play, the picture (when check_picture) and
    the RAM are the same."""
    ok = True
    a, b = levels(retail), levels(practice)
    if [x[0] for x in a] != [x[0] for x in b]:
        print(f"FAIL faithful ({label}): different levels played: {[x[0] for x in a]} and {[x[0] for x in b]}")
        return False
    loads = []
    for (level, start_a, frames_a), (_, start_b, frames_b) in zip(a, b):
        loads.append(f"level {level} starts on video frame {start_a} and {start_b}")
        common = sorted(set(frames_a) & set(frames_b))
        logic, transient = differences(common, frames_a, frames_b, slice(0, LOGIC))
        lag = [fc for fc in common if frames_b[fc][0] != frames_a[fc][0]]
        picture, _ = (differences(common, frames_a, frames_b, slice(LOGIC, None), similar) if check_picture
                      else ([], 0))
        if logic:
            fc = logic[0]
            print(f"FAIL faithful ({label}): level {level}, game frame {fc} (and {len(logic) - 1} more): "
                  + describe(frames_a[fc][1][-1], frames_b[fc][1][-1]))
            ok = False
        if lag:
            fc = lag[0]
            print(f"FAIL faithful ({label}): level {level}: game frame {fc} falls on video frame "
                  f"{frames_a[fc][0]} of the level in retail and {frames_b[fc][0]} in the practice ROM "
                  f"(and {len(lag) - 1} more game frames are shifted)")
            ok = False
        if picture:
            print(f"FAIL faithful ({label}): level {level}: the picture differs on game frame {picture[0]} "
                  f"(and {len(picture) - 1} more)")
            ok = False
        if not logic and not lag and not picture:
            print(f"PASS faithful ({label}): level {level}: the same state"
                  + (", the same picture" if check_picture else "")
                  + f" and the same video frame on all {len(common)} sampled game frames"
                  + (f" ({transient} sampled in the middle of a game frame, then the same)" if transient else ""))
    print(f"loads ({label}): " + "; ".join(loads))
    if retail_ram is not None:
        labels = harness.symbols(harness.RETAIL_ELF)[1]
        bad, near = {}, {}
        for key in retail_ram:
            if key not in practice_ram:
                bad[key] = {"(no snapshot)": 1}
                continue
            diff, small = ramcheck.differences(labels, retail_ram[key], practice_ram[key])
            if diff:
                bad[key] = diff
            for n, c in small.items():
                near[n] = max(near.get(n, 0), c)
        if bad:
            key = sorted(bad)[0]
            print(f"FAIL faithful ({label}): the RAM differs on {len(bad)} of {len(retail_ram)} checks; first at "
                  f"level:game frame {key}, words by label: " + ", ".join(f"{n} ({c})" for n, c in bad[key].items()))
            ok = False
        else:
            print(f"PASS faithful ({label}): the same RAM on {len(retail_ram)} checks, outside the masked areas"
                  + ("; words off by 1 at most: " + ", ".join(f"{n} ({c})" for n, c in near.items()) if near else ""))
    return ok


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--with-features", action="store_true")
    ap.add_argument("--reuse-retail", action="store_true", help="use the retail trace of the last run")
    a = ap.parse_args()
    if a.reuse_retail:
        retail, retail_ram = load("retail")
    else:
        retail, retail_ram = record("retail", harness.RETAIL_ROM, harness.RETAIL_ELF)
    practice, practice_ram = record("practice", harness.PRACTICE_ROM, harness.PRACTICE_ELF)
    ok = compare("features off", retail, practice, retail_ram, practice_ram)
    if a.with_features:
        on, _ = record("practice-features", harness.PRACTICE_ROM, harness.PRACTICE_ELF, "on")
        compare("features on", retail, on, check_picture=False)
    sys.exit(0 if ok else 1)


if __name__ == "__main__":
    main()
