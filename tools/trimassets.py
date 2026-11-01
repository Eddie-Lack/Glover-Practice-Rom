#!/usr/bin/env python3
"""Cut the zero padding off gloveworld's assets blob.

    trimassets.py ASSETS.bin OUT.bin RETAIL.elf [--nm NM]

gloveworld's assets blob (bin/assets.bin) runs from the end of the main segment to the end of the retail ROM's
8 MiB. The retail data in it ends at recordKeyFilesRomEnd; the rest is zero padding. The practice ROM keeps its
size by putting the loader and the practice segment into that padding (tools/mkld.py), so the blob that it links
stops at the end of the data. A newer gloveworld ends the blob there itself; the script then copies it as it is.

The script fails if any byte it cuts is not zero. OUT is written only when its content changes. Make then does
not relink for nothing.
"""
import argparse
import os
import subprocess
import sys

PI_BASE = 0xB0000000


def absolute_symbols(elf, nm):
    out = subprocess.run([nm, elf], check=True, capture_output=True, text=True).stdout
    return {p[2]: int(p[0], 16) for p in (l.split() for l in out.splitlines()) if len(p) == 3 and p[1] == "A"}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("assets")
    ap.add_argument("out")
    ap.add_argument("retail_elf")
    ap.add_argument("--nm", default="mips-linux-gnu-nm")
    a = ap.parse_args()

    syms = absolute_symbols(a.retail_elf, a.nm)
    start = syms["assets_ROM_START"]
    keep = syms["recordKeyFilesRomEnd"] - PI_BASE - start
    blob = open(a.assets, "rb").read()
    if not 0 < keep <= len(blob):
        sys.exit(f"trimassets: the data ends {keep:#x} bytes into a blob of {len(blob):#x} bytes")
    if any(blob[keep:]):
        sys.exit(f"trimassets: {a.assets} has data after recordKeyFilesRomEnd (ROM {start + keep:#x})")
    trimmed = blob[:keep]
    if not os.path.exists(a.out) or open(a.out, "rb").read() != trimmed:
        open(a.out, "wb").write(trimmed)


if __name__ == "__main__":
    main()
