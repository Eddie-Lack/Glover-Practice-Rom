# Releasing

A release is a git tag `vX.Y.Z` on a commit, and a GitHub release for that tag with the patch and the patch page
attached. The owner decides when to release and runs the publish step. Agents prepare everything up to it.

## The version

The version is `X.Y.Z` (major, minor, patch). The tag `vX.Y.Z` on HEAD sets it. `tools/version.py` reads it.

| HEAD | Menu and patch page | File names |
| --- | --- | --- |
| tagged `v1.0.0` | `1.0.0` | `v1.0.0` |
| after tag `v1.0.0`, no tag on HEAD | `1.0.1-dev 3F2A9C1` | `v1.0.1-dev` |
| no tag in the history | `0.1.0-dev 3F2A9C1` | `v0.1.0-dev` |

- A development build leads to the next patch number. Raise the minor or major number with the tag of the release.
- `3F2A9C1` is the short hash of HEAD, in capitals.
- A tree with changes adds `+` (`1.0.1-dev 3F2A9C1+`). Untracked files count as changes. Ignored files do not.
- The longest text is 18 characters. The practice menu (Glover Practice, last item "Version") and the patch page
  show it. The ROM holds it as `PRACTICE_VERSION` from `build/version.h`, which `make` writes on every run.
- With several tags on HEAD, the highest counts. Tags that are not `vX.Y.Z` are ignored.

## Steps

1. Work on the commit to release. It must be on `main` and pushed.
2. Run the checks. All must pass:

   ```sh
   make                       # checklayout must pass
   python3 tests/run.py       # the full run, 7 minutes longer than --quick
   python3 tests/faithful.py  # the game plays as retail with every feature off
   ```

   Run the emulator tests under `flock /tmp/emu.lock` when another job may use the emulator.
3. Update `CHANGELOG.md`. The section "Not yet released" lists what players get. Then:

   ```sh
   python3 tools/release.py prepare 1.0.0   # renames the section to "1.0.0 (date)", date in UTC-7 (Yukon time)
   git commit -am "Changelog: 1.0.0"
   ```

4. Tag the commit and push the tag:

   ```sh
   git tag -a v1.0.0 -m "Glover Practice ROM 1.0.0"
   git push origin main v1.0.0
   ```

5. Build the release:

   ```sh
   make release
   ```

   It refuses a tree with changes and a HEAD without a version tag. `make release ALLOW_UNTAGGED=1` is a trial run
   on an untagged commit (development version, notes from "Not yet released"). `make release-check` runs only the
   check. It then builds the ROM, runs `tools/checklayout.py`, builds the patch and the patch page, and checks that:

   - the base ROM is the clean Glover (USA) ROM (`gloveworld/NGVE.sha1`),
   - the BPS patch applies to it and gives the built ROM, byte for byte,
   - the version text is in the ROM and on the patch page,
   - the changelog has a section for the version.

6. Check `build/release/`:

   | File | What |
   | --- | --- |
   | `glover-practice-v1.0.0.bps` | the patch |
   | `glover-practice-v1.0.0.html` | the patch page, with the patch inside |
   | `SHA1SUMS` | SHA-1 of both files (`sha1sum -c SHA1SUMS`), and of the patched ROM as a comment |
   | `release-notes-v1.0.0.md` | the changelog section and the file list; it is the body of the release |

   Open the page in a browser and patch a ROM (`node tests/patchpage.js` does this in Chromium after `make patch`).
   Boot the patched ROM, open the practice menu and read the version.
7. Publish. This is the only step that talks to GitHub. It needs `gh` and a login with write access:

   ```sh
   python3 tools/release.py publish
   ```

   It checks that HEAD has a version tag, that the tag is on GitHub at the same commit and that the files exist. It
   shows the `gh release create` command and asks. `--yes` skips the question. It attaches the patch, the patch page
   and `SHA1SUMS`, with the notes as the body. The ROM is never attached.
8. After the release, start the next changelog section and commit:

   ```sh
   python3 tools/release.py next
   git commit -am "Changelog: start the next version"
   ```

## Tests

`tests/releasehost.py` needs no emulator and is part of `tests/run.py`. In throw-away git repositories it checks the
version text (tag, no tag, changes, commits after a tag), that `make release` refuses changes and a missing tag, and
the changelog steps.
