"""Measures what a savestate would have to hold (docs/plan.md, "Memory and ROM map"): plays the route of
trace.py and reports the heap in use. Run it on the retail ROM:

    harness.py memory --rom baserom.us.z64 --elf gloveworld/build/NGVE.elf --gfx angrylion
"""
import struct

import trace

JALLOC_NUM_BLOCKS = 0x500
BLOCK_SIZE = 0x14


def run(emu, sym, args):
    jc = sym("jallocControl")
    heap_end = 0x80400000
    peak = {"static": 0, "dynamic": 0, "span": 0}
    f = 0
    for n in trace.run(emu, sym, args):
        f += 1
        if f % 50 == 0:
            static_used, dynamic_used = struct.unpack(">ii", emu.read(jc + 0x6410, 8))
            blocks = emu.read(jc, JALLOC_NUM_BLOCKS * BLOCK_SIZE)
            low = heap_end
            for i in range(JALLOC_NUM_BLOCKS):
                addr, size = struct.unpack(">Ii", blocks[i * BLOCK_SIZE:i * BLOCK_SIZE + 8])
                if blocks[i * BLOCK_SIZE + 0x13] and size > 0:
                    low = min(low, addr)
            peak["static"] = max(peak["static"], static_used)
            peak["dynamic"] = max(peak["dynamic"], dynamic_used)
            peak["span"] = max(peak["span"], heap_end - low)
        yield n
    heap = heap_end - sym("jallocHeapStart")
    print(f"heap: {heap:#x} bytes; most in use: static {peak['static']:#x}, dynamic {peak['dynamic']:#x}; "
          f"lowest block {peak['span']:#x} below the heap's end")
