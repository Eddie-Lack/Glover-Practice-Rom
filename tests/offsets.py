"""Field offsets of the practice code's structures, worked out by compiling a probe with the cross compiler.

    offsets("actions.h", "Run", ["collView", "hide"]) -> {"collView": 0x2C, "hide": ...}

The probe declares an array of offsetof(STRUCT, FIELD) + 1 bytes for each field; the symbol's size in the object file
gives the offset. The tests that name a few offsets by hand (tests/layout.py) must be kept in step with the headers;
a test that uses this one cannot drift."""
import os
import re
import subprocess
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
CFLAGS = ["-std=gnu11", "-Os", "-G0", "-march=vr4300", "-mfix4300", "-mabi=32", "-mfp32", "-mhard-float",
          "-mno-abicalls", "-fno-pic", "-mno-shared", "-ffreestanding", "-fno-builtin", "-fno-common",
          "-I" + os.path.join(ROOT, "practice", "include"), "-I" + os.path.join(ROOT, "build")]


def offsets(header, struct, fields):
    source = f'#include <stddef.h>\n#include "{header}"\n' + "".join(
        f"char o_{f}[offsetof({struct}, {f}) + 1];\n" for f in fields)
    with tempfile.TemporaryDirectory() as tmp:
        c, o = os.path.join(tmp, "probe.c"), os.path.join(tmp, "probe.o")
        open(c, "w").write(source)
        subprocess.run(["mips-linux-gnu-gcc", "-c", *CFLAGS, c, "-o", o], check=True, capture_output=True)
        out = subprocess.run(["mips-linux-gnu-nm", "-S", o], check=True, capture_output=True, text=True).stdout
    sizes = {}
    for line in out.splitlines():
        m = re.match(r"\S+ ([0-9a-f]+) \w o_(\w+)$", line)
        if m:
            sizes[m.group(2)] = int(m.group(1), 16) - 1
    missing = [f for f in fields if f not in sizes]
    if missing:
        raise RuntimeError(f"no offset for {missing} in {struct}")
    return {f: sizes[f] for f in fields}
