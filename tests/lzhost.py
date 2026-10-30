#!/usr/bin/env python3
"""Builds tests/lzhost.c with the host compiler and runs it: the savestate compressor must give back what it was
given, for made-up data of several kinds and sizes."""
import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
OUT = os.path.join(ROOT, "build", "tests", "lzhost")


def main():
    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    subprocess.run(["cc", "-O2", "-Wall", "-fsanitize=address,undefined", "-I" + os.path.join(ROOT, "practice", "include"),
                    "-o", OUT, os.path.join(HERE, "lzhost.c"), os.path.join(ROOT, "practice", "src", "lz.c")], check=True)
    proc = subprocess.run([OUT], capture_output=True, text=True)
    print(("PASS " if proc.returncode == 0 else "FAIL ") + "lzhost: " + proc.stdout.strip() + proc.stderr.strip()[-500:])
    sys.exit(proc.returncode)


if __name__ == "__main__":
    main()
