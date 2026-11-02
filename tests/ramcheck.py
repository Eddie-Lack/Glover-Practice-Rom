"""Hashes of the game's RAM for faithful.py.

The practice code costs a few CPU cycles on each frame, so some values that record time differ from the retail
ROM even when the game does the same thing. These areas are left out (masked) before hashing; everything else
in the lower 4 MB must be the same. The list comes from a comparison of the two ROMs' RAM (docs/faithfulness.md).

Take samples only while a level is running. During a load the decompressor's progress at a given video frame
differs by a few bytes, for the same reason.
"""
import bisect
import zlib

RAM_START = 0x80000000
RAM_END = 0x80400000
BLOCK = 0x1000

# (first label, label after the last byte). None: up to the next label.
MASKED = [
    ("main_TEXT_START", "main_DATA_START"),  # code: the hooks change two calls and replace ClearStaticMemory
    ("audio_memory", "pbankBuffer"),  # the music driver's heap
    ("sched", "frameCount"),  # the scheduler and its message queues
    ("printTimer", "tipShadTex"),  # the debug timing bars (StartTimer, EndTimer) store CPU counts
    ("frameStart", None),  # osGetCount at the start of the frame
    ("__am", "gReverbFx"),  # the audio manager
    ("D_802AB010", None),  # music driver (libmus) state, set up by MusInitialize
    ("D_801F4170", None),  # music driver counters, used by the audio code at 0x801C3040
    ("D_801F4174", None),
    ("__osCurrentTime", "__osSiAccessQueue"),  # the OS's time and timers
    ("__osBaseCounter", None),
    ("__osEepPifRam", None),  # the EEPROM command buffer
    ("piMgrThread", None),  # the PI manager's thread (osCreatePiManager)
    ("piMgrStack", None),  # its stack, which holds pointers into audio_memory during play
    ("piMessageQ", None),
    ("piMessageBuf", None),
    ("__osThreadSave", None),
]
# The picture: the Z buffer and the frame buffers (320 by 240, 16 bits a pixel), and the RSP's output for the RDP
# (rdp_output, up to audio_memory). The faithfulness test compares the picture on its own (faithful.py).
PICTURE = [("zbuffer", 0x25800), ("rdp_output", "audio_memory"), ("cfb_16_a", 0x25800), ("cfb_16_b", 0x25800)]


def masked_ranges(sym):
    labels = sym.labels
    addrs = [a for a, _ in labels]

    def after(addr):
        i = bisect.bisect_right(addrs, addr)
        return addrs[i] if i < len(addrs) else RAM_END

    ranges = []
    for first, last in MASKED:
        start = sym(first)
        ranges.append((start, sym(last) if last else after(start)))
    for name, size in PICTURE:
        ranges.append((sym(name), sym(size) if isinstance(size, str) else sym(name) + size))
    # Every thread's context and stack: they hold saved registers, which include return addresses and counts.
    for addr, name in labels:
        if name.endswith(("Thread", "ThreadStack", "Stack")) and RAM_START <= addr < RAM_END:
            ranges.append((addr, after(addr)))
    return sorted(ranges)


def snapshot(emu, ranges):
    """The lower 4 MB of RAM with the masked areas zeroed, compressed."""
    ram = bytearray(emu.read(RAM_START, RAM_END - RAM_START))
    for start, end in ranges:
        ram[start - RAM_START:end - RAM_START] = bytes(end - start)
    return zlib.compress(bytes(ram), 1)


def near(x, y):
    """True if each 16-bit half of the two words differs by at most 1."""
    for shift in (0, 16):
        d = ((x >> shift) - (y >> shift)) & 0xFFFF
        if d not in (0, 1, 0xFFFF):
            return False
    return True


def differences(labels, a, b):
    """({label: number of words}, {label: number of words}) for the words that differ between two snapshots: those
    that differ by more than 1 in a 16-bit half, and those that differ by 1 at most. The draw thread's vertex data
    can differ by 1 when the practice code's cycles move it against the game thread (docs/testing.md)."""
    a, b = zlib.decompress(a), zlib.decompress(b)
    addrs = [x for x, _ in labels]
    out, small = {}, {}
    for i in range(0, len(a), 4):
        if a[i:i + 4] != b[i:i + 4]:
            j = bisect.bisect_right(addrs, RAM_START + i) - 1
            name = labels[j][1] if j >= 0 else f"{RAM_START + i:#010x}"
            which = small if near(int.from_bytes(a[i:i + 4], "big"), int.from_bytes(b[i:i + 4], "big")) else out
            which[name] = which.get(name, 0) + 1
    return out, small
