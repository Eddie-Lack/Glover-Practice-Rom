# Notes on gloveworld

gloveworld is a separate project. The practice ROM uses it as it is (CLAUDE.md). This file
lists changes to gloveworld that would help the practice ROM, and errors or gaps found in it. Each entry says
what was found, the evidence, and whether it needs the owner's decision. Agents tell the owner when an
entry needs one.

## Changes that would help the practice ROM

The hooks are patch files in `hooks/`, applied to a copy of gloveworld's source.

- **The Hard path of `CheckForExits` has no function of its own.** At the end of a Time Trial level (Hard) the code
  in `CheckForExits` compares `levelTime` with `bestTimes[]` itself, then goes to the name screen or starts the
  title lists. It never calls `CheckForLevelCompleted`. The practice build hooks two calls on these paths
  (`PrepareSong(TITLE_TRACK)` and `InitOverlayList`, [build.md](build.md#layout)). One named function for this block
  would need one hook. Needs the owner: no.

## Errors and gaps found

- **Unnamed music driver variables.** Found by the faithfulness test, which masks them (`tests/ramcheck.py`).
  `D_802AB010` is set up by `MusInitialize`. `D_801F4170` and `D_801F4174` are counters that the audio functions
  `func_801C3040` and `func_801C33A4` read and write. Needs the owner: no.
