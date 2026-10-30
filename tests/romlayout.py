#!/usr/bin/env python3
"""The practice ROM has the retail ROM's size, and the loader and the practice segment sit in its free end.

    romlayout.py

No emulator needed; run `make` first. Checks:

- the ROM is 8 MiB, as large as the retail ROM, and its header checksum is right;
- the retail ROM is zero after its data, and the game names no ROM address at or after the loader;
- the loader and the practice segment in the ROM are the ELF's segments, and every other byte after the retail
  data is zero;
- the link fails with a clear message when the loader and the practice segment would end past the ROM.
"""
import os
import re
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, os.path.join(ROOT, "gloveworld", "tools", "patches"))
from debugpad2 import checksum, word  # noqa: E402

CROSS = os.environ.get("CROSS", "mips-linux-gnu-")
PRACTICE_ROM = os.path.join(ROOT, "build", "glover-practice.z64")
PRACTICE_ELF = os.path.join(ROOT, "build", "glover-practice.elf")
RETAIL_ROM = os.path.join(ROOT, "baserom.us.z64")
RETAIL_ELF = os.path.join(ROOT, "gloveworld", "build", "NGVE.elf")
LD_SCRIPT = os.path.join(ROOT, "build", "practice.ld")
ROM_SIZE = 0x800000
PI_BASE = 0xB0000000

failed = []


def check(ok, passed, failure):
    print(("PASS " + passed) if ok else ("FAIL " + failure))
    if not ok:
        failed.append(failure)


def absolute_symbols(elf):
    out = subprocess.run([CROSS + "nm", elf], check=True, capture_output=True, text=True).stdout
    return {p[2]: int(p[0], 16) for p in (l.split() for l in out.splitlines()) if len(p) == 3 and p[1] == "A"}


def segment(elf, name):
    """The bytes of an ELF section as they are in the ROM."""
    with tempfile.NamedTemporaryFile() as f:
        subprocess.run([CROSS + "objcopy", "-O", "binary", "-j", name, elf, f.name], check=True)
        return f.read()


def link(script, out):
    """Links the practice ELF with SCRIPT, as the Makefile does (the script names the objects). Returns the linker's
    exit code and messages."""
    gw = os.path.join(ROOT, "gloveworld")
    cmd = [CROSS + "ld", "-T", script, "-T", f"{gw}/undefined_syms_auto.txt", "-T", f"{gw}/undefined_funcs_auto.txt",
           "-T", f"{gw}/symbols.ld", "--no-check-sections", "-o", out]
    proc = subprocess.run(cmd, cwd=ROOT, capture_output=True, text=True)
    return proc.returncode, proc.stderr


def objects(script, directory):
    text = open(script).read()
    seen = []
    for obj in re.findall(rf"({directory}/\S+?\.o)\(", text):
        if obj not in seen:
            seen.append(obj)
    return seen


def main():
    rom = open(PRACTICE_ROM, "rb").read()
    retail = open(RETAIL_ROM, "rb").read()
    old = absolute_symbols(RETAIL_ELF)
    new = absolute_symbols(PRACTICE_ELF)
    data_end = old["recordKeyFilesRomEnd"] - PI_BASE
    loader = (new["loader_ROM_START"], new["loader_ROM_END"])
    practice = (new["practice_ROM_START"], new["practice_ROM_END"])

    check(len(rom) == ROM_SIZE == len(retail), f"the ROM is {len(rom):#x} bytes (8 MiB), as large as the retail ROM",
          f"the ROM is {len(rom):#x} bytes; the retail ROM is {len(retail):#x}")
    check((word(rom, 0x10), word(rom, 0x14)) == checksum(rom, "6102"), "the header checksum is right",
          "the header checksum does not match the ROM")
    check(not any(retail[data_end:]), f"the retail ROM is zero after its data (ROM {data_end:#x})",
          f"the retail ROM holds data after ROM {data_end:#x}")

    # The game reaches the ROM through the addresses that its symbols name (0xB0000000 and up): the start and the
    # end of each asset, and offsets inside them. The last symbol is the end of the data.
    rom_syms = {n: a for n, a in old.items() if PI_BASE <= a < PI_BASE + 0x10000000}
    last = max(rom_syms, key=rom_syms.get)
    check(rom_syms[last] - PI_BASE <= loader[0], f"no game symbol names a ROM address past the data (the last is "
          f"{last}, {rom_syms[last] - PI_BASE:#x})", f"{last} names ROM {rom_syms[last] - PI_BASE:#x}, in the "
          f"loader's place")

    check(data_end <= loader[0] <= loader[1] <= practice[0] <= practice[1] <= len(rom),
          f"the loader (ROM {loader[0]:#x} to {loader[1]:#x}) and the practice segment ({practice[0]:#x} to "
          f"{practice[1]:#x}) follow the data and end inside the ROM",
          "the loader or the practice segment is outside the free end of the ROM")
    check(rom[loader[0]:loader[1]] == segment(PRACTICE_ELF, ".loader")
          and rom[practice[0]:practice[1]] == segment(PRACTICE_ELF, ".practice"),
          "the ROM holds the ELF's loader and practice segment where the symbols say",
          "the loader or the practice segment in the ROM differs from the ELF's")
    outside = rom[data_end:loader[0]] + rom[loader[1]:practice[0]] + rom[practice[1]:]
    check(not any(outside), "every other byte after the retail data is zero",
          "there are non-zero bytes after the retail data outside the loader and the practice segment")

    # A ROM too small for the practice segment: the link must fail and say why. A ROM that just fits: it links.
    args = ["--loader"] + objects(LD_SCRIPT, "build/loader") + ["--practice"] + objects(LD_SCRIPT, "build/practice")
    with tempfile.TemporaryDirectory() as tmp:
        script = os.path.join(tmp, "practice.ld")
        for size, fits in ((practice[1] - 1, False), (practice[1], True)):
            subprocess.run([sys.executable, os.path.join(ROOT, "tools", "mkld.py"), os.path.join(ROOT, "gloveworld"),
                            script, "--rom-size", hex(size)] + args, cwd=ROOT, check=True)
            code, messages = link(script, os.path.join(tmp, "out.elf"))
            if fits:
                check(code == 0, f"a ROM of {size:#x} bytes (just enough) links",
                      f"a ROM of {size:#x} bytes (just enough) does not link: {messages[-300:]}")
            else:
                check(code != 0 and "do not fit in the free end" in messages,
                      f"a ROM of {size:#x} bytes (one byte short) does not link, and the linker says why",
                      f"a ROM of {size:#x} bytes (one byte short) did not stop the link: {messages[-300:]}")

    sys.exit(1 if failed else 0)


if __name__ == "__main__":
    main()
