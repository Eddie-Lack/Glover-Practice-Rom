#!/usr/bin/env python3
"""Check that the practice build keeps the retail layout.

    checklayout.py RETAIL.elf PRACTICE.elf RETAIL.z64 PRACTICE.z64 [--nm NM]

Every global label of gloveworld's matching build must be at the same address in the practice build, except the
functions that the hooks replace (REPLACED) and the labels at the end of the assets blob (TRIMMED).

The practice ROM has the retail ROM's size. The retail ROM's data ends at recordKeyFilesRomEnd and the rest of its
bytes are zero. The loader and the practice segment sit in that free end. The script checks that:

- the practice ROM is as large as the retail ROM, and the retail ROM is zero after its data;
- the loader and the practice segment lie after the data and inside the ROM, with only zero bytes around them;
- the part of the ROM before the loader differs from the retail ROM only where the hooks are. The script lists the
  differing words by the function or data label they belong to.
"""
import argparse
import bisect
import subprocess
import sys

# Functions that nothing calls, whose place the hooks take (see hooks/).
REPLACED = {"ClearStaticMemory": "PracticeMeasureFrameTicks"}
# The assets blob of the practice build stops at the end of the data, without the zero padding that follows it
# in the retail ROM. Its end labels move up by the size of that padding. Nothing reads them.
TRIMMED = {"_binary_bin_assets_bin_end", "assets_DATA_END", "assets_VRAM_END"}
VRAM_TO_ROM = 0x800FF000  # vram minus ROM offset in the main segment
MAIN_ROM = (0x1040, 0xF6680)
HEADER_NAME = (0x20, 0x34)
HEADER_CRC = (0x10, 0x18)
PI_BASE = 0xB0000000
PRACTICE_RAM = (0x80400000, 0x80480000)  # the practice segment's place in RAM (tools/mkld.py)


def symbols(elf, nm):
    """The labels (text, data, bss, read-only data) and the absolute symbols (ROM addresses) of an ELF."""
    out = subprocess.run([nm, elf], check=True, capture_output=True, text=True).stdout
    labels, absolute = {}, {}
    for line in out.splitlines():
        parts = line.split()
        if len(parts) == 3 and parts[1] in "TDBR":
            labels[parts[2]] = int(parts[0], 16)
        elif len(parts) == 3 and parts[1] == "A":
            absolute[parts[2]] = int(parts[0], 16)
    return labels, absolute


def fail(message):
    sys.exit("checklayout: " + message)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("retail_elf")
    ap.add_argument("practice_elf")
    ap.add_argument("retail_rom")
    ap.add_argument("practice_rom")
    ap.add_argument("--nm", default="mips-linux-gnu-nm")
    a = ap.parse_args()
    old, old_abs = symbols(a.retail_elf, a.nm)
    new, new_abs = symbols(a.practice_elf, a.nm)
    retail = open(a.retail_rom, "rb").read()
    practice = open(a.practice_rom, "rb").read()

    # The size of the free end, and the labels that move because of it.
    data_end = old_abs["recordKeyFilesRomEnd"] - PI_BASE
    cut = len(retail) - data_end
    if len(practice) != len(retail):
        fail(f"the practice ROM is {len(practice):#x} bytes; the retail ROM is {len(retail):#x}")
    if any(retail[data_end:]):
        fail(f"the retail ROM has data after recordKeyFilesRomEnd ({data_end:#x})")

    bad = []
    for name, addr in sorted(old.items(), key=lambda x: x[1]):
        name_new = REPLACED.get(name, name)
        want = addr - cut if name in TRIMMED else addr
        if new.get(name_new) != want:
            bad.append(f"{name} is at {new.get(name_new, 0):#010x}, not {want:#010x}")
    if bad:
        print("\n".join("checklayout: " + b for b in bad[:20]), file=sys.stderr)
        sys.exit(f"checklayout: {len(bad)} labels moved")

    # The loader and the practice segment.
    first, last = new_abs["loader_ROM_START"], new_abs["practice_ROM_END"]
    if first < data_end:
        fail(f"the loader starts at ROM {first:#x}, inside the retail data (it ends at {data_end:#x})")
    if last > len(practice):
        fail(f"the loader and the practice segment end at ROM {last:#x}, past the end of the ROM "
             f"({len(practice):#x})")
    if any(practice[data_end:first]) or any(practice[last:]):
        fail("the practice ROM has data outside the loader and the practice segment, after the retail data")
    if not any(practice[first:last]):
        fail("the loader and the practice segment are empty")

    order = sorted((addr, name) for name, addr in new.items() if 0x80100000 <= addr < 0x80300000)
    addrs = [x[0] for x in order]
    where = {}
    for i in range(0, data_end, 4):
        if retail[i:i + 4] != practice[i:i + 4]:
            if HEADER_NAME[0] <= i < HEADER_NAME[1]:
                place = "the header's name"
            elif HEADER_CRC[0] <= i < HEADER_CRC[1]:
                place = "the header's checksum"
            elif MAIN_ROM[0] <= i < MAIN_ROM[1]:
                j = bisect.bisect_right(addrs, i + VRAM_TO_ROM) - 1
                place = order[j][1]
            else:
                place = f"ROM {i:#x}"
            where[place] = where.get(place, 0) + 1
    print("checklayout: every game label is at its retail address. Words that differ from the retail ROM: "
          + ", ".join(f"{place} ({n})" for place, n in where.items()))
    room = len(practice) - first
    print(f"checklayout: the loader and the practice segment are at ROM {first:#x} to {last:#x}. They use "
          f"{last - first:#x} of the {room:#x} bytes to the end of the ROM ({(last - first) * 100 // room}%); "
          f"{len(practice) - last:#x} bytes are left")
    code = new_abs["practice_ROM_SIZE"]
    bss = new["practice_BSS_END"] - new["practice_BSS_START"]
    ram = PRACTICE_RAM[1] - PRACTICE_RAM[0]
    print(f"checklayout: the practice RAM area ({PRACTICE_RAM[0]:#x} to {PRACTICE_RAM[1]:#x}): code and data {code:#x}, bss "
          f"{bss:#x}, together {code + bss:#x} of {ram:#x} bytes ({(code + bss) * 100 // ram}%); {ram - code - bss:#x} bytes "
          f"({(ram - code - bss) / 1024:.1f} KB) are left")


if __name__ == "__main__":
    main()
