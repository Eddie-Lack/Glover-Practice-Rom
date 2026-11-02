#!/usr/bin/env python3
"""Write the memory ranges that a savestate holds, from gloveworld's ELF.

    mkstate.py GLOVEWORLD_ELF OUT.h [--nm NM]

A savestate holds the game's own state: its data and bss, the heap, and the texture bank buffer in low memory.
It leaves out what belongs to the OS, the threads, the audio driver (and the game's handle to its song) and the
graphics output: those keep running across a load, and must not go back in time (docs/build.md, "Savestates"). Every address is the retail one.
"""
import argparse
import re
import subprocess

# The regions that hold game state.
REGIONS = [
    ("main_DATA_START", "jumptable"),       # the game's data; the music driver's and libultra's data follow
    ("main_BSS_START", "D_802AB010"),        # the game's bss; the music driver's and libultra's bss follow
    ("jallocHeapStart", 0x80400000),          # the heap
    ("sysfont", 0x80100000),        # the texture bank buffers below the code
]

# OS objects inside the game's regions, left out from their label to the next named label (not a D_ label).
OS_OBJECTS = re.compile(r"^(eepromMsgQ|eepromMsgBuf|serialMsgQ|statusdata|controllerMsgBuf|controllerMsgQ|"
                        r"controllerdata|piMessageBuf|sched|dram_stack|gfx_client|gfx_msgbuf|tlist|sched_gfxMQ|"
                        r"main_client|main_msgbuf|retraceMessageBuf|dmaMessageBuf|rdpMessageBuf|gfx_msgQ|"
                        r"rspMessageBuf|dynamicp|tlistPointers|tlistp|main_msgQ|dmaIOMessageBuf|dummyMesg|"
                        r".*MessageQ|.*MsgQ|rdp_output_len|rdp_output_size|schedRetraceCounts|"
                        r"timerRetraceCounts|gfxTasks|draw_buffer|frameStart|controllerBusy)$")

# Game objects that describe the live music driver, which a state does not hold: the playing song's handle.
LIVE_AUDIO = {"audioCtrl": 0x20}

# OS objects with a known size: thread contexts (OSThread, 0x1B0 bytes), stacks, buffers.
SIZED = {
    "idleThread": None,  # the idle thread's context and its stack, which ends at piMessageBuf
    "mainThreadStack": 0x4000, "mainThread": 0x1B0,
    "ControllerThreadStack": 0x400, "ControllerThread": 0x1B0,
    "drawGraphicsThreadStack": 0x4000, "drawGraphicsThread": 0x1B0,
    "TimerThreadStack": 0x400, "TimerThread": 0x1B0,
    "nnScGraphicsStack": 0x2000, "nnScAudioStack": 0x2000, "nnScStack": 0x2000,
    "bootStack": 0x400,
    "gfxYieldBuf": 0xC00,
    "dynamic": 2 * 0x2B230,  # the display lists and matrices of both frame buffers
}


CHUNK_BYTES = 0x4000


def symbols(elf, nm):
    out = subprocess.run([nm, "-n", elf], check=True, capture_output=True, text=True).stdout
    table, named = {}, []
    for line in out.splitlines():
        parts = line.split()
        if len(parts) != 3:
            continue
        addr, kind, name = int(parts[0], 16), parts[1], parts[2]
        table[name] = addr
        if kind in "TtDdBbRr" and not name.startswith(("D_", "jtbl_", "pad_", ".L")):
            named.append((addr, name))
    named.sort()
    return table, named


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("elf")
    ap.add_argument("out")
    ap.add_argument("--nm", default="mips-linux-gnu-nm")
    a = ap.parse_args()
    table, named = symbols(a.elf, a.nm)

    def addr(x):
        return x if isinstance(x, int) else table[x]

    def next_named(after):
        return next(n for n, _ in named if n > after)

    holes = []
    for start, name in named:
        if name in LIVE_AUDIO:
            holes.append((start, start + LIVE_AUDIO[name]))
        elif name in SIZED:
            size = SIZED[name]
            end = table["piMessageBuf"] + 0x30 if size is None else start + size
            holes.append((start, end))
        elif OS_OBJECTS.match(name):
            holes.append((start, next_named(start)))
    ranges = []
    for first, last in REGIONS:
        start, end = addr(first), addr(last)
        cuts = sorted(h for h in holes if h[1] > start and h[0] < end)
        for h0, h1 in cuts:
            if h0 > start:
                ranges.append((start, min(h0, end)))
            start = max(start, h1)
        if start < end:
            ranges.append((start, end))
    total = sum(e - s for s, e in ranges)
    # savestate.c cuts each range into chunks of this size (LZ_BLOCK_MAX) and asserts the count it can hold.
    chunks = sum(-(-(e - s) // CHUNK_BYTES) for s, e in ranges)
    lines = ["/* Written by tools/mkstate.py from gloveworld's ELF: the memory a savestate holds. */",
             f"#define STATE_SIZE 0x{total:X}",
             f"#define STATE_CHUNK_BYTES 0x{CHUNK_BYTES:X}",
             f"#define STATE_CHUNKS {chunks}",
             "static const u32 stateRanges[][2] = {"]
    lines += [f"    {{ 0x{s:08X}, 0x{e:08X} }}," for s, e in ranges]
    lines += ["};", ""]
    text = "\n".join(lines)
    try:
        old = open(a.out).read()
    except OSError:
        old = None
    if old != text:
        open(a.out, "w").write(text)
        print(f"{a.out}: {len(ranges)} ranges, 0x{total:X} bytes")


if __name__ == "__main__":
    main()
