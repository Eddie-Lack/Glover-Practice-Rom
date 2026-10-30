#!/usr/bin/env python3
"""The practice ROM's version, from the git tags.

    version.py [--repo DIR] text      the text for the menu and the patch page
    version.py [--repo DIR] name      the version for file names (v1.0.0, or v1.0.1-dev)
    version.py [--repo DIR] number    the release number (1.0.0) if HEAD has a version tag, else exit 1
    version.py [--repo DIR] dirty     exit 0 if the tree has changes, 1 if it is clean
    version.py [--repo DIR] header    the line for build/version.h

A version tag is `vX.Y.Z` (X, Y and Z are numbers) on HEAD. The text is then `X.Y.Z`.
Without a tag on HEAD the build is a development build of the next version: the last tag with its patch number
raised by one (`0.1.0` if there is no tag yet), then `-dev` and the short hash of HEAD, in capitals:
`1.0.1-dev 3F2A9C1`. A tree with changes adds `+`. The longest text is 18 characters.
"""
import argparse
import re
import subprocess
import sys

TAG_GLOB = "v[0-9]*.[0-9]*.[0-9]*"
TAG_RE = re.compile(r"^v(\d+)\.(\d+)\.(\d+)$")


def git(repo, *args):
    r = subprocess.run(["git", "-C", repo, *args], capture_output=True, text=True)
    return r.returncode, r.stdout.strip()


def is_dirty(repo="."):
    """True if the tree has changes: tracked, staged or untracked (ignored files do not count)."""
    code, out = git(repo, "status", "--porcelain")
    return code == 0 and out != ""


def release_number(repo="."):
    """"1.0.0" if a version tag is on HEAD, else None. With several tags the highest one counts."""
    code, out = git(repo, "tag", "--points-at", "HEAD", "--list", TAG_GLOB)
    tags = [(tuple(map(int, m.groups())), t) for t in out.split() if (m := TAG_RE.match(t))] if code == 0 else []
    return ".".join(map(str, max(tags)[0])) if tags else None


def next_number(repo="."):
    """The version a development build leads to."""
    code, out = git(repo, "describe", "--tags", "--abbrev=0", "--match", TAG_GLOB, "HEAD")
    m = TAG_RE.match(out) if code == 0 else None
    if not m:
        return "0.1.0"
    x, y, z = map(int, m.groups())
    return f"{x}.{y}.{z + 1}"


def short_hash(repo="."):
    code, out = git(repo, "rev-parse", "--short=7", "HEAD")
    return out.upper() if code == 0 and out else "UNKNOWN"


def text(repo="."):
    number = release_number(repo)
    s = number if number else f"{next_number(repo)}-dev {short_hash(repo)}"
    return s + ("+" if is_dirty(repo) else "")


def name(repo="."):
    number = release_number(repo)
    return "v" + (number if number else next_number(repo) + "-dev")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--repo", default=".")
    ap.add_argument("what", choices=["text", "name", "number", "dirty", "header"])
    a = ap.parse_args()
    if a.what == "text":
        print(text(a.repo))
    elif a.what == "name":
        print(name(a.repo))
    elif a.what == "number":
        n = release_number(a.repo)
        if n is None:
            sys.exit(1)
        print(n)
    elif a.what == "dirty":
        sys.exit(0 if is_dirty(a.repo) else 1)
    else:
        print(f'#define PRACTICE_VERSION "{text(a.repo)}"')


if __name__ == "__main__":
    main()
