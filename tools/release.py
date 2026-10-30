#!/usr/bin/env python3
"""The steps of a release (docs/release.md).

    release.py check [--allow-untagged]         refuse a dirty tree, and a HEAD without a version tag
    release.py prepare X.Y.Z                    rename "Not yet released" in CHANGELOG.md to the version and the date
    release.py next                             add a new "Not yet released" section to CHANGELOG.md
    release.py package --rom R --bps B --page P --base BASE --out DIR
                                                check the files and write the release folder
    release.py publish [--yes]                  make the GitHub release for the tag on HEAD (needs gh)

`make release` runs check and package. Only publish talks to GitHub, and only the owner runs it.
"""
import argparse
import datetime
import hashlib
import os
import re
import shutil
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)
import bps  # noqa: E402
import version  # noqa: E402

CHANGELOG = os.path.join(ROOT, "CHANGELOG.md")
UNRELEASED = "Not yet released"
OUT = os.path.join(ROOT, "build", "release")


def die(msg):
    sys.exit(f"release: {msg}")


def sha1(path):
    return hashlib.sha1(open(path, "rb").read()).hexdigest()


def yukon_date():
    """Today's date in UTC-7 (Yukon time)."""
    return datetime.datetime.now(datetime.timezone(datetime.timedelta(hours=-7))).strftime("%Y-%m-%d")


# ---- the changelog ----

def sections(text):
    """The changelog as (heading, body) pairs after the title. A heading is a line that starts with "## "."""
    parts = re.split(r"(?m)^## (.*)$", text)
    return parts[0], [(parts[i], parts[i + 1]) for i in range(1, len(parts), 2)]


def find_section(text, wanted):
    """The body of the section for a version ("1.0.0": "## 1.0.0" or "## 1.0.0 (date)") or for UNRELEASED."""
    for heading, body in sections(text)[1]:
        if heading == wanted or heading.startswith(wanted + " "):
            return body.strip("\n")
    return None


def prepare(number, repo=ROOT, date=None):
    text = open(CHANGELOG).read()
    if find_section(text, number) is not None:
        die(f"CHANGELOG.md already has a section for {number}")
    if f"\n## {UNRELEASED}\n" not in "\n" + text:
        die(f'CHANGELOG.md has no "## {UNRELEASED}" section')
    text = text.replace(f"## {UNRELEASED}\n", f"## {number} ({date or yukon_date()})\n", 1)
    open(CHANGELOG, "w").write(text)
    print(f"CHANGELOG.md: {UNRELEASED} is now {number}")


def start_next():
    text = open(CHANGELOG).read()
    if find_section(text, UNRELEASED) is not None:
        die(f'CHANGELOG.md already has a "{UNRELEASED}" section')
    m = re.search(r"(?m)^## ", text)
    if not m:
        die("CHANGELOG.md has no section")
    text = text[:m.start()] + f"## {UNRELEASED}\n\nNothing yet.\n\n" + text[m.start():]
    open(CHANGELOG, "w").write(text)
    print(f'CHANGELOG.md: new "{UNRELEASED}" section')


# ---- the steps ----

def check(allow_untagged):
    if version.is_dirty(ROOT):
        die("the tree has changes. Commit them first (git status shows them).")
    if version.release_number(ROOT) is None and not allow_untagged:
        die("HEAD has no version tag (vX.Y.Z). Tag the release commit, or set ALLOW_UNTAGGED=1 for a trial run.")
    print(f"release: version {version.text(ROOT)}")


def package(a):
    number = version.release_number(ROOT)
    name = version.name(ROOT)
    text = version.text(ROOT)
    if version.is_dirty(ROOT):
        die("the tree has changes")
    notes_body = find_section(open(CHANGELOG).read(), number if number else UNRELEASED)
    if notes_body is None:
        die(f'CHANGELOG.md has no section for {number}. Run "tools/release.py prepare {number}" and commit.' if number
            else f'CHANGELOG.md has no "{UNRELEASED}" section')

    rom = open(a.rom, "rb").read()
    base = open(a.base, "rb").read()
    patch = open(a.bps, "rb").read()
    page = open(a.page).read()
    wanted_base = open(os.path.join(ROOT, "gloveworld", "NGVE.sha1")).read().split()[0]
    if hashlib.sha1(base).hexdigest() != wanted_base:
        die("the base ROM is not the clean Glover (USA) ROM")
    if text.encode() not in rom:
        die(f'the ROM does not hold the version text "{text}"')
    if f'const VERSION = "{text}";' not in page:
        die(f'the patch page does not show the version "{text}"')
    if bps.apply(base, patch) != rom:
        die("the BPS patch does not give the built ROM")
    print("release: the patch gives the built ROM, and the version shows in the ROM and the page")

    shutil.rmtree(a.out, ignore_errors=True)
    os.makedirs(a.out)
    files = {"bps": f"glover-practice-{name}.bps", "html": f"glover-practice-{name}.html"}
    shutil.copy(a.bps, os.path.join(a.out, files["bps"]))
    shutil.copy(a.page, os.path.join(a.out, files["html"]))
    rom_sum = hashlib.sha1(rom).hexdigest()
    with open(os.path.join(a.out, "SHA1SUMS"), "w") as f:
        for kind in ("bps", "html"):
            f.write(f"{sha1(os.path.join(a.out, files[kind]))}  {files[kind]}\n")
        f.write(f"# the patched ROM (not shipped), z64 byte order:\n# {rom_sum}  glover-practice.z64\n")
    with open(os.path.join(a.out, f"release-notes-{name}.md"), "w") as f:
        f.write(f"{notes_body}\n\n## Files\n\n"
                f"- `{files['bps']}`: the patch for any BPS patcher\n"
                f"- `{files['html']}`: the patch page; open it in a browser\n\n"
                f"SHA-1 of the patched ROM (`.z64`): `{rom_sum}`\n")
    print(f"release: {a.out}")
    for n in sorted(os.listdir(a.out)):
        print(f"  {n}")


def publish(yes):
    number = version.release_number(ROOT)
    if number is None:
        die("HEAD has no version tag (vX.Y.Z)")
    if version.is_dirty(ROOT):
        die("the tree has changes")
    name = f"v{number}"
    paths = [os.path.join(OUT, f) for f in
             (f"glover-practice-{name}.bps", f"glover-practice-{name}.html", "SHA1SUMS", f"release-notes-{name}.md")]
    for p in paths:
        if not os.path.exists(p):
            die(f"{p} is missing. Run make release first.")
    head = version.git(ROOT, "rev-parse", "HEAD")[1]
    remote = subprocess.run(["git", "-C", ROOT, "ls-remote", "origin", f"refs/tags/{name}", f"refs/tags/{name}^{{}}"],
                            capture_output=True, text=True).stdout.split()
    if not remote:
        die(f"the tag {name} is not on GitHub. Push it first: git push origin {name}")
    if head not in remote[::2]:
        die(f"the tag {name} on GitHub points at another commit than HEAD")
    cmd = ["gh", "release", "create", name, *paths[:3], "--title", f"Glover Practice ROM {number}",
           "--notes-file", paths[3], "--verify-tag"]
    print("release: " + " ".join(cmd))
    if not yes and input("Publish this release? [y/N] ").strip().lower() != "y":
        die("not published")
    sys.exit(subprocess.run(cmd, cwd=ROOT).returncode)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    c = sub.add_parser("check")
    c.add_argument("--allow-untagged", action="store_true")
    sub.add_parser("prepare").add_argument("number")
    sub.add_parser("next")
    p = sub.add_parser("package")
    for opt in ("rom", "bps", "page", "base", "out"):
        p.add_argument("--" + opt, required=True)
    sub.add_parser("publish").add_argument("--yes", action="store_true")
    a = ap.parse_args()
    if a.cmd == "check":
        check(a.allow_untagged)
    elif a.cmd == "prepare":
        if not re.fullmatch(r"\d+\.\d+\.\d+", a.number):
            die("the version is X.Y.Z, for example 1.0.0")
        prepare(a.number)
    elif a.cmd == "next":
        start_next()
    elif a.cmd == "package":
        package(a)
    else:
        publish(a.yes)


if __name__ == "__main__":
    main()
