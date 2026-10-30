#!/usr/bin/env python3
"""Write the practice build's linker script from gloveworld's.

    mkld.py GLOVEWORLD OUT.ld --rom-size SIZE --loader LOADER.o... --practice PRACTICE.o...

gloveworld's script (NGVE.ld) links the game at its retail addresses. The practice build keeps that layout:
the hooks do not change the size of any code or data (hooks/include/practice_hooks.h), so every game symbol
stays where it is in the retail ROM. tools/checklayout.py checks this after the link. This script makes these
changes:

1. The game's C objects come from build/gw/ (the hooked copy of the source). The assembly and binary objects
   come from gloveworld's own build, except the assets blob (3).
2. The ROM keeps its size, SIZE (8 MiB). The retail ROM's data ends at recordKeyFilesRomEnd (0x77DE20); the rest
   of the 8 MiB is zero, and nothing in the game reads it (docs/build.md, "Layout").
3. The assets blob comes from build/assets.o, which the Makefile makes without the zero padding at its end
   (tools/trimassets.py).
4. The loader goes at ROM LOADER_ROM_START, in the free end. It runs at LOADER_VRAM, in the game's heap, which
   the game does not use until later in its boot.
5. The practice segment follows the loader in ROM. It runs at 0x80400000, in the Expansion Pak's memory. The
   link fails if the loader and the practice segment do not end inside the ROM.
6. A few symbols for the boot hook and the practice code.
"""
import argparse
import os
import re
import sys

MAIN_ROM_END = 0xF6680
LOADER_ROM_START = 0x780000  # the first aligned address after the retail data (0x77DE20)
PI_BASE = 0xB0000000
LOADER_VRAM = 0x80380000
LOADER_LIMIT = 0x80390000  # 64 KB for the loader's code, data and bss
PRACTICE_VRAM = 0x80400000
PRACTICE_LIMIT = 0x80480000  # 512 KB for the practice code, data and bss (docs/plan.md)
OS_MEM_SIZE = 0x80000318  # osMemSize, set by the console's boot code


def segment(name, vram, objs, limit):
    load = "\n".join(f"        {o}(.text .text.* .rodata .rodata.* .data .data.*)" for o in objs)
    bss = "\n".join(f"        {o}(.bss .bss.* COMMON)" for o in objs)
    return f"""    {name}_ROM_START = __romPos;
    .{name} 0x{vram:08X} : AT({name}_ROM_START)
    {{
        {name}_VRAM = .;
{load}
        . = ALIGN(., 16);
    }}
    __romPos += SIZEOF(.{name});
    {name}_ROM_END = __romPos;
    {name}_ROM_SIZE = {name}_ROM_END - {name}_ROM_START;
    {name}_PI_START = 0x{PI_BASE:08X} + {name}_ROM_START;
    .{name}_bss (NOLOAD) :
    {{
        {name}_BSS_START = .;
{bss}
        . = ALIGN(., 16);
        {name}_BSS_END = .;
    }}
    ASSERT({name}_BSS_END <= 0x{limit:08X}, "the {name} segment is too large")

"""


def number(text):
    return int(text, 0)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("gloveworld")
    ap.add_argument("out")
    ap.add_argument("--rom-size", type=number, required=True)
    ap.add_argument("--loader", nargs="*", default=[])
    ap.add_argument("--practice", nargs="*", default=[])
    a = ap.parse_args()
    gw = a.gloveworld

    ld = open(os.path.join(gw, "NGVE.ld")).read()
    ld = re.sub(r"\bbuild/src/", "build/gw/src/", ld)
    ld = re.sub(r"\bbuild/bin/assets\.o\b", "build/assets.o", ld)
    ld = re.sub(r"\bbuild/(asm|bin)/", lambda m: f"{gw}/build/{m.group(1)}/", ld)

    marker = "    /DISCARD/ :"
    if ld.count(marker) != 1:
        sys.exit("mkld: NGVE.ld does not have exactly one /DISCARD/ section")
    if not LOADER_ROM_START < a.rom_size:
        sys.exit("mkld: the loader starts outside the ROM")
    added = (f'    ASSERT(main_ROM_END == 0x{MAIN_ROM_END:X}, "the main segment changed size")\n'
             f'    ASSERT(__romPos == recordKeyFilesRomEnd - 0x{PI_BASE:X}, "the retail data changed size")\n'
             f'    ASSERT(0x{LOADER_ROM_START:X} >= __romPos, "the loader starts inside the retail data")\n'
             f'    __romPos = 0x{LOADER_ROM_START:X};\n\n'
             + segment("loader", LOADER_VRAM, a.loader, LOADER_LIMIT)
             + "    __romPos = ALIGN(__romPos, 16);\n"
             + segment("practice", PRACTICE_VRAM, a.practice, PRACTICE_LIMIT)
             + f'    ASSERT(practice_ROM_END <= 0x{a.rom_size:X}, "the loader and the practice segment do not fit '
               f'in the free end of the {a.rom_size // 0x100000} MiB ROM (ROM 0x{LOADER_ROM_START:X} to '
               f'0x{a.rom_size - 1:X}): the ROM must keep its size")\n\n')
    ld = ld.replace(marker, added + marker)
    ld += f"\nosMemSize = 0x{OS_MEM_SIZE:08X};\n"

    # Written only when it changes, so make relinks only when needed.
    if not os.path.exists(a.out) or open(a.out).read() != ld:
        open(a.out, "w").write(ld)


if __name__ == "__main__":
    main()
