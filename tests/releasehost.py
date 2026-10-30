#!/usr/bin/env python3
"""Host test of the release process (docs/release.md), without an emulator. In throw-away git repositories it checks:
the version text (with a tag, without a tag, with changes, after a tag), that `make release` refuses a tree with
changes and a HEAD without a version tag, and the changelog steps."""
import os
import re
import shutil
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, os.path.join(ROOT, "tools"))
import release  # noqa: E402
import version  # noqa: E402

failures = []


def expect(label, got, want):
    ok = got == want
    print(("ok   " if ok else "FAIL ") + f"{label}: {got!r}" + ("" if ok else f" (wanted {want!r})"))
    if not ok:
        failures.append(label)


def git(repo, *args):
    subprocess.run(["git", "-C", repo, "-c", "user.name=t", "-c", "user.email=t@example.com", *args],
                   check=True, capture_output=True)


def commit(repo, n):
    open(os.path.join(repo, "f.txt"), "w").write(str(n))
    git(repo, "add", "-A")
    git(repo, "commit", "-m", f"c{n}")


def make_release(repo, *extra):
    return subprocess.run(["make", "-C", repo, "release", *extra], capture_output=True, text=True)


def main():
    tmp = tempfile.mkdtemp(prefix="releasehost-")
    try:
        repo = os.path.join(tmp, "r")
        os.mkdir(repo)
        git(repo, "init", "-q")
        shutil.copy(os.path.join(ROOT, "Makefile"), repo)
        shutil.copytree(os.path.join(ROOT, "tools"), os.path.join(repo, "tools"), ignore=shutil.ignore_patterns("__pycache__"))
        open(os.path.join(repo, ".gitignore"), "w").write("build/\n__pycache__/\n")
        commit(repo, 1)
        h = version.short_hash(repo)
        expect("no tag", version.text(repo), f"0.1.0-dev {h}")
        expect("no tag, file name", version.name(repo), "v0.1.0-dev")
        expect("no tag, number", version.release_number(repo), None)
        expect("the longest text fits 18 characters", len(version.text(repo) + "+") <= 18, True)

        open(os.path.join(repo, "f.txt"), "w").write("changed")
        expect("no tag, changes", version.text(repo), f"0.1.0-dev {h}+")
        r = make_release(repo)
        expect("make release refuses changes", (r.returncode != 0, "has changes" in r.stderr), (True, True))
        expect("make release built nothing", os.path.exists(os.path.join(repo, "build", "gw")), False)
        git(repo, "checkout", "f.txt")

        r = make_release(repo)
        expect("make release refuses without a tag", (r.returncode != 0, "no version tag" in r.stderr), (True, True))
        open(os.path.join(repo, "untracked.txt"), "w").write("x")
        r = make_release(repo, "ALLOW_UNTAGGED=1")
        expect("an untracked file counts as changes", (r.returncode != 0, "has changes" in r.stderr), (True, True))
        os.remove(os.path.join(repo, "untracked.txt"))

        git(repo, "tag", "v1.0.0")
        expect("tag on HEAD", version.text(repo), "1.0.0")
        expect("tag on HEAD, file name", version.name(repo), "v1.0.0")
        open(os.path.join(repo, "f.txt"), "w").write("changed")
        expect("tag on HEAD, changes", version.text(repo), "1.0.0+")
        git(repo, "checkout", "f.txt")

        commit(repo, 2)
        h = version.short_hash(repo)
        expect("one commit after a tag", version.text(repo), f"1.0.1-dev {h}")
        git(repo, "tag", "v1.0.9")
        git(repo, "tag", "v1.0.10")
        git(repo, "tag", "not-a-version")
        expect("the highest tag counts", version.text(repo), "1.0.10")
        commit(repo, 3)
        h = version.short_hash(repo)
        expect("next version of a tag with two digits", version.text(repo), f"1.0.11-dev {h}")
        out = subprocess.run([sys.executable, os.path.join(ROOT, "tools", "version.py"), "--repo", repo, "header"],
                             capture_output=True, text=True).stdout.strip()
        expect("header line", out, f'#define PRACTICE_VERSION "1.0.11-dev {h}"')

        # The changelog steps.
        log = os.path.join(tmp, "CHANGELOG.md")
        open(log, "w").write("# Changelog\n\n## Not yet released\n\nThe new things.\n\n### Menu\n\n- A\n\n## 0.0.1\n\nOld.\n")
        release.CHANGELOG = log
        release.prepare("1.0.0", date="2026-11-01")
        text = open(log).read()
        expect("prepare renames the heading", "## 1.0.0 (2026-11-01)\n\nThe new things." in text, True)
        expect("prepare removes the old heading", release.UNRELEASED in text, False)
        expect("notes of a version", release.find_section(text, "1.0.0"), "\nThe new things.\n\n### Menu\n\n- A".strip("\n"))
        expect("notes do not run into the next section", "Old." in release.find_section(text, "1.0.0"), False)
        expect("a version prefix is not another version", release.find_section(text, "1.0"), None)
        release.start_next()
        text = open(log).read()
        expect("next adds the section on top", text.index(release.UNRELEASED) < text.index("## 1.0.0"), True)
        expect("the date is a date", bool(re.fullmatch(r"\d{4}-\d\d-\d\d", release.yukon_date())), True)
    finally:
        shutil.rmtree(tmp, ignore_errors=True)
    if failures:
        sys.exit(f"releasehost: {len(failures)} failed: {', '.join(failures)}")
    print("PASS releasehost: all passed")


if __name__ == "__main__":
    main()
