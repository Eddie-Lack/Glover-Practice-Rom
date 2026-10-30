#!/usr/bin/env python3
"""Write the patch page with the patch built in.

    mkpage.py TEMPLATE.html PATCH.bps OUT.html

TEMPLATE is tools/patch.html. Opened on its own, it asks for the .bps file. OUT holds the patch as base64 and
the version, so a player needs only that one file and the ROM.
"""
import base64
import re
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import version as practice_version


def main():
    template, patch, out = sys.argv[1:4]
    page = open(template).read()
    data = base64.b64encode(open(patch, "rb").read()).decode()
    version = practice_version.text()
    block = ("// PATCH BEGIN (written by tools/mkpage.py; do not edit by hand)\n"
             f'const PATCH = "{data}";\n'
             f'const VERSION = "{version}";\n'
             "// PATCH END")
    page, n = re.subn(r"// PATCH BEGIN.*?// PATCH END", lambda m: block, page, flags=re.S)
    if n != 1:
        sys.exit("mkpage: the template has no PATCH BEGIN ... PATCH END block")
    open(out, "w").write(page)
    print(f"{out}: {len(page)} bytes")


if __name__ == "__main__":
    main()
