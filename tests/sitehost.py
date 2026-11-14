#!/usr/bin/env python3
"""Checks tools/mksite.py without an emulator: the Glovebox box goes beside the patcher, with the newest release's
download, or with a link to the releases page when there is no release.

    python3 tests/sitehost.py
"""
import json
import os
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PAGE = """<!doctype html><html><head><style>main{}</style>
</head><body><main><h1>Glover Practice Patcher</h1></main>
<script>const VERSION = "1.0.0";</script></body></html>
"""
RELEASE = {
    "tag_name": "v2.3.4",
    "html_url": "https://github.com/Eddie-Lack/Glovebox/releases/tag/v2.3.4",
    "assets": [
        {"name": "notes.txt", "size": 10, "browser_download_url": "https://example.invalid/notes.txt"},
        {"name": "Glovebox-2.3.4-windows-x64.zip", "size": 3 * 1024 * 1024,
         "browser_download_url": "https://github.com/Eddie-Lack/Glovebox/releases/download/v2.3.4/Glovebox-2.3.4-windows-x64.zip"},
    ],
}


def build(tmp, release_text):
    page = os.path.join(tmp, "page.html")
    rel = os.path.join(tmp, "rel.json")
    out = os.path.join(tmp, "out.html")
    open(page, "w").write(PAGE)
    open(rel, "w").write(release_text)
    subprocess.run([sys.executable, os.path.join(ROOT, "tools", "mksite.py"), page, rel, out],
                   check=True, capture_output=True)
    return open(out).read()


def main():
    problems = []
    with tempfile.TemporaryDirectory() as tmp:
        page = build(tmp, json.dumps(RELEASE))
        main_end = page.index("</main>")
        box = page.find('<aside class="side"')
        if box < main_end:
            problems.append("the box is not after the patcher")
        for want in ("Version 2.3.4", "releases/download/v2.3.4/Glovebox-2.3.4-windows-x64.zip",
                     "Glovebox-2.3.4-windows-x64.zip, 3.0 MB", "flex-wrap: wrap"):
            if want not in page:
                problems.append(f"with a release, the page lacks {want!r}")
        if "{{" in page:
            problems.append("a {{...}} was left unfilled")
        if 'const VERSION = "1.0.0"' not in page:
            problems.append("the patch page's own script changed")
        for text in ("{}", "", '{"message": "Not Found"}'):
            page = build(tmp, text)
            if 'href="https://github.com/Eddie-Lack/Glovebox/releases"' not in page or "Version" in page.split("</main>")[1]:
                problems.append(f"with no release ({text!r}), the box does not link to the releases page alone")
            if "{{" in page:
                problems.append(f"with no release ({text!r}), a {{{{...}}}} was left unfilled")
    if problems:
        print("FAIL sitehost: " + "; ".join(problems))
        sys.exit(1)
    print("PASS sitehost: the Glovebox box is filled from a release, and links to the releases page without one")


if __name__ == "__main__":
    main()
