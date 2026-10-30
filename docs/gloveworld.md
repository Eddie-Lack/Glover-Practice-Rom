# Notes on gloveworld

gloveworld is a separate project. The practice ROM uses it as it is (CLAUDE.md). This file
lists changes to gloveworld that would help the practice ROM, and errors or gaps found in it. Each entry says
what was found, the evidence, and whether it needs the owner's decision. Agents tell the owner when an
entry needs one.

## Changes that would help the practice ROM

The hooks are patch files in `hooks/`, applied to a copy of gloveworld's source.

- **The assets blob includes the zero padding.** `bin/assets.bin` runs to the end of the 8 MiB, so it holds the
  0x821E0 zero bytes after `recordKeyFilesRomEnd`. The practice build uses that space for its loader and
  practice segment, and cuts the blob first (`tools/trimassets.py`). A split that ends the assets at
  `recordKeyFilesRomEnd`, with the padding added at the link, would make the cut unnecessary. Needs the owner: no.

- **The Hard path of `CheckForExits` has no function of its own.** At the end of a Time Trial level (Hard) the code
  in `CheckForExits` compares `levelTime` with `bestTimes[]` itself, then goes to the name screen or starts the
  title lists. It never calls `CheckForLevelCompleted`. The practice build hooks two calls on these paths
  (`PrepareSong(TITLE_TRACK)` and `InitOverlayList`, [build.md](build.md#layout)). One named function for this block
  would need one hook. Needs the owner: no.

## Errors and gaps found

- **The best times table is declared two ways, and the save covers 12 more bytes.** `eeprom.c` declares `bestTimes`
  as `u8[6][0x1E]` (180 bytes); the other files declare `BestTime[]`. `EepromSaveBestTimes`, `EepromLoadBestTimes`
  and the CRC cover 0xC0 bytes, the table and 12 bytes of padding up to `bestTimesCopy` (0x801E6850). Needs the
  owner: no.

- **Unnamed OS and music driver variables.** Found by the faithfulness test, which masks them
  (`tests/ramcheck.py`). Needs the owner: no.
  - `D_802AE990` is the PI manager's thread and stack: `osCreatePiManager` loads its address and creates the
    thread there.
  - `D_802AB010` is set up by `MusInitialize`. `D_801F4170` and `D_801F4174` are counters that the audio
    functions `func_801C3040` and `func_801C33A4` read and write.
- **A label that looks misplaced.** `_objBank9_data_objend` is at 0x802AF201, an odd address, in the bss after
  the PI manager's thread. During play the word at 0x802AFB00, after the label, holds a pointer into
  `musicHeap`. It looks like music driver state rather than the end of an object bank. Needs the owner: no.
- **`tools/emu/setup.sh` can skip packages.** It installs its packages only when `libmupen64plus.so.2` is
  missing. On a machine that already has the mupen64plus core, it does not install the z64 RSP plugin, Xvfb or
  cmake, which angrylion and `emu.py` need. Needs the owner: no.
- **Facts about `tools/emu` that its README does not have.** Found while making the practice ROM's harness
  ([testing.md](testing.md)). Needs the owner: no.
  - angrylion's worker threads (`Parallel`) make two runs of the same ROM differ by a few video frames during
    loads. With one thread, runs repeat exactly.
  - mupen64plus keeps one EEPROM save per ROM. A save left by an earlier run changes the game's timing at boot.
  - The frame callback runs only when the video plugin shows a new frame, so a script stops advancing on a
    screen that never changes.
- **`emu.py` gives a patched ROM the retail ROM's settings.** `data_dir_for` gives a patched ROM the retail ROM's entry in the core's ini, and
  `state_for` copies each state with the patched ROM's MD5 before loading it. The retail entry sets `Players=1`,
  so controller 2 and its Controller Pak are gone, and a state loaded on the frame after its save is read before
  the core has written it. `tests/harness.py` replaces both functions for the practice
  ROM. A setting in `emu.py` for this would help. Needs the owner: no.
