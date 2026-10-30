#!/usr/bin/env python3
"""Finish the practice ROM: write its name into the header and recompute the header checksum.

    mkrom.py ROM --size SIZE

The ROM is changed in place. The boot chip (CIC-6102) stays the same, so the checksum uses its seed. The script
fails if the ROM is not SIZE bytes: the practice ROM has the retail ROM's size.
"""
import argparse
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "gloveworld", "tools", "patches"))
from debugpad2 import checksum, put_word  # noqa: E402

NAME = b"GLOVER PRACTICE"
CIC = "6102"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("rom")
    ap.add_argument("--size", type=lambda t: int(t, 0), required=True)
    a = ap.parse_args()
    path = a.rom
    rom = bytearray(open(path, "rb").read())
    if len(rom) != a.size:
        sys.exit(f"mkrom: the ROM is {len(rom):#x} bytes, not {a.size:#x}. The loader and the practice segment "
                 f"must fit in the free end of the retail ROM (docs/build.md, \"Layout\").")
    rom[0x20:0x34] = NAME.ljust(20, b" ")
    crc1, crc2 = checksum(rom, CIC)
    put_word(rom, 0x10, crc1)
    put_word(rom, 0x14, crc2)
    open(path, "wb").write(rom)


if __name__ == "__main__":
    main()
