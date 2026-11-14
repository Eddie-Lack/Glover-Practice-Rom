#!/usr/bin/env python3
"""Write glover.love's index.html: a release's patch page with the Glovebox box beside the patcher.

    mksite.py PATCHPAGE.html GLOVEBOX.json OUT.html

PATCHPAGE is the patch page attached to a release of this ROM. GLOVEBOX.json is GitHub's answer for Glovebox's
newest release (`gh api repos/Eddie-Lack/glovebox/releases/latest`). It may be `{}`: the box then links to the
releases page. The box is site/glovebox.html. The patch page itself is not changed, so the pages of older releases
work too.
"""
import html
import json
import os
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
RELEASES = "https://github.com/Eddie-Lack/Glovebox/releases"

# Lays the two boxes out side by side on a wide screen, and one above the other on a narrow one. The rest copies
# the patcher's look for the new box.
STYLE = """<style>
body { display: flex; flex-wrap: wrap; justify-content: center; align-items: flex-start; gap: 20px; }
main { flex: 1 1 520px; margin: 0; }
.side { flex: 1 1 300px; max-width: 360px; background: var(--navy); border: 6px ridge var(--grey); padding: 0 0 14px; }
.side-logo { font-size: clamp(26px, 6vw, 36px); }
.side-what { margin-top: 6px; color: #c0c0ff; font-size: 12px; }
.side code { font: 12px "Courier New", Courier, monospace; color: var(--yellow); }
a.button { display: inline-block; text-decoration: none; font: bold 13px Arial, Helvetica, sans-serif; color: #000000;
  background: var(--grey); border: 2px solid; border-color: var(--grey-light) #404040 #404040 var(--grey-light);
  box-shadow: inset -1px -1px 0 var(--grey-dark), inset 1px 1px 0 #dfdfdf; padding: 6px 22px; }
a.button:active { border-color: #404040 var(--grey-light) var(--grey-light) #404040; box-shadow: inset 1px 1px 0 var(--grey-dark); }
a.button:focus-visible { outline: 1px dotted #000000; outline-offset: -5px; }
</style>
"""


def box(release):
    """The box's HTML for a release (GitHub's JSON), or for none."""
    text = open(os.path.join(ROOT, "site", "glovebox.html")).read()
    text = text[text.index("-->") + 3:].lstrip("\n")
    tag = release.get("tag_name") or ""
    zips = [a for a in release.get("assets", []) if a.get("name", "").endswith("-windows-x64.zip")]
    if zips:
        a = zips[0]
        url = a["browser_download_url"]
        size = a.get("size", 0) / (1024 * 1024)
        file_line = f"{html.escape(a['name'])}, {size:.1f} MB"
    else:
        url = release.get("html_url") or RELEASES
        file_line = f'On the <a href="{RELEASES}">releases page</a>'
    version = tag[1:] if tag.startswith("v") else tag
    version_line = f'<p class="version">Version {html.escape(version)}</p>' if version else ""
    return (text.replace("{{URL}}", html.escape(url, quote=True))
                .replace("{{FILE_LINE}}", file_line)
                .replace("{{VERSION_LINE}}", version_line))


def main():
    page_path, release_path, out = sys.argv[1:4]
    page = open(page_path).read()
    try:
        release = json.load(open(release_path))
    except (OSError, ValueError):
        release = {}
    if not isinstance(release, dict):
        release = {}
    for mark in ("</head>", "</main>"):
        if page.count(mark) != 1:
            sys.exit(f"mksite: the patch page needs exactly one {mark}")
    page = page.replace("</head>", STYLE + "</head>")
    page = page.replace("</main>", "</main>\n\n" + box(release))
    open(out, "w").write(page)
    print(f"{out}: {len(page)} bytes, Glovebox {release.get('tag_name') or 'no release'}")


if __name__ == "__main__":
    main()
