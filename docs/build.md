# The build

This file explains how the practice ROM is built, and why. The steps for a contributor are in the
[README](../README.md#building).

## Setting up a machine

On Ubuntu 24.04 the build needs:

```sh
sudo apt install git python3 python3-venv build-essential gcc-multilib bison gperf binutils-mips-linux-gnu
```

`make setup` builds gloveworld's compilers (GCC 2.7.2) with `gcc-multilib`. The practice code needs a current
MIPS GCC, `gcc-mips-linux-gnu`. On Ubuntu that package conflicts with `gcc-multilib`: installing one removes
the other. Run `make setup` first, then install `gcc-mips-linux-gnu`. The compilers that `make setup` built
stay in `gloveworld/tools/gcc-2.7.2/`.

`make setup BASEROM=path/to/glover.z64` checks the ROM's SHA-1, links it as `baserom.us.z64` (ignored by git),
creates gloveworld's Python environment, builds its toolchain if needed, and splits the ROM inside the
submodule.

The practice build never runs gloveworld's `tools/regen.py`.

## What `make` does

1. It runs gloveworld's own `make`. Its ROM must still match the retail ROM: the practice build uses its
   assembly objects, its binary objects and its ELF.
2. It copies `gloveworld/src` to `build/gw/src` and applies the patches in `hooks/`.
3. It compiles that copy with gloveworld's compiler and flags, plus `-DPRACTICE`, which turns the hooks on.
4. It compiles the practice code in `practice/src/` twice: once for the practice segment, and once more
   (`-DPRACTICE_LOADER`) for the loader, which needs its own copy of the text code.
5. `tools/trimassets.py` cuts any zero padding off gloveworld's assets blob, and `objcopy` makes
   `build/assets.o` from the rest ([The free end of the ROM](#the-free-end-of-the-rom)).
6. `tools/mkld.py` writes the linker script from gloveworld's `NGVE.ld`. It uses the cut assets blob and puts the
   loader and the practice segment into the free end of the retail ROM.
7. It links, pads the ROM to 8 MiB (`ROM_SIZE` in the Makefile), writes the header's name (`GLOVER PRACTICE`) and
   checksum (`tools/mkrom.py`, which fails if the ROM is not 8 MiB).
8. `tools/checklayout.py` checks that every game label is at its retail address, that the loader and the practice
   segment lie in the free end with only zero bytes around them, and lists the words of the retail data that
   differ. It prints how much of the free end the build uses, and how much of the practice RAM area (0x80400000 to
   0x80480000: code, data and bss) is used and left.

`build/version.h` holds the version text (`tools/version.py`: the tag `vX.Y.Z` on HEAD, or the next version with `-dev` and
the short hash; `+` for a tree with changes). `make release` builds the files for a release in `build/release/`
([release.md](release.md)).

`make patch` writes `build/glover-practice.bps` (`tools/bps.py`) and `build/glover-practice.html`, the patch
page with the patch built in (`tools/mkpage.py` fills `tools/patch.html`). `make test` runs the emulator tests
([testing.md](testing.md)). `make testrom` builds `build/test/glover-practice-test.z64`, the same ROM with the SD card
test mailbox in `sd.c`; the SD card tests run on it ([Savestates on the SD card](#savestates-on-the-sd-card)).

## Layout

The practice build keeps the retail ROM's layout. Every function and variable of the game is at its retail
address, in ROM and in RAM. The ROM has the retail ROM's size, 8 MiB. Only these words of the retail data differ
from the retail ROM, each a single call:

| Place | Change |
| --- | --- |
| header | the name and the checksum |
| `MainProc` | `MeasureFrameTicks` becomes `PracticeMeasureFrameTicks`: the boot hook |
| `DrawGraphics` | `SendLastFrame` becomes `PracticeSendLastFrame`: the start of the frame's display list (the fog switch) |
| `SetupViewing` | `guLookAtHilite` becomes `PracticeguLookAtHilite`: the free camera's view (the game's `ShadingMode` is 2; its `guLookAt` branch never runs) |
| `DrawGraphics` | `PrintTimers` becomes `PracticePrintTimers`: the end of the frame's display list (HUD, menu) |
| `ControllerProc` | `ReadController` becomes `PracticeReadController`: the input layer, the menu's input, EEPROM work; `ReadControllerFromData` (demo playback) becomes `PracticeReadControllerFromData`, the same during a demo |
| `RunGame` | `ClearText` becomes `PracticeClearText`: the start of each game frame's logic (pause, lag) |
| `RunGame` | `DoMenu` becomes `PracticeDoMenu`: it calls `TitleScreen` (the early title menu) or `DoMenu` |
| `MainGameLoop` | `ProcessInput` becomes `PracticeProcessInput`: the ball controls |
| `StartNewLevel` | `SeedRandom` becomes `PracticeSeedRandom`: the start of each level (the in-game timer fix) |
| `StartNewLevel` | `InitScreenDebris` (the first call after `LoadLevel`) becomes `PracticeInitScreenDebris`: the controller display's textures |
| `PrintOverlays` | `PrintStats` becomes `PracticePrintStats`: the controller display, drawn after the game's HUD |
| `EndOfLevelScreen`, `EndOfWorldScreen`, `CheckForExits` | `CheckForLevelCompleted` becomes `PracticeCheckForLevelCompleted` (one call in each): the practice best times |
| `CheckForExits` | on the Hard path (Time Trials only) `PrepareSong(TITLE_TRACK)` (a record, to the name screen) and `InitOverlayList` (no record, to the title) become `PracticePrepareSong` and `PracticeInitOverlayList`: the best times on a path that never calls `CheckForLevelCompleted` |
| `HiscoresRun` | each of its six `printHiscores` calls becomes `PracticeprintHiscores`: the Hi Scores screen |
| `ClearStaticMemory` | the boot hook, of the same size (0x74 bytes), takes its place. Nothing calls it |

The rest of the patch is the practice code. The rest of the ROM:

| ROM | RAM | What |
| --- | --- | --- |
| 0x000000 to 0x77DE1F | as retail | the retail ROM's layout and data |
| 0x77DE20 to 0x77FFFF | | zero, as in retail |
| 0x780000 | 0x80380000 | the loader, about 2.7 KB, and its 3 KB of bss |
| after the loader | 0x80400000 | the practice segment (code, data, then bss) |
| up to 0x7FFFFF | | zero |

### The free end of the ROM

The retail ROM's data ends at 0x77DE20 (`recordKeyFilesRomEnd`). The rest of its 8 MiB, 0x821E0 bytes, is zero.
The loader and the practice segment go there, from 0x780000, the first round address after the data. The ROM
keeps its size, which suits every flash cart and emulator, and the patch changes only the bytes it must.

Nothing in the game reads the free end:

- The game reaches the ROM through the addresses that its symbols name (`symbols.ld`, 0xB0000000 and up): the
  start and the end of each bank, track, level file and recording. `recordKeyFilesRomEnd` is the highest.
- `DMAMemory` and `DmaRomToRam` take a ROM address from such a start or end, or an offset inside them.
  `LoadLandscapeFile` and
  `LoadRecordKeyFile` (banks.c) walk a file list by the size in each header. They stop with an error at
  `landscapeFilesRomEnd` and at `recordKeyFilesRomEnd`.
- `tests/romlayout.py` checks on every run that the retail ROM is zero after its data and that no game symbol
  names a ROM address past it.

gloveworld's assets blob (`bin/assets.bin`) runs from the end of the main segment to `recordKeyFilesRomEnd`. An
older gloveworld's blob ran to the end of the 8 MiB and held the zero padding. A linker script cannot cut an input
section, so the Makefile passes the blob through `tools/trimassets.py` first. It writes `build/trim/bin/assets.bin`
up to `recordKeyFilesRomEnd`, copies a blob that already ends there as it is, and fails if a byte it drops is not
zero. `objcopy` makes `build/assets.o` from it. `checklayout.py` reads the blob's padding from `assets_ROM_END`.
With an older gloveworld, three labels at the end of the blob move up by that padding: `assets_DATA_END`,
`assets_VRAM_END` and `_binary_bin_assets_bin_end`. Nothing reads them.

The free end holds 512 KB from 0x780000. The practice segment's RAM is also limited to 512 KB
(0x80400000 to 0x80480000, code, data and bss). The ROM space is enough as long as that limit stands. The link
fails with a message when the loader and the practice segment would end past the ROM, and `tests/romlayout.py`
tests this. The loader and the practice segment take 130 KB (25%) in October 2026; `make` prints the current size. If they ever outgrow the space, the ROM
must grow again, which is a decision for the owner ([decisions.md](decisions.md)).

The header checksum covers only ROM 0x1000 to 0x101000. The place of the loader does not change how the
checksum is made.

## Boot

1. MainProc creates the scheduler and the message queues, then calls `PracticeMeasureFrameTicks` where it
   called `MeasureFrameTicks`.
2. That hook (`hooks/asm/PracticeMeasureFrameTicks.s`) copies the loader from ROM to 0x80380000 with the
   game's `DmaRomToRam`, and calls `PracticeLoader`. 0x80380000 is in the game's heap. The game has initialised
   its allocator by then but has not allocated anything; the allocator takes blocks from the top of the heap
   and keeps its block list in its own bss (`jallocControl`).
3. `PracticeLoader` (`practice/src/loader/loader.c`) checks `osMemSize`, which the console's boot code sets.
   - Below 8 MB it shows "Expansion Pak required" and stops. It draws with the game's own display list code
     (`InitDisplayLists`, `ClearFrameBuffer`, `CleanupAndSendDisplayList`, `SendLastFrame`) and the RDP. A
     frame buffer drawn by the CPU stays black in emulators with high-level graphics plugins, and those
     emulators show a frame only when the frame buffer changes. It sends a new frame every two video
     frames.
   - With the Expansion Pak it copies the practice segment to 0x80400000 and clears its bss.
4. The hook calls `PracticeInit`, which clears the loader's memory back to zero, sets up the practice code and
   reads the saved settings from the EEPROM. Then it calls `MeasureFrameTicks`, as the game did.

The settings are read here because the game has not set up the serial interface yet: no request of the game's
can collide with the read. `PracticeInit` gives the serial interrupt its own message queue for this;
`initControllers` sets the game's own later. Saving happens in `PracticeReadController`, in the controller
thread after its read, when the game has no EEPROM request waiting, as the game does its own EEPROM work.

## The menu pages and the menu look

`practice/src/menu.c` draws and moves through the menus; `practice/src/menus.c` holds the tables. A menu shows at
most `MENU_ROWS` (14) items. With the title row and the hint line the panel is 189 pixels high. A longer menu
scrolls (the cheats, 26 items, the HUD layout, 22, and the splits, 48); the title row then shows the cursor's place
as "n/N". Every other menu has at most 14 items and fits as it is. Sub pages keep the first page to the items that
are used most: Savestate > SD card, HUD > Counters, Game > Random numbers, Controller 2 and Movement and camera,
and Inspect > Collision view (with its own sub page, Faces shown). `tests/menupages.py` walks every page and checks the frame buffer.

The Settings menu has "Menu opacity" and "Menu position across" and "Menu position down". `menu.c` keeps the table of
the nine places of the panel (x 12, 32 or 52; y 12, 28 or 36) and the six alpha values of the panel
(0, 0x40, 0x80, 0xC0, 0xD8 and 0xFF; the title bar is 16 more). The panel is 256 pixels wide, and the lowest place
ends a 14-row panel at pixel 225 of 240. `MenuPanelColor` gives the panel and title bar colours to `menu.c` and to
the seven tools that take over the panel (actor list, memory viewer, frame log, glove and ball, Spawn actor, save file
editor, Controller Pak view). Those tools keep their own place and size: they are 288 or 304 pixels wide and have no
room to move. The frame log reaches pixel 230 of 240.

The three values are `menuOpacity`, `menuAcross` and `menuDown` in `Settings` (0x4B to 0x4D, where the unused HUD
slots 11 and 12 were). They are stored from 1. In the EEPROM block they take the bytes of `hudPlace[11]` and
`hudPlace[12]`, which an older ROM wrote as zero, so an older block and an older Controller Pak note read as 0 and
`SettingsFixMenuLook` gives the defaults (85%, centre, middle). A value out of range gets the default too. The version
stays 5. `tests/settingsold.py` loads a block with the three bytes zeroed.

## Savestates

A savestate is a copy of the game's own memory. `tools/mkstate.py` lists what it holds, from gloveworld's symbols,
into `build/state_ranges.h`:

- the game's data, from `main_DATA_START` to the music driver's data (`jumptable`);
- the game's bss, from `main_BSS_START` to the music driver's bss (`D_802AB010`);
- the heap, from `jallocHeapStart` to 0x80400000;
- the texture bank buffers, from `sysfont` to 0x80100000.

It leaves out what keeps running across a load: the threads' contexts and stacks, the scheduler and its message
queues, the display list buffers (`dynamic`), the controller read (`controllerdata`), the music driver and its
heap, the game's handle to the playing song (`audioCtrl`), libultra, the frame buffers and the Z buffer. That is
0x1DFE54 bytes (1.9 MB). States are kept compressed, in a pool.

### The pool

The Expansion Pak's upper 4 MB is laid out as follows (docs/plan.md has the whole map):

| Range | Use |
| --- | --- |
| 0x80400000 to 0x8047FFFF | the practice segment (code, data, bss) |
| 0x80480000 to 0x806EBFFF | the pool (2.4 MB) |
| 0x806EC000 to 0x806EFFFF | the SD card test mailbox, used only by the test ROM (`make testrom`) |
| 0x806F0000 to 0x806FFFFF | the work of a save and a load: two blocks of 16 KB |
| 0x80700000 to 0x8077FFFF | the ghosts |
| 0x80780000 to 0x807FFFFF | the input recording (512 KB; it was 1 MB, and made room for the pool) |

The pool holds two kinds of item, one after the other from 0x80480000. A freed item closes its gap: the items
behind it move down (a copy of at most a few hundred kilobytes). Free space is one block at the end.

- A **base** is a whole state, compressed on its own. There are at most three (`STATE_BASES`), one for each level that
  has states. A base takes about 0.6 MB and the pool is 2.4 MB. A fourth base never fits next to the other three.
- A **slot** is a state stored as its difference from a base. There are twelve: ten that the player chooses
  (`stateSlot`, the Savestate menu's "Slot"), one for the input recording (`STATE_SLOT_RECORD`, the state that Record
  saves; the chosen slot is not touched) and one for the instant reset (`STATE_SLOT_AUTO`). A slot is a few kilobytes
  when its base is of the same level.

The state is cut into chunks of at most 16 KB (`LZ_BLOCK_MAX`), each in one range; there are 145
(`STATE_CHUNKS`, counted by `tools/mkstate.py`; a `_Static_assert` keeps it within `STATE_CHUNKS_MAX`, 160). A base
is a list of chunks, each compressed by `LzCompress` ([below](#the-compressor)) or, if that does not make it
smaller, stored as it is. A slot is a list of chunks of four kinds, each with a 2-byte header (kind in the top 2
bits, length in the other 14):

| Kind | Meaning | Size |
| --- | --- | --- |
| same | equals the base's chunk | 2 bytes |
| delta | the compressed XOR of the chunk and the base's chunk | 2 bytes and the data |
| plain | compressed by itself (smaller than the delta, or the base is of another level) | 2 bytes and the data |
| raw | stored as it is, from the next word-aligned address after the header | 2 bytes, up to 2 of padding and the chunk |

A base keeps a 64-bit hash of each of its chunks (`baseSums`, from `LzHash`): the plain sum of the words, and a second
word that mixes xor, a rotation and an addition. A save hashes each live chunk and compares: an equal hash gives
"same" with no work on the base. Most chunks of a state are the same as the base's. A save decodes only the base's
chunks that differ. The second word exists because a plain sum, or the sum of running sums, stays equal when one word
goes up by one, the next down by two and the next up by one; such a chunk was stored as "same". A chunk with a
different content and the same hash would still be stored wrong. For two independent 32-bit words that is not
expected in practice. `tests/lzhost.c` tries changes that keep the plain sum, and `tests/slots.py` saves a state
with such a change in a chunk of the base.

A save into slot S, in level L:

1. If a base of level L exists, the slot is stored as its difference from it.
2. Otherwise, if a base is free, the state is compressed as a new base, and the slot is "same" for every chunk (292 bytes):
   the base is this state.
3. Otherwise, or if the pool has no room for a base, the slot is stored as the difference from the base of another
   level that has the most chunks equal to the live state's (`ForeignBase`, by the hashes). Its chunks are mostly
   "plain". It takes most of a base.
4. If the pool has no room, the old state of slot S is freed and the save is tried once more. If it still does not
   fit, the save says "pool full" and slot S is empty; the message says "emptied" when the slot held a state. The
   other slots are not touched. The check for room is made for each chunk by the size that the chunk may take (2
   bytes for "same").
5. The new item is entered in the table before the old one is freed, and bases that no slot uses are freed.

The level start slot (`STATE_SLOT_AUTO`) is saved the quick way when its base is of the same level: each chunk that
differs from the base is stored as it is, decided by the hashes alone, with no decoding of the base and no
compression. It gives up, and the slot is saved the usual way, when more than 20 chunks (320 KB) differ. In Atlantis
1 a second start differs in 18 chunks: the slot takes 240 KB instead of 5 KB, and the save takes 0.2 s instead of 0.4 s.

The player's slots hold states of any level. A load of a slot decodes the base chunk by chunk into the game's
memory and applies the slot's chunks on top. A "same" chunk is left alone when the game's memory in the slot's own
level already has the base's hash: the hash of a chunk costs about half of decoding it, and in the level that runs most
chunks are equal. The song is kept with the slot.

### The compressor

`practice/src/lz.c` (format in `practice/include/lz.h`) is written for this project. It is an LZ with a token byte
for a run of literals and a copy of earlier bytes; matches start on a word and have an offset that is a multiple of
4 (the game's memory is mostly words). A match is found at one of four positions and is copied a word at a time. The offset can
repeat the last one, which costs no bytes. The compressor looks up each word position in a small table (1024
buckets of 2 positions, 16-bit, 4 KB), checks the last offset first, and extends a match back as far as it goes. It
skips faster over data that does not match. The positions inside a match are not added to the table. Tried and
dropped: adding them (a fifth of the time, 0.3 percent in size), a table of one position (15 percent faster, 4 percent
larger), matches of at least 8 bytes (7 percent faster, 18 percent larger).

On a state of Atlantis 1 (game frame 300; `tests/slots.py` prints the same kind of numbers):

| | Size |
| --- | --- |
| the state | 1,965,652 bytes |
| a base | 633,016 bytes (3.11 times smaller). Measured on states of five levels with the same code on the host: Atlantis 1 3.12, level 20 3.03, level 30 3.01, level 35 3.02, level 1 2.77. A zlib level 1 gives 506 KB on Atlantis 1; xz at its highest, 333 KB |
| a slot, same level, 1 to 10 game frames later | about 300 bytes, 9 KB, 10 KB |
| a slot, same level, 700 and 1200 game frames later | 6 to 15 KB |
| a slot, another level's base | 400 to 650 KB (449 KB for a state of level 35 against the base of level 1) |

Three levels' bases and ten slots take 1.97 MB of the 2.44 MB pool (`tests/slots.py`: 469 KB free). A fourth base does
not fit, and `STATE_BASES` is 3. A state in a fourth level is stored as its difference from another level's base, which
takes most of what is left. The level start slot in quick form adds up to 320 KB. The menu shows
the used slots and the free kilobytes. The bytes of a state that do not
compress (textures of 16 bits, vertex data, display lists) set the limit; the order-0 entropy of the literals is only
6 percent below their size, and the cost is in 98,000 short matches.

Times in Atlantis 1, as the emulator's CPU counter measures them (mupen64plus; the counter runs at 46.875 MHz as on
the console, and the emulator does not model waits for memory. The console may take longer). `tests/slots.py` and
`tests/instantreset.py` print them.

| | CPU counter ticks | Time |
| --- | --- | --- |
| save, first in the level (makes the base) | 67.5 to 70.3 million | 1.44 to 1.50 s |
| save, later in the level | 17.3 million | 0.37 s |
| save, the level start slot, first start of the level (makes the base) | 67.5 million | 1.44 s |
| save, the level start slot, a later start (quick) | 9.3 million | 0.20 s |
| load, a slot | 19.3 to 22.2 million | 0.41 to 0.47 s |
| load, the level start state (the instant reset) | 11.5 to 11.7 million (10.0 for a later start) | 0.25 s (0.21 s) |

The load of the level start state took 17 million ticks (0.37 s) before the loads skipped the chunks that the game
already holds, and the save of a second start 18 million (0.38 s) before the quick form. The checks that
`LzDecompress` makes against a damaged stream made a decode about 12 percent slower.

A save, or a load, stops the game's threads for that time (the sound goes on, and the frame messages that came
meanwhile are dropped, below). The game frame before shows "saving..." or "loading..." (`Announce` in `actions.c`: the
request waits one message of the main queue, and the state is of the next game frame). Hold gives up when the draw
or controller thread has not stopped after 3 s, and the save or load says "game busy". A save also resets the HUD's
lag counter, as a load does. Nothing is lost.

The base of a level is decoded again by every save (only its chunks that differ) and every load (all of them, except
the chunks skipped as above), at about 8.9 counter ticks a byte. A faster decoder is the first thing to try if the
load time matters more.

`tests/lzhost.py` compiles `lz.c` for the host with the address sanitizer. It round-trips made-up data of seven kinds
and ten sizes, checks that a limit at the exact size of the output is enough, feeds the decoder streams that ask
for literals or a match past the block's end or an offset before its start, and cut and damaged streams, and checks
`LzHash`. In the emulator, `stateVerify` (set by `tests/slots.py` and `tests/instantreset.py`) decodes each saved
state again before the threads run, whatever the kind of each chunk, and compares it with the game's memory;
`stateVerifyBad` counts the words that differ. A few words in the last range of the game's bss (the audio code's
buffers, 0x80296C38 to 0x802AB010) change while a save runs. `stateVerifyOutside` counts the differing words outside
that range, and the tests require it to stay 0.

### The instant reset

The setting is `settings.instantReset` (Warp menu, last item, "Reset": load the level / instant). While it is on,
`StateGameFrame` (from `ActionsGameFrame`) saves the state into the slot `STATE_SLOT_AUTO` on the first game frame of
each level: the game is in a level (mode 4, not a title), `frameCount2` is 0, and `practice.levelStarts` has not been
seen before. `RunGame` calls the hook for every message of the main queue. The test on `frameCount2` and the count
make it happen once. The first time a level is entered this makes the level's base (the state itself), and the slot
is "same" (292 bytes); a later visit adds only the difference from the base, in the quick form (above). With the
setting off the hook returns after one test, and nothing is saved at a level start.

The slot records the level start that it holds (`Slot.starts`, the value of `practice.levelStarts`). A save that was
refused (the EEPROM was busy, or the other threads did not stop) clears the slot and is tried again on the next message
of the same game frame. A save that failed because the pool is full clears the slot and is not tried again for this
start. Either way an older start is never left in the slot.

Reset (the action; the bindings and the menu items "Reset level") loads the slot when all of these hold: the game is in
a level (mode 4, not a cameo, which has the level's number), the game is not paused, Glover is not dying
(`handControl.deathTimer` is 0), no exit is under way, no EEPROM request is waiting, and the slot holds the state of
this level start (`starts` equals `practice.levelStarts`). Otherwise it does what it did:
`ExitLevel(EXIT_TIMETRIAL_RESTART)` and the level loads again, which saves a new state at the new start.
`tests/instantreset.py` covers a pause, a death and a start whose state was not saved. A cameo is not tested.

What the load does that a plain load does not:

- The game's RAM copy of the save (`saveSlot`, `bestTimes`, `hiscoreTable`, `eepromCRCS`) is kept as it was just before
  the load, and is put back after it. Otherwise a new record in the game's table, or a level marked as done in the
  copy, would be lost, and the game's next save would write the old copy to the cartridge.
- It counts as a level start for the practice code: `practice.levelStarts` goes up, `HudLevelStart` and `BestTimesLevelStart`
  run, and the practice best time is not tainted (a plain load taints it). It adds no "state loaded" message; it says "level reset (instant)".
- The music does not restart. A load leaves `audioCtrl`, the game's handle to the playing song, as it is, and the
  reset's state has the song of the level, which is the one that plays. The song goes on from where it is, and
  `PrepareSong` is not called. `MusStop` stops the sound effects, as after any load. The normal reset goes through the
  game's level start, which prepares the song again; the instant reset does not.
- It is silent where a real start has ambient effects. `MusStop` stops the sound effects, and the state does not run
  the level's start again. A sound that a real start begins is not begun. The sound effects that played stay
  stopped until the level starts them again. The owner decides if this needs work ([decisions.md](decisions.md)).
  Which levels are affected has not been listed.
- The state is of the first game frame. Lives, health and the garib counts are those of the start. The normal reset keeps
  what the game keeps over a Time Trial restart.

The setting is saved with the other settings. The EEPROM block (64 bytes at blocks 56 to 63) has no free byte and no free flag
bit, but `hudOn`, a 16-bit field that holds one bit for each of 13 HUD slots (11 are used), has three
unused bits at the top. (The unused bytes of slots 11 and 12 hold the menu look, see "The menu pages and the menu look".) Bit 15 holds `instantReset`. Settings saved before have the bit clear, which is the default.
The version number stays 5 and the old blocks load. A new block read by an older ROM ignores the bit. The Controller Pak
note holds the whole `Settings` structure (one byte at 0x52, which was padding), and its loader masks the new byte
to one bit like the other flags.

A save or a load runs on the game thread at the start of a game frame (`ActionsGameFrame`, from
`PracticeClearText`), one message after the request. It then:

1. raises the game thread to priority 109 (above the draw thread, 20, and the scheduler's graphics thread, 100;
   below its audio thread, 110) and puts the controller thread (0x73) at priority 1;
2. checks that the draw thread waits between frames (blocked on `gfx_msgQ`, which it waits on only at the top
   of its loop), that the controller thread waits for its next frame (not on `controllerMsgQ`, in the middle
   of its read), and that no graphics task is pending (`gfxTasks`: the RSP and RDP read textures and vertices
   from the heap). If not, it puts both priorities back, lets those threads finish, and tries again, for 3 s at
   most. The test comes after the change: a frame message could wake a thread between a test and a change;
3. compresses or decompresses. The scheduler (120), its audio thread and the music driver (140) keep running: they keep the OS's
   timing and the sound, and use none of the state's memory. Interrupts stay on;
4. after a load, empties the held frame and writes the data cache back (`osWritebackDCacheAll`);
5. drops the frame messages that came during the copy, from `gfx_msgQ`, `main_msgQ` and the controller
   thread's own queue (the scheduler's client whose queue is on that thread's stack). On the console the copy
   takes several frames. Without this, the draw thread would build those frames back to back, over buffers that
   the RSP still reads, and the controller thread would read several times at once. The game, the draw thread
   and the controller thread go on from the next frame message, after a save as after a load;
6. puts the priorities back.

After a load, the sound effects stop: the music driver is not part of the state.

The state holds the game's RAM copy of the save: `saveSlot`, `bestTimes`, `hiscoreTable` and `eepromCRCS`. A load
puts the copy of the moment of the save back, and the game's next save writes it to the cartridge. A record or a
file change made after the state was taken is lost at that save. The save file editor ([below](#the-save-file-editor))
keeps its own copy and merges only the bytes that it changed.

The draw thread always holds one frame that it has built and not yet sent: it builds a frame's display list
during one frame and sends it at the start of the next (`SendLastFrame`). Two things follow for a load:

- The held frame was built from the old state, and its display list points into the old heap. After a load
  from another level, or after the level was loaded again, that heap holds other data, and the frame stops the
  RDP. So a load empties each task of the held frame: its list becomes a full sync and the end, as each of the
  game's task lists ends. The scheduler still shows that frame buffer, which holds its picture from two frames
  before. Drawing goes on as before.
- `currentTask` is in the game's bss, but it counts the tasks of the frame that the draw thread holds now. The
  copy keeps its live value. Restored, it made `SendLastFrame` send old task entries, and the graphics tasks
  never ended.

A load in the same level, without a reload in between, hid both problems: its old display list still pointed
at the same data.

The game's drawing code calls `Random`. The order of drawing and game frames decides the random numbers. A
first fix stopped the drawing around a load with the game's `StopDrawing`; that changed this order, and a replay
from a state no longer matched the first run. Emptying the held frame keeps the order: a save and a load leave
the threads in the same state.

A save or a load is refused, with a message on the screen, while the game has an EEPROM request waiting: the
request is in the game's bss, and the controller thread would act on one that the copy has taken back. A load in another level gives that level back; when
the song of the save differs from the one playing, the load starts it with the game's `PrepareSong`.

## Savestates on the SD card

`practice/src/fat.c` (a small file system), `sd.c` (the cart drivers), `card.c` (the card files and the menu's
requests) and the export and import functions in `savestate.c`. The Savestate menu has seven items at its end: "SD
card" (the status), "Check card", "Card slot" (1 to 100), "Card file" (what the chosen file holds), "Save slot to
card", "Load from card" and "Delete on card".

- **Where the files are.** The folder `GLOVERPR` on the first FAT16 or FAT32 volume of the card (a partition table
  is read, a card with none works too), and in it `STATE001.GST` to `STATE100.GST`. Short names only. exFAT, FAT12,
  GPT and sector sizes other than 512 are not read: the status says "not FAT16 or FAT32".
- **What a file holds.** One state with no dependence on a base. A card save takes the pool slot that the Slot
  choice names and writes, for each chunk of the state, the record that a base would hold: the compressed chunk, or
  the chunk itself if it does not compress. A chunk of the slot that equals its base's is that base's record. A
  chunk that the slot keeps as a difference from the base is decoded, XORed and compressed again. The same
  compressor (lz.h) is used. A file is 0.63 MB whatever the slot is, about 3.1 times smaller than the 1.9 MB state.
- **The format** (all numbers big-endian):

  | Offset | Size | Content |
  | --- | --- | --- |
  | 0 | 4 | "GPSS" |
  | 4 | 2 | version, 2 (the byte at 5) |
  | 6 | 2 | header size, 64 (the byte at 7) |
  | 8 | 1 | level of the state |
  | 10 | 2 | number of chunks (145 in this build) |
  | 12 | 4 | song (signed) |
  | 16 | 4 | bytes of the state (0x1DFE54) |
  | 20 | 4 | layout hash: FNV-1a over the start and end of each range of `build/state_ranges.h`, then `LZ_FORMAT` (lz.h), `LZ_BLOCK_MAX` and `CARD_RECORD_FORMAT` (savestate.c) |
  | 24 | 40 | zeros |
  | 64 | | the chunks, in the order of the ranges: a 2-byte header (the kind in the top 2 bits: 1 compressed, 3 raw; the length in the other 14 bits for kind 1), the data. A raw chunk's data starts at the next multiple of 4 of the file's offset |
  | size - 4 | 4 | CRC-32 (the zlib one) of every byte before it |

  A load refuses a file whose magic, version, header size, chunk count, state size or layout hash differ from this
  ROM's, whose level is out of range or whose CRC does not match. It reads the header first and refuses a file from
  another build at once. It then reads the whole file for the CRC, and a third time to decode it. The game's memory
  changes only after the CRC matched. Raise `VERSION` (card.c) when the format changes, `LZ_FORMAT` when the
  compressed stream changes, and `CARD_RECORD_FORMAT` when the records change. `tests/sdutil.py` decodes the format on
  its own (header, CRC, records, the compressed blocks of lz.h).
- **The recording file and the best times file.** `RECORD.GRC` is a state file (the format above, same version and layout
  hash) of the pool slot `STATE_SLOT_RECORD`, with the recording's runs after the chunks. The header's bytes 24 to 27 hold
  the recording's game frames and bytes 28 to 31 the number of runs; a state file has zeros there. A run is the 5 bytes of
  the Controller Pak's note (`RecordingRunAt`: buttons, stick x, stick y, the number of game frames). The CRC covers the
  runs too. A load reads the state as for a card file, then the runs, and checks that they add up to the header's frames.
  The game then holds the recording's state, and `StateSaveSlot(STATE_SLOT_RECORD)` copies it into the recording's slot
  (the ghosts are cleared). A save needs a recording and its slot's state. `BESTS.GRB` is 72 bytes: "GPBT", the version
  (1), the 30 practice best times as big-endian u16 from byte 8, and the CRC-32 of the 68 bytes before it. A load refuses
  a file of another size, magic, version or CRC and leaves the times as they are. Both files are in the folder `GLOVERPR`.
  `tests/sdrecording.py` saves them (and decodes them with `tests/sdutil.py`), `tests/sdrecordingload.py` loads them in
  another emulator run and finds the same game frames in the playback.
- **The streams.** A save and a load never hold a whole file in memory. A 20 KB stage at 0x806F8000 (`CARD_STAGE`,
  behind the two 16 KB work blocks) holds the records on their way to or from the card. The card code's own data
  (the `Fat` with its 8 KB FAT cache, the CRC table and a sector buffer, 10.1 KB) lives in 12 KB behind the stage
  (`CARD_WORK`, 0x806FD000), which ends where the ghosts begin. The Controller Pak's note buffer (31 KB) uses the same 32 KB (`cpak.c`): the menu stays in the Controller Pak view while a pak operation runs, so the two never run together. A `_Static_assert` checks that the area ends at
  0x80700000. The export fills it chunk by
  chunk and hands whole sectors to the file system. The import keeps at least one record in it. A RAW record's data is
  aligned by the file's offset, and the stage keeps its addresses equal to the file's offsets modulo 4.
- **Threads.** The menu only sets `cardWanted`. The game thread acts on it at the start of its next game frame
  (`ActionsGameFrame`), or at once while the menu holds the game (`PracticeClearText`) or frame advance has stopped
  it. A save and a load first show "saving to card..." or "loading from card..." for a game frame, then stop the game's
  other threads as a state save does (`StateHold`), and close the menu. Check and Delete run with the menu open. While
  a save or a load runs, the menu cannot clear a slot (`held` in savestate.c).
- **The progress bar.** The draw thread is stopped during a save and a load. The HUD cannot show progress then. The
  card code writes a bar of 240 by 8 pixels straight into the frame buffer that the video interface shows
  (`DrawProgress`, near the bottom edge). The game's next frame covers it. The message afterwards gives the size and the
  time: "saved to card 1: 619 KB, 1.34 s". `cardLastTicks` and `cardLastWait` hold the total and the part spent
  waiting for the cart.
- **The file system** (`fat.c`, `fat.h`, no dependence on the game). It mounts FAT16 and FAT32, finds and makes
  folders, and finds, writes, reads and deletes files in 8.3 names. A file is written to new clusters and the
  directory entry changes last; a replaced file's old clusters are freed after that. A stop in between leaves the old
  file whole and some unowned clusters (`tests/fathost.c` stops the device at every write of a replacement, and
  fails each single write once). Once the directory entry is written, the new clusters are never freed: a failure
  after that point leaks clusters at worst. A replacement needs room for the new file while the old one stands.
  The FAT is cached 16 sectors at a time, which is one SummerCart64 command. A new directory cluster is zeroed
  through the card stage in commands of 16 sectors. On a FAT32 volume whose FAT is not mirrored (ExtFlags bit 7)
  only the active FAT is read and written. A directory walk stops on a chain that loops, and a folder entry with
  cluster 0 is called damaged. A new
  file has the date 1 January 2025 (the console has no clock). The FAT32 FSInfo block gets "free count unknown" after
  each change. No long names are made or read. The free space is not counted: a save that finds no free cluster
  says "the card is full" and frees what it wrote.
- **SummerCart64** (`sd.c`). Written from the cart's public documentation (docs/00_quick_startup_guide.md,
  01_memory_map.md, 02_n64_commands.md, 03_usb_interface.md and 04_config_options.md in the SummerCart64 repository,
  read as raw files; no source code of the cart or of any program for it was read). The registers are on the cart bus
  at 0x1FFF0000 and start locked. Detection writes the KEY register (0, then 0x5F554E4C, then 0x4F434B5F) and reads
  IDENTIFIER, which must be "SCv2". A cart that does not answer is left alone: no command follows. The commands used
  are WRITEBACK_PENDING, SD_CARD_OP (status, init, deinit, byte swap on and off), SD_SECTOR_SET, SD_READ and
  SD_WRITE, each through the cart's 8 KB data buffer (at most 16 sectors a command). Every command holds libultra's PI
  lock (`__osPiGetAccess`), which the game's own cartridge DMAs take. The open sequence:
  1. WRITEBACK_PENDING. The cart writes an EEPROM save back to the card by itself, and the card cannot be shared
     (03_usb_interface.md, SD_CARD_OP). A nonzero answer ends the open with "save writeback pending, try again".
  2. SD_CARD_OP status (a card must be inserted), init, status again. If the byte swap is on, it is turned off (a
     sector then arrives as the bytes of the card), and turned on again at close.
  3. A test read. The data buffer is filled with a pattern, sector 0 is read, and the buffer must have changed. If not,
     the other buffer address is tried, and if that fails too, the open ends with "card read failed". Nothing is
     written to the card before this has passed, in any session.
  4. A failure after init sends the byte swap back and deinit before the registers are locked.

  The address argument of SD_READ and SD_WRITE. The N64 command table (02) calls it `pi_address`, and the memory map
  (01) puts the data buffer at 0x1FFE0000 on the PI bus and at 0x05000000 in the cart's own map. The USB table (03) calls
  the same argument `address` and gives no map, but that page serves the PC side and the cart's own map is the one it
  uses everywhere. The console's command is the one in the table of 02. So the driver gives 0x1FFE0000 first, and
  the test read of step 3 falls back to 0x05000000 and keeps the one that works. The wrong address should do no harm:
  0x1FFE0000 is outside the cart's own map (which is 128 MiB), and 0x05000000 is in the 64DD register range of the PI
  bus. This is a reading of the documents and is not tested.

  After a timeout the next command waits for the busy bit to clear (with its own timeout) before it writes any
  register. The registers are locked again after the operation, unless a command is still busy (they then stay
  open: a lock would cut the command off). After the operation the card is deinitialised.
- **EverDrive 64.** There is no driver. The pages of krikzz.com/pub/support/everdrive-64 hold the user manuals of the
  X-series and the Pro series (they only point to a source repository on github.com, which the build proxy blocks),
  the 2015 USB tools (code) and sample code. None of them documents the registers. A driver written from memory
  would be a guess, so none was written (docs/decisions.md).
- **The test mailbox and the test build.** The mailbox is not in the ROM that players get. `make testrom` builds
  `build/test/glover-practice-test.z64` (and its ELF): the same objects, with `sd.c` compiled with
  `PRACTICE_TEST_MAILBOX`. A test module with `TEST_BUILD = True` (`sdcard`, `sdcardload`, `sdcardfull`, `sdcardbig`,
  `demo_sdcard`) runs on it, and `make test` builds it first. In that build `sdBackend` is 0 at boot. A test sets it to
  1, and `SdOpen` then asks the "mailbox" for sectors: a block in the practice bss (`sdMailbox`, 16 sectors, symbol
  `sdMailbox`) with a command, a sector, a count and a status. The ROM writes the command last and waits for it to go
  to 0. `tests/sdutil.py` answers from a disk image in a thread of its own (the emulator calls the harness's frame
  callback only when the picture changes, and a save stops the picture). With `sdBackend` 0 the test build drives the
  SummerCart64 driver, and finds no cart. The driver itself runs on no test.
- **Faithfulness.** Nothing touches the cart at boot or while the card is not used: no register is read or written
  until the player presses Check card, Save, Load or Delete. Every game frame costs one test of one byte
  (`CardPending`). Check and Load never write to the card. Only Save and Delete write, and only for an item the player
  chose. Saving makes the folder if it is not there.
- **Space.** The practice segment grew by about 19 KB of code (the file system 7.7 KB, the card code 6.7 KB, the
  driver 2 KB, the export and import 2 KB) and 1 KB of bss. The file system's state, the CRC table and a sector
  buffer are in the spare memory behind the stage (see "The streams"). `make` prints the practice RAM area's use next
  to the ROM's: code and data 0x27AC0, bss 0x54D50, together 0x7C810 of 0x80000 bytes (97%), **14.0 KB left**
  (`tools/checklayout.py`; the test build has 5.1 KB left, as the mailbox takes 8 KB of bss).
- **Times.** In the emulator (the mailbox's thread answers, so the figures depend on the host) a save takes 1.5 s
  and a load 3.0 to 3.5 s, and about half of that is waiting for the mailbox. The console's own work is about 0.7 s
  for a save and 1.4 to 1.7 s for a load (`cardLastTicks` less `cardLastWait`: the CRC pass, the decoding of the chunks
  and the copies of the records). The card adds its own time for 0.63 MB, once for a save and three times for a load
  (not measured; the header read is one sector).
- **The first save on a large, populated card** (`tests/sdcardbig.py`, and `fathost.py` for the commands). The image is
  a 2200 MB FAT32 volume with 4 KB clusters, 2000 files with long names and 12 folders of 100 files, and a file of
  1.2 GB in front (300,000 clusters in use). The save made the folder and a file of 619 KB behind all of it. It asked for
  2767 sectors to be read in 532 commands and 1315 sectors to be written in 198 commands. In the emulator it took 5.5 s,
  of which 3.2 s were the mailbox and 2.2 s the console's own work, 1.5 s more than on a small card. The extra is the
  walk over the FAT (the scan for a free cluster starts at cluster 2 when the FSInfo block has no hint) and over the
  root directory, which is read a sector at a time (about 250 of the commands). With the FAT cached 4 sectors at a
  time the same save needed 980 read commands. A card file written after the first save starts the search at the hint
  and does not repeat the walk.
- **Not tested on a console.** The SummerCart64 driver: the register protocol, the data buffer and the meaning of
  the address in the SD commands (the table calls it a PI address; this code gives 0x1FFE0000 first and tries
  0x05000000 if a test read leaves the buffer as it was), the byte swap setting, the write-back check, and the real
  card's times. The CPU reads the data buffer word by word over the PI, which should take a few milliseconds for 8 KB.
  Nothing was tried on a real FAT32 card by the practice code (the file system is checked with `fsck.fat` on images
  that mkfs.fat and a formatter of our own made).
- **Not built.** Stronger compression (docs/decisions.md has the measurements), exFAT, GPT, long names, a free-space
  figure, an EverDrive driver, loading a card file into a pool slot.

## Input recordings

A recording starts with a savestate: Record saves into `STATE_SLOT_RECORD`, a pool slot of its own, and does not touch the slot that the player chose. Play loads that state. When the save does not fit, Record says "recording not started: pool full" and does not start. From the next game frame, `RecordingFrame` (in the controller thread,
after the practice input layer and before the game's `ReadController`) keeps the buttons and the stick that the
game is about to read, 4 bytes a game frame, from 0x80780000 (up to 131072 game frames at 20 a second, 109 minutes). Playback loads the state and writes each kept
input into `controllerdata` at the same point. The game's `pad[]` is in the state. The game computes the same
presses from the same reads.

The controller thread reads once a game frame message, also while the game thread waits (frame advance, slow
motion, the practice menu). Those reads do not match game frames. A recording or a playback stops when one
of these is on.

A recording also keeps the glove's and the ball's places for the ghost ([The ghost](#the-ghost)).

## The collision view

`CollViewDraw` (`practice/src/collview.c`) runs on the draw thread in `PracticePrintTimers`, at the end of the
frame's display list, while the view is on. It reads the game's memory and appends one `G_DL` to the game's list.

- It draws only on frames of a level in which the game drew the world (`gfxIsDrawing`, no backdrop-only frame,
  the screen not faded out): only then is the frame's camera matrix (`dynamicp + 0x4080`, from `SetupViewing`)
  this frame's. Glover must be less than 30,000 units from the origin on each axis: a matrix is 16.16 fixed point
  (`guMtxF2L`), and the conversion of a larger value traps.
- The draw thread runs above the game thread and can stop it in the middle of a game frame. Every pointer is
  checked before it is followed, every list walk is bounded, and every float is checked as bits before the FPU
  sees it (`practice/include/safe.h`): the game's threads trap on NaN and denormal operands. The actor and
  memory views use the same checks.

### The cache

The faces are those of `CheckCollisions` (collision.c): the landscape actors in `collisionList`, and the platforms
in `platformList` without flag 0x84 and with faces. The game places face i of an actor as the corners of mesh face
i, each scaled by `Actor.scale`, turned by the matrix of `QuaternionToMatrix` and moved to `Actor.pos`
(`InitPlanes` for the landscape, `RecalculatePlatformInfo` for a platform). This was checked in 26 levels: for every
landscape actor, and every platform whose faces the game had brought up to date on that frame, the mesh placed this
way is the game's `CollFace` corners within 0.0002 units. So the view keeps each mesh once, in its own space, and
draws it with a matrix made the same way each frame. A platform moves with no work for its faces, and is drawn
where it is, also far from Glover, where the game's own faces are out of date.

- A record is one actor: its shape, its terrain row, and for a landscape actor its matrix, made once
  (`staticMtx`). A platform's matrix is made each frame (`movingMtx`, one set for each frame buffer). A hash table
  finds an actor's record by its address. Each frame the view walks the game's two lists and looks up each actor.
  An actor with no record, or whose mesh changed, gets a build.
- A shape is one mesh as some actors use it. Actors share a shape when they have the same mesh, the same scale
  and the same kind of each face (the kind depends on the actor's rotation). The 26 levels have at most 116 meshes
  and 2,476 faces; a shape is about one mesh.
- A shape holds the mesh's vertices as s16, times 2^shift (the most precision that fits; the matrix divides it
  out), and one display list for each kind of face. A list is batches of up to 32 vertices (F3DEX 1.21's vertex
  cache) and the triangles that use them, two to a `G_TRI2`. A mesh vertex is loaded once in a batch. The levels
  need about 1.2 vertices a face.
- The kinds: a flat floor (normal y above 1 - 0.25 / `handSlideScale` of the terrain row: 0.75 for most rows, the
  game's `CheckPlaneForSliding` and the onGround test of `UpdateActor`), a steep floor (above 0.3: the game lets
  such a face carry Glover, collision.c), a wall (-0.3 to 0.3) and a ceiling (below -0.3; the game has no such
  threshold, it mirrors the floor's). On low gravity rows there is no steep floor. A degenerate face is left out.
  A platform that turns keeps the kinds of the rotation it had when its shape was built.
- The build runs as a job, one mesh at a time and 16 faces at a time, for at most 1.5 ms of CPU counts on each
  frame (`BUILD_COUNTS`), over as many frames as it needs: first the kind of each face, then a shape to share or
  the new shape's lists, one kind after another. Nothing of the cache is drawn until every actor has its shape
  (`CollStats.state`, the menu shows "building"). An actor that comes later is added the same way while the rest
  is drawn.
- While a shape is built, its scratch (the batch slot of each mesh vertex, and for open edges the hash table of
  the mesh's edges) is in the unused end of `cacheVtx`.
- The cache is built again after `CollViewReset` (a savestate load, a reset of the level and a warp call it), a
  level change, or a frame count that went back. It is cleared only once no frame in flight draws it: a frame's
  list is sent at the start of the next frame's build and has run by the end of the one after
  (`CACHE_WAIT_FRAMES`, 3 frames after the last one that drew the cache). What changes each frame (the scene
  list, the platform and sphere matrices, the touched faces, the holes, the projection, the viewport) has one set
  for each frame buffer. A build only appends to the cache, which frames in flight do not read past.
- Memory, in the practice segment's bss: 2,688 vertices (42 KB) and 1,792 commands (14 KB) for the cache, 96
  landscape matrices, 128 shapes and 160 records; for each frame buffer a scene list of 896 commands, an extra list
  of 232, 64 platform matrices, 24 sphere matrices and 40 hole posts (`HOLE_DRAW_MAX`; the painter itself holds
  `HOLE_MAX`, 64). `tests/perf.py --survey` found the highest use in the 42 levels with the whole-level view and
  every switch on: 2,493 vertices, 1,541 commands, 108 shapes (Space 2A), 126 records, 546 scene commands, 17 spheres
  and 9 holes at the start of a level. Atlantis Bonus uses all 64 platform matrices and sets `full` bit 2, so 64
  is too few there. The caps of vertices, shapes, records, scene and platform matrices stay as they are. One byte for each face of the mesh being built
  (`faceBytes`) holds a face build's groups or an edge build's open edges: only one build runs. Face info and the
  wireframe took the room for their code from the cache's commands (2,048 before; Atlantis 3's faces need 1,193)
  and from the sphere matrices (40 before). The faces alone of the largest level (Atlantis 3) take
  2,297 vertices and 1,193 commands. The old scan's buffers are gone.

### Each frame

- The scene list: for each actor in reach and not filtered out, a matrix load, then for each kind of its faces
  a primitive colour (when it changes) and a `G_DL` to the shape's list. Vertices are white; the colour and alpha
  come from the primitive colour, times the environment alpha (the combiner). The primitive colour needs no pipe
  sync.
- Radius (`Run.collRadius`): the whole level (0), or 1000, 500 or 250 units. An actor is in reach when its
  bounding sphere (`Actor.pos`, `Actor.radius`) comes within the radius of Glover. The landscape's radii go up to
  3,922 units, so faces far from Glover can be drawn.
- Face filter (`Run.collFaces`, one bit each, `collview.h`): floors (flat and steep), walls and ceilings leave out
  a shape's lists; water (row 12), slippery (slip above 1), low gravity (gravity scale below 1), landscape and
  platforms leave out the actor.
- Colours (`Run.collColour`): by terrain (water, slippery, low gravity and sticky rows in their colour, others by
  kind with the steep floor yellow-green), by kind (floor, wall, ceiling), or one of 12 colours for each record.
- Back faces (`Run.collBack`). The solid side is the one that the normal points to, and the game's normal is the
  cross product of the edges from corner 0: the RSP sees it as the front. "faint" draws the scene with
  `G_CULL_BACK`, then again with `G_CULL_FRONT` and the environment alpha at 0x6B (about 0x28 for a face);
  "hide" draws it once with `G_CULL_BACK`; "same" once with no culling.
- The extra list sets the state (the frame's last RSP task starts from the microcode's first state: viewport,
  the game's projection from `guPerspective` with the game's values times the frame's camera matrix, geometry
  mode, render mode, combiner), calls the scene, then draws the spheres, the touched faces and the holes.
  "See-through" uses the game's translucent surface; "on surfaces" adds Z compare without Z update and pulls the
  faces 2 units towards the camera in the viewport.
- Set a render mode with `G_SETOTHERMODE_L`, shift 3 and length 29, as `gDPSetRenderMode` does. With shift 0 and
  length 32, F3DEX 1.21 (run by the emulator's low-level RSP) keeps the old bits and ORs the new ones in: the
  outline mode drew nothing until this was changed. The view, the text (`text.c`) and the ghost (`ghost.c`) now
  set their modes this way.
- A see-through mode that sorts the faces by depth (the view clearing the Z buffer and filling it with the faces
  first, with Z update) was tried. In the emulator, any Z update in the frame's last task broke the practice menu's
  text on that frame and the game's HUD digits on the next ones. It was tried while every render mode was set with
  length 32: the text then kept the view's Z bits, which may be the cause. It was not tried again. Do not write
  the Z buffer from the overlay until it is.
- The faces that the glove and the ball touched on the last game frame (`ActorObject.planes`) are drawn white over
  the cache, from their world-space corners, in half units around Glover (at most 8).
- Collision spheres (`Run.collSpheres`, `Run.collEnemies`): one cached icosahedron, drawn with a matrix for each
  sphere (at most 32, the glove's and the ball's first). An enemy is an actor of type 5 to 58 (not 6) with flag 8
  (the flag `CheckActorCollisions` tests; the scenery fish of Atlantis 1 have none) within twice the radius plus
  its own radius of Glover, or any with the whole level. The game places a sphere with
  `RotateVectorByQuaternion`, which goes through `GetRotationFromQuaternion` (an `acos`) and can give NaN for a
  quaternion that is a little off. The view turns by the unit quaternion directly (`Rotate`) and leaves out a
  sphere whose actor has a quaternion with a squared size outside 0.8 to 1.25. Spheres with flag 0x20 or 0x10
  need no rotation.

### Open edges

An edge is open when only one face of its mesh uses it (`Run.collEdges`). The game's own `CheckForInnerEdges`
marks only edges between coplanar faces. `FindOpenEdges` counts every edge of a mesh once (a hash table of the two
vertex numbers, at least 4 slots a face) when the shape's edges are built. Faces share an edge when they share its
two vertex numbers: a mesh with the same corner twice under two numbers would show false open edges (none in
Atlantis 1). Meshes of more than 512 faces are left out (the largest in the 26 levels has 328).

- Each open edge is a band 3 units wide inside its face, from the edge to the same edge moved in: the inner
  corners are the face's corners pulled towards its incentre, which keeps every band the same width. The band
  turns the same way as its face, so it is culled with it. Its vertices are pink and opaque.
- A shape's open edges are a list of their own, drawn after its faces. The lists are built after the faces of
  every actor, nearest to Glover first, in the build's time. They leave 192 vertices and 96 commands of the cache
  for actors that come later. In the largest levels they do not all fit (`CollStats.full` bit 1): the view has
  room for about 70 to 500 of them. Turning the switch off and on again drops the edge lists and builds them again
  around Glover's new place, if no shape was built after them.

### Wireframe

`Run.collWire`: 0 off, 1 over the faces, 2 alone. The extra list calls the scene once more after the face passes
(or in place of them), with the render mode `0x50006248` (`0x50006A58` on surfaces):

- `ALPHA_CVG_SEL`: the blender's alpha is the pixel's coverage, not the combiner's alpha.
- `AA_EN`: every pixel that a face touches is drawn, not only those whose centre it covers.
- The blend is turned round: memory times the alpha, plus the face's colour times 1 - alpha (`FORCE_BL`,
  `IM_RD`).

A pixel inside a face is fully covered and keeps the frame's colour. A pixel that an edge crosses takes some of the
face's colour. So each face's edges show as lines about a pixel wide, in its colour, with no line microcode, no
vertices and no lists of their own. "Hide" back faces culls the back faces' outlines; "faint" and "same" outline
every face. An open edge's band is outlined too. The scene's edge lists end by setting the faces' combiner, which
the outline pass also uses.

Cost: no memory and a few commands. The RSP and the RDP draw every face of the scene once more; the RDP reads and
writes every pixel of every face (inside ones blend to their own colour). The menu warns about the lag.

### Face info

`Run.collInfo`. `CollInfoUpdate` runs on the draw thread before `CollViewDraw` on frames that show the level (the
same test as the view), and `CollInfoDraw` draws a panel at the top right in the text pass, with a cross at the
screen's centre. It works with the view off. Two faces, in `collInfo` (`CollFaceInfo`, collview.h):

- The glove's: of the faces in `hand.obj->planes`, the one whose normal points most upwards. Its actor is found by
  walking the two lists for the actor whose `faces` array holds it. With no face this frame it stays this frame's
  while `onGround` is set (the game does not test every face on every frame), and is shown in grey as an earlier
  frame's once Glover is in the air. A level change or `CollViewReset` clears it.
- The centre's: a ray from the eye (`actualCamPos`, or the free camera's) along the view. The direction is the third
  column of the frame's camera matrix (`dynamicp + 0x4080`, s15.16), turned round. Each actor's bounding sphere is
  tested first. Each face's plane (`normal`, `d`) is read as bits and tested in a short loop: ahead of the eye and
  nearer than the best so far. Only then are the corners read, and the point is tested against each edge in the
  plane of the two axes along which the normal is shortest. The last walk's face is tried first, so most faces
  fail at once. The ray ends at the far plane. It honours the face filter and "hide" back faces. The face is drawn
  yellow among the touched faces (`MAX_CONTACTS` is 9).
- The walk over the faces takes at most 0.75 ms of each frame (`INFO_COUNTS`, looked at every 16 faces) and goes
  on over the next frames from the actor and face where it stopped, with the ray of its first frame. `collInfo[1]`
  changes only when a walk ends. In the largest levels a walk can take several frames, so the centre's face lags
  a fast turn of the camera.
- The panel shows the kind (the view's thresholds, `KindOf`), the slope (the angle of the normal from straight up),
  "(back)" when the ray meets the back, the terrain row and its type, the distance, platform or landscape, the
  model's name and the face's number in the actor's `faces`.

`CollStats.infoCounts` is the CPU counts of `CollInfoUpdate` in the frame.

### Moon Pound holes

`Run.collHoles` (the last item of the Collision view menu, "Moon Pound holes") needs the view on. Two parts:

- The game thread (`HolesGameFrame` in moonaim.c, called from `PracticeClearText` after `TrainersGameFrame` while
  `run.collView` and `run.collHoles` are set) keeps the Moon Pound aim's face buffer and hole candidates up to date
  around the glove: a build starts when there is none, when the glove is 24 units from the build centre sideways or
  in height, and every 100 game frames. The build uses the aim's job (`JobStart`, `JobRun`) with a budget of 1.5 ms
  (`HOLE_COUNTS`) a game frame; the aim's own steps use 4 ms. The buffer is shared with the aim. A level start or a
  savestate load resets it. When a build ends, `PublishHoles` copies the candidates (up to 64: the vertex plus the
  centroid offset for a centre 1 unit above it) into `holeViews[(holeSeq + 1) & 1]` with the `holeBegin`/`holeSeq`
  protocol of the trainer views.
- The draw thread (`AddHoles` in collview.c) copies the list, checks that no publish began, and writes one post for
  each hole (`holeVtx`, 64 quads, one set for each frame buffer): a camera-facing quad 3 units wide and 24 tall,
  violet (`0xB050FFFF`), made by `MakeBar`, in half units around Glover. `CollStats.holes` counts them.

The candidates lie within 72 units of the build centre, so the holes shown are those within about 50 units of Glover.
Cost: 8 KB of vertices in bss. Measured in the hub (`tests/collholes.py`): the longest game thread
step 103,990 counts (2.2 ms; a call that starts before the budget ends can pass it). A build takes about 11 game frames.

### Cost

With the view off: one byte test a frame, and `CollViewReset` sets a byte on a reset, a load or a warp.

Measured in the emulator in counts of the CPU counter (`CollStats.counts`: the view's work on the draw thread in
one frame), with Glover at the level's start, the whole level, see-through and every other switch off, then on
surfaces with the open edges, the glove's, the ball's and the enemies' spheres:

| Level | Actors | Faces | Counts | With the switches | Open edges that fit |
| --- | --- | --- | --- | --- | --- |
| Hub | 78 | 1,635 | 59,000 to 62,000 | 74,000 | 327 |
| Atlantis 3 | 70 | 1,869 | 100,000 | 139,000 | 134 |
| Carnival 1 | 113 | 2,125 | 144,000 | 181,000 | 221 |
| Carnival 3 | 43 | 1,909 | 63,000 | 87,000 | 233 |
| Pirates 1 | 85 | 1,829 | 134,000 | 169,000 | 416 |
| Pirates 3 | 109 | 1,928 | 147,000 | 182,000 | 217 |
| Fear 1 | 70 | 2,232 | 80,000 | 105,000 | 67 |
| Fear 2 | 113 | 1,867 | 143,000 | 165,000 | 333 |
| Out of this World 1 | 125 | 1,744 | 144,000 | 173,000 | 328 |
| Out of this World 3 | 109 | 2,104 | 150,000 | 182,000 | 511 |

- The work grows with the number of actors (about 1,200 counts each: the lookup, the reach test, a platform's
  matrix), not with the faces. The old scan used up to its 3 ms (141,000 counts) and more for 6 to 50 faces.
- A build takes 5.5 to 6.9 million counts in all, over 70 to 90 frames (2 to 3 seconds); no frame passes about
  180,000 counts while it runs. The emulator's count rate may be faster than a console's.
- The wireframe adds about 100 counts of CPU work (a few commands). Face info, measured the same way in the hub,
  Atlantis 3, Carnival 1 and 3, Pirates 1 and 3, Fear 3, Out of this World 1 and the first space boss: 38,000 to
  47,000 counts a frame (median), at most 66,000. A walk of the ray over the level's faces takes about 4 to 7
  frames there. The emulator counts about 2 counts an instruction in these loops (a test loop of stores: 8 counts
  for 4 instructions).
- The RDP draws every face of the level (up to about 2,200, plus the open edges' bands) as see-through triangles,
  and each face is rasterized once in each mode. Its time cannot be measured in the emulator (angrylion does not
  model time). On a console the view can add lag, most with the whole level: a smaller radius or the face filter
  draws fewer.

## Hiding objects

`Run.hide` holds one bit for each kind of object that the Inspect menu's "Hide objects" page leaves out of the
picture (`practice/include/hide.h`, `practice/src/hide.c`). Several of the game's drawing functions also change the
game's state, so a hidden kind's function always runs:

- `TransformAndDrawObject` places collision spheres and platforms tied to a sub-object, and `DrawActor` steps the
  animations before it.
- `PrintStats` calls `Random`. `DrawFootprints` ages and kills footprints and calls `UpdateEnemyBlastring`.
  `UpdateLightning` counts its timers down, and `DrawLightning` calls `Random`. `ProcessShadows` fades the shadows.

After the call, `Strip` turns the commands that the function wrote to `glistp` and that draw into `G_SPNOOP`:
`G_VTX`, `G_TRI1`, `G_TRI2`, `G_LINE3D`, `G_FILLRECT`, `G_SPRITE2D_DRAW`, and `G_TEXRECT` with its two half
commands. For the 3D objects it also drops `G_DL` and `G_BRANCH_Z` (a model's prebuilt list). Every other command
stays: matrices, render modes, combiners, texture loads and geometry modes. The RDP then ends in the state that the
game expects, and the game's copies of that state (`transparentSurf`, `xluSurf`, `xluFact`, the lighting bit) stay
true. A state command inside a dropped prebuilt list is lost; that can only change how a later object looks.

| Kind | Hook | Which ones |
| --- | --- | --- |
| Landscape, scenery, water, platforms, glove, ball, enemies, pick-ups, debris | `TransformAndDrawObject` in `DrawActor` (object.c) | by `currentDrawActor`: `&hand`; `&ball`, `extraBall[3]`, `plaster[4]`; inside `debris[120]` (0x120 bytes each); then by type: 60 with flag 4 and 63 are scenery, 60 without it is landscape, 62 water, 59 platforms, 61 and 1000 or more (orbiters) pick-ups, 4, 2 (ball shots) and 5 to 58 enemies. `fadeOverlay`, `starOverlay` and the foreground objects (`noClip`) are never hidden |
| Garibs, debris sprites, shots' sprites, sprites of hidden models | `PrintSprite` at its four calls (overlay.c) | a sprite inside `debris[]`; a sprite whose animation's frames are those of `garibAnimation[0..3]` (this also hides the garib that the Locate cheat shows over the glove); a sprite inside a `Bullet` of `bulletList`; a sprite of the model of an actor hidden on this frame (at most 32 a frame, from the model's tree) |
| Sky | `PrintBackdrops` (main.c) | every backdrop |
| HUD | `PrintStats`, inside `PracticePrintStats` | the game's counters. `PrintStats` sets up no state of its own, and `ShowText` after it starts with `spInit`: here `glistp` is put back instead. Texts and the pause screen stay |
| Shadows, footprints and blast rings, trails, lightning | `ProcessShadows`, `DrawFootprints`, `UpdateLightning`, `DrawBlurs` in `CreateObjects` | all of them. `DrawBlurs` calls `blurdl`, which sets state only, and keeps it |

The hooks are in `hooks/object.c.patch` (five calls), `hooks/overlay.c.patch` (the four calls of `PrintSprite`)
and `hooks/main.c.patch` (`PrintBackdrops`). `PracticePrintStats` already took the place of `PrintStats`.

A sprite of the opaque pass whose actor is drawn later, in the translucent pass, is not hidden: the list of a frame's
hidden models starts empty at each frame (`dispFrameCount`). `HideStats` (`hideStats`) counts the actors, sprites and
commands of the last complete frame for the tests.

A run with any kind hidden does not count for a practice best time (`BT_OTHER` in `BestTimesTick`). The game's logic
is the same, but the RDP has less to do, so a frame can lag less. That moves when the drawn frames fall between game
frames, which decides some `Random` calls and animation steps. With every bit off, each hook costs a test of
`run.hide` and the work of the call.

The collision view's "On surfaces" mode compares with the Z buffer. With the landscape hidden, the Z buffer holds no
landscape, and that mode shows the faces that are in front of the objects still drawn.

Cost: with every kind hidden in Atlantis 1, the practice code's work after the calls (17 actors, 14 sprites, 262
commands) took at most 19,060 counts (0.41 ms) of the draw thread a frame (`HideStats.counts`, `tests/hide.py`).
The RDP has less to draw. 2 KB of code and data and 0.2 KB of bss.

The overlay lists (`overlayList`, practice.c) hold only text: the HUD, the menu and the face info. The collision
view and the ghost have lists of their own. The most commands one frame used was 3,811, with the menu on its HUD page
in Out of this World 3 and every HUD item, every collision view switch and face info on (`tests/overlay.py`, which
also visited the menu's other pages and views in Atlantis 3, Carnival 1, Pirates 3 and Out of this World 3). The
level does not change it: the HUD and face info use about 1,900 commands, and a menu page about as many. With every
trainer and the frame log on from the start of the level as well, the menu's busiest pages reached 4,611 commands
(96% of 4,800) in one run and 4,289 (89%) in the test's combined pass. Ten percent over that is more than 4,800, so
the list cannot shrink to 4,096. The test checks that pass separately and requires 150 commands to be left. `OVERLAY_SIZE`
went from 6,000 to 4,800 commands for each of the two lists, which frees 19 KB. The text writers stop at the end of
the list (`Room` in text.c) and `Practice.overlayFull` counts the frames on which one stopped; the test requires 0.

## The fog switch

The Game menu's "Fog" item turns the level's fog off (`Run.fogOff`). Only the drawing code reads `fog.on`
(`SetRenderMode`, `RenderObjects`, the shadows, footprints, lightning and overlays). The practice code sets it to
0 while the draw thread builds a frame:

- `PracticeSendLastFrame` calls `SendLastFrame`, which starts each frame in `DrawGraphics`. If the switch is on
  and `fog.on` is not 0, it keeps `fog.on` and sets it to 0.
- `PracticePrintTimers`, which ends the frame, puts the kept value back.

The draw thread builds the display list in one go between the two (it waits only at the top of its loop, and the
savestate copy waits for it there). The game's logic never reads `fog.on`, and a save or a load of a state never
sees it at 0. With the fog off, `SetRenderMode` uses the render modes of a level without fog, and `RenderObjects`
leaves out the fog colour and the fog position. The fog colour still fills the frame buffer behind the level
(`ClearFrameBuffer`), and the far plane still cuts the view. The test looks for the fog colour command and the
fog geometry mode in the frame's display lists (`tests/gameswitches.py`).

Cost: with the switch off, one byte test in each of the two hooks. The new hook is one more call in the draw
thread's frame.

## The extra controller reads

The Inspect menu's "Show missed taps" (`Run.missedTaps`, `practice/src/missedtaps.c`) shows the taps that start and
end between two reads of the game. With the switch off, the only cost is a byte test in the input layer.

With it on, the controller thread's input layer (`PracticeReadController`) starts a thread of the practice code
(priority one below the controller thread's; `controllerPriority` is read at run time) and a libultra timer. The
library of this game has no `osStopTimer`. The timer fires once, and the thread sets it again after each read for
as long as the switch is on. The interval is 5 ms. Each turn of the thread does one read:

1. It turns interrupts off (`__osDisableInt`). No other thread can run until it turns them on again.
2. It checks four things: the switch is on; the controller thread waits for its next frame message (its state is
   `OS_STATE_WAITING` and its queue is a local of its own, inside `ControllerThreadStack`). It is not in the
   middle of a controller read or of EEPROM work; the last serial request was a read of the buttons
   (`__osContLastCmd` is 1); the serial interface is not busy (`SI_STATUS_REG`); and no serial interrupt is raised
   (`MI_INTR_REG`, bit 1). A raised interrupt belongs to a request of the game that libultra has not handled. If one
   check fails, it skips the turn (`MissedTaps.skipped`).
3. It reads the PIF's answer with `__osSiRawStartDma`, called directly: the same single transfer that
   `osContStartReadData` makes while the last request was a read of the buttons. It polls `SI_STATUS_REG` until the
   transfer ends (at most 10 ms) and then writes to the register, which clears the serial interrupt flag. libultra
   never sees the interrupt. No message goes to `controllerMsgQ`. A stray message there would make the game take
   stale data, and a waiting thread there could take a message that belongs to the controller thread. On a timeout
   the transfer is still running. Its interrupt will reach libultra later and may send such a message. The tool
   turns itself off (`MissedTaps.failed`) and the controller thread shows the HUD message "missed taps: read
   failed, off" at the next game read. Switching it on again clears the flag.
4. `osContGetReadData` turns the answer into a local `ContPad[4]`. The thread takes controller 1's buttons.

The game's own `osContStartReadData` is not used, because it only starts the transfer: its caller waits for the
message. On a console, each read of the PIF's answer makes the PIF poll the controllers again. The emulator
(mupen64plus) does the same. An extra read therefore returns the poll that the previous read triggered, which is up
to one tick (5 ms) old, not a poll that it starts itself.

The thread sets the timer again, or clears `armed`, with interrupts off. The controller thread starts the tool
(`Start`) when the switch is on and the tool is not active; with interrupts off in both places, a quick off and on
cannot leave the switch on with no timer set.

The controller thread calls `MissedTapsRead` at each game read with the raw buttons of controller 1. A sample from
either place (the thread or the game read) counts a rise of each button since the last sample. At the game read,
for each button, the rises since the previous game read minus the press that the game sees (the button is held now
and was not held at the previous read) are the taps that the game missed. Two taps between two reads count as two.
`MissedTaps` holds the counters; the tests read them. The input display (`DrawInput`) draws the buttons of a miss in
orange for 40 game reads and a line "missed taps N" above the row.

Limits:

- Only the 14 buttons of controller 1, not the stick or controller 2.
- The extra reads shift the game's timing a little, and the CPU runs with interrupts off for about as long as one
  of the game's own controller reads, every 5 ms. A read that is still not finished after 10 ms turns the tool
  off (`MissedTaps.failed`).
- The thread skips its turn while the controller thread works.
- This tool has not been run on a console. In the emulator the tool takes nearly every pulse of the test plugin
  before the game does, and the game keeps its pace ([testing.md](testing.md)).

## Chaos inputs

The Game menu's "Chaos inputs" (`Run.chaos`: 0 off, 1 rare, 2 sometimes, 3 often; `practice/src/chaos.c`). It has its
own random generator (xorshift32, seeded from the CPU counter) and never reads or writes the game's `rseed`.

- `ChaosGameFrame` (game thread, from `ActionsGameFrame`) rolls once per game frame (`frameCount2` changed; the
  function is called for every message of the main queue). The chance is 5, 17 or 50 in 1000 for rare, sometimes
  and often. It does nothing unless a level is running at full speed: not paused, not in the menu, no frame
  advance, no playback, no recording being made, no noclip. An event is one of three: an added press of A, B, Z, L, R or a C button (kept
  in `pendingAdd`); the next press is taken away (`dropArmed`); a nudge of the glove, made at once in the game
  thread.
- `ChaosInput` (controller thread, in `Frame` after the buttons are hidden and before `RecordingFrame`) changes
  `controllerdata` only. The bindings and the input display use `input`, the real input. A binding does not fire
  from a chaos press. An added press is skipped if the player holds the button. A taken-away press is added to
  `input.hidden`. The game does not see the button until it is let go. Start and the D-pad are never added, and
  Start is never taken away.
- A nudge of the place moves the glove by up to 6 units sideways and up to 3 units up (the previous place moves
  with it). A nudge of the speed adds up to 2.5 on each axis (upwards only on y) and keeps each component within 60. Both check the glove's pointer and
  floats with `safe.h` first and keep the place within 20,000 units.
- Chaos does nothing while a recording is made or played back. A recording would keep the added and dropped presses,
  but not the nudges of the glove. A playback would differ. The menu hint says "Off while recording".
- `ChaosInput` acts only on controller 1 (`input` is controller 1's).
- The HUD marker is "MODIFIED chaos", joined with the noclip and moon jump words when they are on.

## The free camera

The Game menu's "Free camera" item (`Run.freeCam`: 0 off, n flies 2^n units a controller read;
`practice/src/freecam.c`, `practice/include/freecam.h`).

- **What it overrides.** The draw thread copies the game's camera into `actualCamPos[draw_buffer]` and
  `actualCamTargetPos[draw_buffer]` at the top of a frame, then `SetupViewing` (called several times a frame) builds
  the matrix with `guLookAtHilite` from them. The hook `PracticeguLookAtHilite` replaces the eye, the target and the
  up vector in that one call and passes everything else on. Nothing the game's logic reads is written: not
  `camera`, not `cameraInfo`, not `actualCamPos` (the test checks this). `CalcCameraRot`, which writes `camera.rot`
  for the logic, runs before and still sees the game's camera. The hook is on `guLookAtHilite` because the game's
  `ShadingMode` is 2; the `guLookAt` branch of `SetupViewing` is dead code. It has no hook.
- **The safe point.** The hook runs in the draw thread, inside the call that already builds the matrix. The
  matrix, the display list and every user of it (fog, the highlights, the collision view, the sprites, which
  multiply `cameraView` and the projection, `overlay.c`) agree. With the switch off the hook takes the
  camera's copy with interrupts off and calls `guLookAtHilite`.
- **Moving it.** `FreeCamRead` runs in the controller thread, in `Frame` after the game's read, on every read. The
  game reads the controllers once a game frame (every 5th video frame in mupen64plus). The camera moves at the
  game's pace. The thread runs on while the game thread waits in `ActionsGameFrame` (frame advance) or the menu is
  open. The camera still flies then. Controller 2's buttons and stick are read from `controllerdata[1]`. While the
  camera is on, the game's debug controls on controller 2 get no stick and none of the camera's buttons
  (`FREECAM_BUTTONS`: D-pad, C buttons, R, Z), and a quick-action binding on controller 2 that uses one of those
  buttons does not fire. Bindings on the other buttons (A, B, L, Start) work as usual.
- **The pose.** A position and a unit direction. A turn rotates the direction (about the vertical axis, or in the
  vertical plane, which stops short of straight up and down); the up vector is always (0, 1, 0). The camera is
  placed at `camera.pos`, looking at `cameraInfo.actor.pos`, when the switch goes on, when a level starts
  (`practice.levelStarts` changes), once `frameCount` is above 1 (the game's camera is set by then) and when a state
  is loaded (`stateLoads` changes). The game's values are checked with `safe.h` first. A direction whose flat
  length is below `MIN_FLAT` is turned away from straight up or down, and a move needs a flat length above 0.001.
  The controller thread writes `pos` and `dir` in one piece with interrupts off. The hook copies them the same way,
  and builds the game's own view for that frame if a value is not a normal number.
- **Savestates and recordings.** The state is in the practice segment's bss, which a savestate does not hold. A
  load places the camera again at the loaded game camera. A recording keeps the game's input only.
- **Cost.** With the switch on: a few dozen instructions a controller read. With it off: one byte test in the
  controller thread and one in the hook.
- **Test.** `tests/freecam.py` ([testing.md](testing.md)).

## The ghost

The Game menu's "Ghost" item (`Run.ghost`; `practice/src/ghost.c`, `practice/include/ghost.h`). A ghost is
the glove's and the ball's place on every game frame of an input recording, drawn as a see-through marker.

- **Keeping the places.** `RecordingToggle` calls `GhostRecordStart` once the recording's savestate is saved. From
  then on `GhostGameFrame` (the game thread, from `PracticeClearText`, after `ActionsGameFrame`) keeps one frame for
  each value of `frameCount2` while `recording.recording` is on. `RunGame` calls `ClearText` for every message of the
  main queue. The test is on `frameCount2` (docs/game.md). The frame goes to index `frameCount - start`, where
  `start` is `frameCount` at the savestate; `frameCount` stops while the game is paused. A paused frame writes
  the same index again. With no recording the function reads one byte and returns.
- **A frame** is 16 bytes: the glove's and the ball's `pos` in whole units (`s16`, from `ReadFloat`; a NaN or a
  value beyond 32000 leaves the part out), the glove's `rot.y` as a turn (65536 is a full circle) and a flag byte
  for the parts that were readable. The game's glove model is not drawn: the glove is an icosahedron with a
  pyramid for its nose (forward is `(sin, cos)` of `rot.y`, as `spawn.c` uses it), the ball an icosahedron. Their
  sizes come from the first collision sphere of each actor (12 and 20 units if unreadable).
- **Where.** Four slots of 0x20000 bytes (8192 game frames, 6.8 minutes) at 0x80700000, between the savestate pool
  (ends at 0x806FFFFF) and the recording at 0x80780000 (`_Static_assert`s keep the slots clear of both). Each slot has a
  `GhostSlot` in the practice segment's bss (level, count, start, age). A new recording takes a free slot, or the
  oldest ghost's. The ghost of the same level that it will replace stays until the new one has kept a frame
  (`Finish`). A recording that fails to start does not lose the old ghost. Frames that the sampling did not reach
  are cleared as the next one is kept. Nothing is saved to the EEPROM.
- **Drawing.** `GhostDraw` runs on the draw thread in `PracticePrintTimers`, after the collision view, only when
  `run.ghost` is on. It needs the same conditions as the collision view (a level frame with the world drawn), finds
  the level's slot and takes the frame at `frameCount - start`. If the index is outside the ghost, it draws
  nothing. It builds its own list as the collision view does: the game's projection, the frame's camera matrix
  (`dynamicp + DYNAMIC_CAMERA_VIEW`; the free camera applies to it), one model matrix for each marker, a vertex load
  and ten triangle pairs for each sphere, three more for the nose. The surface is the game's translucent one, with
  no Z test. The ghost shows through walls. The list has 41 commands (a `_Static_assert` keeps it within the 64 that fit). The game's list gets one `G_DL`.
- **In step.** The draw thread runs at the start of a game frame's message, before that message's logic. The
  live glove it sees has the place that the game thread kept at the same `frameCount`. The ghost lies on the
  glove of a replay to within the rounding to whole units (0.4 units on average in `tests/ghost.py`). While the
  live run is ahead or behind the ghost by n game frames, they are n frames apart.
- **Cost.** Off: one byte test in the draw thread's end-of-frame function, and one test in the game thread when
  not recording. Recording: a few dozen instructions and one 16-byte write per game frame. Drawing: two
  `guMtxF2L`, one `guPerspective`, 29 vertices and 46 triangles for the RDP. Not measured on hardware.
- **Limits.** A ghost is lost at power-off. It does not follow a warp or a death. `GhostGameFrame` ends the ghost
  recording, keeping the places so far, when `frameCount` goes back (a restart), the level changes or a state is
  loaded. `GhostRecordStart` runs on the game thread (from `ActionsGameFrame`). A recording that runs past 8192 game frames keeps the first
  8192. Frames missing from the sampling (none seen) are left out of the drawing. The ghost shows only in
  `GAME_MODE_LEVEL`.
- **Test.** `tests/ghost.py` ([testing.md](testing.md)).

## Practice best times

`practice/src/besttimes.c`, `practice/include/besttimes.h`. One best in-game time for each level, in RAM, apart from
the game's save.

- **Data.** `PracticeBests` holds 30 `u16` counts of game frames (0: no time yet), one for each value of
  `levelInfo[].bit` from 0 to 29: five levels in each of six worlds, the order of the game's own table `bestTimes`
  and of the Hi Scores screen. Levels without a time (the hubs, the cave, the logos) have bit 30 and are left out.
  Space Boss 1 and 2 share bit 28, as in the game. The struct is saved to a Controller Pak (see
  [The Controller Pak](#the-controller-pak)) and to the SD card (`BESTS.GRB`, see [Savestates on the SD card](#savestates-on-the-sd-card)). `BestTimesState` (not saved) holds the screen choice, the reasons of the run, and the last result.
- **The level-complete point.** `CheckForLevelCompleted` (game.c) is where the game updates its own records (the
  best time, the level and garib bits) when `gameInfo.exitFlags` bit 1 is set, which `ExitLevel(type, pause, 1)`
  sets for a level finished the normal way. Three calls are hooks: the one in `EndOfLevelScreen`, the one in
  `EndOfWorldScreen` and the one for the exit to a hub in `CheckForExits`. `PracticeCheckForLevelCompleted` calls
  `BestTimesLevelDone`, then the game's function.
  `BestTimesLevelDone` reads `player.levelFrames`, the same count as the HUD's timer (20 game frames a second, five
  hundredths each), and the level `gameInfo.prevLevel`. It writes only `practiceBests` and `bestTimesState`.
  A level that the player quits has no "complete" flag and is not noted. `newLevExitTimer` with an exit type
  ends a level through the same `ExitLevel(..., 1)`, which the tests use.
- **The Hard path.** Hard is the Time Trial. At the end of such a level `CheckForExits` does not show the end of
  level screen and does not call `CheckForLevelCompleted`. It sets `bestTimes` itself and goes to the name screen (a
  record), or starts the Time Trial screen. The first call on each of these two paths (`PrepareSong`,
  `InitOverlayList`) is a hook that calls `BestTimesLevelDone` first. A hub exit has already called it through
  `PracticeCheckForLevelCompleted`, and its level has no time. A Time Trial finish counts as a practice best. It is a
  run of the same level, timed by the same `player.levelFrames`, and the count starts at the same game frame as in a
  normal run of that level (the timer fix, [plan.md](plan.md), "Timers"). A restart, a quit and a time-up have no
  complete flag and are not noted. `tests/besttimes.py` finishes one Hard run that beats the game's record and one
  that does not.
- **A modified run is not kept.** `BestTimesTick` (called from `HudTick`, on every message of a game frame) sets
  reason bits in `bestTimesState.taint` while any of these holds: moon jump or noclip, chaos inputs, memory pokes or
  flips, an actor made or moved (the HUD's "MODIFIED" conditions), a set action, frame advance or slow motion, a
  cheat other than the three that only open ways (level select, open portals, open levels) and Locate garibs
  when it was already on at the level's start (bit 0: `StartNewLevel` sets it on an Easy file and nothing clears it
  until power-off), a frozen random seed, controller 2's debug controls.
  A teleport of Glover, a power-up given from the menu, a savestate load, a memory poke or flip or an edit in the
  memory view (set at the write; "Clear all pokes" does not undo it), and "Random numbers: one call" or "level
  start" set a bit where they happen. `PracticeSeedRandom` (the start of each level) clears the bits. A finished level with a bit set
  gets the result "modified" and keeps nothing. "Go to checkpoint" (the Warp menu's action and a binding) can move
  Glover to a checkpoint that was not reached. It sets a bit when it moves Glover. "Last checkpoint" does what the
  pause menu's choice does. It sets a bit when the pause menu would not offer the choice (a boss, bonus or cameo
  level, Glover or the ball in the air, a death or loss under way; never on Hard). A change of the Lives or the Health number in the Game menu sets a bit
  too. The game sets lives and health only at a new game, a continue and a death. A value set before a level
  starts carries into the level. That start is one that normal play can reach (collected lives, health lost to an
  enemy), and the new level clears the bits.
- **The message.** When the timer or the last level time shows on the HUD, a finished level shows its time, and
  whether it is a new practice best, slower than the best, or not kept. With every HUD item off nothing is drawn.
- **The Hi Scores screen.** `HiscoresRun` draws a world's page with `printHiscores`. `PracticeprintHiscores`
  draws the same page from `practiceBests` with the game's own `NEW_TEXT`: the world's name, the five level names,
  and the time with hundredths ("m:ss.hh", or "-:--.--" for no time) in place of the player's initials. It never
  writes `bestTimes` or `hiscoreTable`. The real save and its EEPROM blocks stay as they are. With no practice time at
  all, or with the setting "game records", it calls `printHiscores`, which gives the retail screen. The score
  table page (the first) stays the game's. The game's own "erase" prompt on that screen still acts on the game's table.
- **The setting.** Settings menu, the two items before "Save file": "Hi Scores screen shows" (practice bests, the
  default, or game records) and "Clear practice best times". The choice is not saved: the saved settings have no free bit.
- **Cost.** Each game frame: about twelve byte tests in `BestTimesTick`. At a level's end: one function.
- **Test.** `tests/besttimes.py` ([testing.md](testing.md)).

## The save file editor

Settings menu, last item "Save file" (`practice/src/saveedit.c`, `practice/include/saveedit.h`). A view with two
pages: the file (slot, lives, crystals won, crystals returned, difficulty, Levels, Write, Reload) and the levels
(30 rows of `levelInfo[].bit`: done, all garibs, best time). The menu item and the page show "Edits your real save!".

- **Edits.** They go to the editor's own copy, `saveEditSlots` and `saveEditBests` (six `SaveSlot`s and thirty
  `BestTime`s). `originalSlots` and `originalBests` hold the copy as of the last refresh and tell which fields the
  edits changed. Opening the editor refreshes every field that is not edited from the game's copy. The game's RAM copy (`saveSlot[]`, `bestTimes[]`) stays as it is until Write. The game's own saves
  (`UpdateSaveSlot` at a level's end and at a new game, the file copy in the title screens) write that copy and its
  CRCs. They never write an edit. Edits that are not written stay while the editor is closed. Limits: lives 3 to
  99 (the game raises a file with fewer lives to 3 when it loads it), crystals returned at most crystals won
  (changing the won count keeps the returned count within it), and the difficulty of the file in play
  (`currentSaveSlot`) cannot change. The page marks that file "(in play)". After the boot it is file 1, and it stays
  the last file the game used. Write also keeps the difficulty of whichever file is in play at that moment.
- **Writing.** A on "Write to the cartridge" only asks. A again sets `saveEdit.request` to 1; any other button
  cancels. A question is refused while a request is pending. `SaveEditService` runs in the controller thread after
  its read, as `SettingsService` does, and waits while the game has an EEPROM request (`eepromMessageQueue[0]`,
  `eepromMessageNum`). With interrupts off it checks that again, merges the edits into the
  game's copy (by field; the levels and all-garibs words by bit; a field that only the game changed stays; the
  crystals are clamped to 0 to 7 won and returned at most won), and posts the game's own requests with `PostEepromMessage`:
  6 (`EepromSaveSlots`, blocks 31 to 51), 4 (`EepromSaveBestTimes`, blocks 7 to 30), 1 (the hiscore table, blocks 1 to 6) and 11 (`EepromSaveCRC`, which
  computes every CRC from the RAM copy and writes blocks 52 to 55). Requests 4 and 1 are always posted. The RAM copy of the
  best times can hold a Hard record that the name screen has not saved yet, and request 11 computes the best times
  CRC from the RAM copy, and the hiscore CRC comes from the RAM table, which a loaded savestate or an erase on the
  Hi Scores screen can leave different from the cartridge's. A write or a reload waits while `difficultySwapped`
  is set: `PracticeProcessInput` holds the difficulty of the file in play at Easy for the length of `ProcessInput`.
  The game thread posts requests too; with interrupts off it cannot post between the check
  and the posting. The controller thread does one request a frame, in order. The practice settings (blocks 56 to 63)
  are not touched.
- **Reloading.** A on "Reload from the cartridge" asks, as Write does. The service waits while the game has an EEPROM
  request, then reads blocks 31 to 51, 7 to 30 and 52 to 55 with `osEepromLongRead` into buffers of its own, as
  `SettingsService` reads its block, and checks the six file CRCs and the best times CRC with the game's
  `UpdateCRCData`, as the game does at boot. If a read fails or a CRC is wrong, the status is 4 and nothing else
  changes: the game's copy, `eepromCRCS` and the edits stay. Otherwise, with interrupts off and the game still
  without a request, the game's copy and the six file CRCs and the best times CRC take the data, and the editor's
  copy follows it (the edits are gone). If the game posted a request during the read, the reload starts over.
- **Limits.** The game keeps one all-garibs bit for each level, not a count. Best times are one table for all
  files, in whole seconds (0 to 5999 here). A time edited from a blank name gets the file's name, or "AAA".
  Hold Z for steps of 10 (R: 60 seconds on times). The volumes, the score and the visited bits are not editable.
- **Cost.** With the editor closed: one byte test in the controller thread (`saveEdit.request`).
- **Test.** `tests/saveedit.py`, `tests/saveeditload.py` and `tests/saveeditbad.py` ([testing.md](testing.md)).

## The Controller Pak

`practice/src/cpak.c`, `practice/include/cpak.h`. The game never uses the Controller Pak, and its libultra has no
Pfs code. The practice code has its own small driver. It was written from the commonly known layout of the pak's
serial commands and file system (no document was read: the proxy blocked n64brew.dev), and checked against the
pak that mupen64plus formats (`tests/pakfile.py` is a
second, independent reading of the layout). The driver has not run on a console.

- **One note.** The pak holds a note "GLOVER PRACTICE" (game code "GPRC", company code "GP"). Its data starts with a
  header (`cpak.h` has every field) and holds the whole `Settings` struct (84 bytes, with its own
  version and magic, which a load checks), the Hi Scores screen choice, `run.trainers`, the 30 practice best times,
  and, if it fits, the input recording as runs of 5 bytes (buttons, stick x, stick y, number of game frames). A
  sum over the data guards it. A load refuses a note with a wrong sum or an unknown version ("damaged", nothing
  changes). A load takes the settings only if their magic and version are the current ones. The recording is the
  input only: playing it needs the savestate that it was recorded from, which is not on the pak. A load leaves a
  recording that is being recorded or played alone.
- **The serial interface.** `Command` builds a block of the PIF's RAM (for each controller before ours a 0, then
  0xFF, the byte counts, the command, room for the answer, 0xFE; byte 63 is 1), takes libultra's serial lock
  (`__osSiGetAccess`), sends it with `__osSiRawStartDma`, waits on `controllerMsgQ` for the interrupt, reads the PIF
  back and waits again, as libultra's own EEPROM calls do. The answer area starts as 0xFF, which fails the data
  check of a read. `__osContLastCmd` is set to a write command before the first transfer. The next controller
  read of the game then sends its own command again (the same rule as for the EEPROM). The PIF marks an absent
  controller in the receive count.
  - Status (0x00): a controller (type 0x0500) with bit 0 of the status byte is a controller with a pak.
  - Read (0x02) and write (0x03) of 32 bytes at an address of the pak. The address word is the address with
    its low 5 bits replaced by a 5-bit check (the upper 11 bits, times x^5, modulo x^5 + x^2 + 1; address 0x8000 gives
    0x801F). The data check is CRC-8 with the polynomial 0x85 and start value 0. A read must match; a write returns
    the check of what the pak received, which must match. An empty slot answers with the check byte inverted, for
    a write as for a read: the driver reports "no pak", not a failed transfer. Each block is tried three times.
  - Before it uses the pak, a write of 32 zero bytes to 0x8000 selects bank 0, as libultra does. Its result is
    ignored.
  - Then the driver identifies the device. A Rumble Pak or a Transfer Pak can read as zeros with a valid check, and
    ignores writes. The driver reads block 0 of page 0, writes its inverse, reads it back and writes the old
    block again. A device that does not give the inverse back is refused ("not a Controller Pak"). The block lies
    in an unused part of page 0; if the restoring write fails, the pak keeps the test pattern there.
- **The layout.** 128 pages of 256 bytes. Page 0: four copies of a 32-byte ID block (at 0x20, 0x60, 0x80, 0xC0).
  Its last four bytes hold the sum of the first 14 words and 0xFFF2 minus that sum; byte 0x1A is the number of
  banks (1). Pages 1 and 2: the index table and its backup, 128 big-endian entries (a free page is 3, the last
  page of a note is 1, otherwise the next page); byte 1 is the sum of the low bytes of entries 5 to 127. Pages 3
  and 4: the note table, 16 entries of 32 bytes (game code, company code, first page, status, name in the
  pak's character set). An entry is free when its game code or company code is 0. Pages 5 to 127 hold data.
  The driver trusts a pak when one of the four ID blocks is valid and has one bank, and one of the two index
  tables has the right sum. The other copies may be damaged. A check reports that ("Repair the pak" is then
  offered); a save writes both index tables in any case. A repair (after a question) writes the valid ID block to all
  four places and the valid index table to both. A pak with no valid ID block, or with no valid index table, is
  "not formatted". Only that pak is offered a format, and only after a question. A format writes the note table,
  then both index tables, then the ID area. A format that stops half way leaves a pak that is still "not
  formatted". The driver never writes to a pak that failed the identification.
- **Other notes.** A save or a delete writes only the practice note's own pages, the two index tables and the
  practice note's table entry. The tests compare the other notes' entries, pages and index entries byte for byte
  (`tests/cpakforeign.py`). The pak is consistent after every write. With enough free pages a save writes, in
  this order: the data into free pages, both index tables with the new chain linked, the note table entry (one
  block) with the new first page, both index tables with the old pages freed, and then it reads the ID block,
  both index tables and the entry back and compares them. A failure before the entry leaves the old note; a
  failure after it leaves the new note. At worst some pages stay marked as used with no note, which the driver does not
  reclaim. With too few free pages (but enough with the note's own pages) a save writes over the note's pages:
  it clears the entry, writes the index tables with the old pages freed, then writes the data, the links and the
  entry. A failure then ends the save with "the practice note may be damaged" and usually leaves no note. A save
  to a new entry follows the first order. A delete clears the entry first, then frees the pages. Both index tables
  are written one after the other: a torn first one has a wrong sum, and the driver then reads the backup. A chain
  that leaves the data pages or loops is not trusted (the note counts as absent, and a save takes new pages). A
  second practice note, or a chain that shares a page with another note's chain, makes save and delete refuse
  ("the notes overlap or repeat"). A save that does not fit says "no room" and writes nothing; only the recording
  is left out when it alone does not fit ("saved, the recording does not fit").
- **Time.** An operation is a list of steps run by `CPakService`, which the controller thread calls after its
  read, as it calls `SettingsService`. It waits while the game has an EEPROM request waiting. Each call moves
  blocks for at most 25 ms while the menu pauses the game, and for at most 8 ms while the game runs (the thread
  mostly waits for the serial interface; a game frame lasts 50 ms). It goes on in the next game frame. The check
  comes before each block, so at least one block moves per call. In the emulator a block takes under a millisecond. On a console a block should take
  a millisecond or two (not measured). A note of one page (about 100 blocks with the checks) then takes a few
  game frames, and a recording that fills the pak (123 pages, 984 blocks of data) a few seconds. While the
  thread moves blocks, the controller read of that game frame is late by up to 25 ms (8 ms when the game runs; both
  limits are untested on a console). The game thread is not starved, since the thread waits for the serial
  interface. The delay can show as lag when the game runs (the menu pauses it by default). Operations take more
  game frames when the game runs, and the game's next controller read has to send its command
  again (the block in the PIF is not its own). The view says so. With no request, a game frame costs one test of
  two bytes. Nothing happens at boot.
- **The view.** Settings, last item. Rows: the controller with the pak (1 or 2), Check, Save, Load, Save the
  recording too, Delete the practice note, Repair the pak, Format. Opening it asks for a check. Save, Delete,
  Repair and Format ask first (A confirms, any other button declines). Format asks only after a check found a pak
  with no valid ID block or no valid index table; Repair only after a check found a damaged copy. A row that cannot work is dimmed. The status line says what happened: no pak,
  not a Controller Pak, not formatted, no room, saved, loaded, no note, damaged, overlapping notes, a failed
  transfer. B and the menu button (all four C buttons) do nothing while an operation runs. A load applies its
  settings while the menu is open. A load clears the ghosts when it replaces the recording. A note is checked before
  it is applied: the runs of the recording must lie inside the note, have 1 to 255 frames each and add up to the
  frame count in the header.
- **Emulator notes.** mupen64plus emulates the pak when the input plugin reports one (`tests/pulseinput.c`, from
  the `PULSEINPUT_PAKS` variable that `harness.py` sets from a test's `PAKS`), keeps it in a .mpk file of four
  paks of 32 KB, and formats it when the file is new. It writes only the pak of controller 1 back to the file; the
  tests read controller 2's pak through the driver.
- **Not built.** A load at boot (docs/decisions.md). The recording needs its savestate. The driver is not tested on
  a console: the PIF may need more time between the write and the read on hardware than the emulator does.

## Memory corruption

The Inspect menu's last item, "Memory corruption", and two keys of the memory viewer (`practice/src/corrupt.c`,
`practice/src/memview.c`, `practice/include/corrupt.h`).

- A poke is an address, a value, a size (1, 2 or 4 bytes), a mode, a bit count and a period. Eight pokes live in
  `corrupt.pokes`. Z in the memory view marks the word at its cursor as a poke in the mode "once"; Start opens the
  second page of the view, which lists them (the Memory corruption menu's "Pokes" opens that page directly).
- The game thread does the work, once a game frame (`frameCount2` changed; `ActionsGameFrame` runs for every message
  of the main queue and calls `CorruptGameFrame` only while a poke runs, a request waits or the marker is on).
  Freeze writes the value, random sets the low `bits` bits to random bits, flip flips one random bit among the low
  `bits` bits. Random and flip act every 1, 2, 5, 10, 30 or 60 game frames. The menu thread edits the list; the
  "Apply once pokes" and "Flip one random bit" menu items set a request bit that the game thread takes with
  interrupts off, and close the menu. A on the pokes page writes the poke at once, from the menu thread.
- "Flip one random bit" picks a bit uniformly in the region: the glove's actor (`hand`, 0xE4 bytes), the ball's actor,
  a random actor from `actorList` (at most 400 are walked, each pointer checked with `SafePointer`), or 16, 256,
  4,096 or 65,536 bytes that start at the memory view's address rounded down to that size. In an actor, bytes 0x10 to
  0x1B are left out: `distNext`, `distPrev` and `distSq`, which `CreateObjects` (the draw thread) writes again for
  every actor each video frame, also while the game is stopped. A test run once flipped one of those bits and found
  no change.
- Writes are allowed from 0x80000000 to 0x807FFFFF, except any byte from 0x80400000 to `practice_BSS_END` (the
  linker's symbol: the practice code, its data and its bss). The savestates (from 0x80480000) can be written. A word
  goes to an address that is a multiple of 4, a half word of 2. After each write the data cache is written back and
  the instruction cache is cleared for those bytes. A poke whose mode is off is
  never written.
- The random generator is a xorshift32 of its own, seeded from the CPU counter at first use. The game's `rseed` is
  not touched.
- The HUD marker is "MODIFIED pokes" (`CorruptMarker`) while a poke runs (freeze, random or flip), and after any write
  (`Corrupt.dirty`) until a state is loaded (the load puts back what the writes changed in the state's ranges), the
  menu's "Clear all pokes" is used, or the console is reset. A level change or restart does not clear it, because a
  write to a global or to the save outlives the level.
- Limits: the game keeps its own copies of some values (for example the glove's position is also in other places), so
  a write to one may be overwritten at once. A freeze runs at the start of each game frame, before the game's logic.
  The writes are not part of a savestate or a recording. The bindings are full. There is no binding.

## The early title menu

The Warp menu's "Early title menu" (9th item) and "Back to the normal title" (10th), `practice/src/earlytitle.c`. The
facts about `TitleScreen` are in [game.md](game.md#the-early-title-menu).

- `TitleScreen` is still at its retail address: the table in "Hooks" keeps it, and the practice segment uses other
  space. The hook `PracticeDoMenu` replaces the call of `DoMenu` in `RunGame` (one call instruction, as every hook)
  and calls `TitleScreen` while `earlyTitle.on` is 1. With the feature off it costs three byte reads in each game frame.
- Open (a request taken at the start of a game frame): in a level, `ExitLevel(QUIT_EXIT)` as the pause menu's Quit,
  which fades out, frees the level and starts mode 1 with `levelSelectCount` 0; `earlyTitle.pending` makes
  `PracticeDoMenu` switch on. `ExitLevel` for a Quit also calls `UpdateHiscoreTable(player.score)`, which may write the
  save. The menu hint says "Edits your save". A request while an exit runs (`gameInfo.exitFlags & 1`, or `pending`
  set) is refused with a HUD message. `pending` is cleared at the next game frame
  if the exit stopped without reaching mode 1 (a loaded state, for example).
- `LeaveTitle` frees what is loaded, for the early title and for a warp or reset from any title. In mode 1 with
  `levelSelectCount` 0 nothing is loaded (the exit that led to the title freed the level, `DoMenu` and `TitleScreen`
  have not set up yet, or `KillTitle` or `LeaveTitle` ran): it frees nothing. In mode 1 with `levelSelectCount`
  above 0 and the early title off, it calls `KillTitle` (the normal title has no level). Everywhere else (the title
  flythrough, the early title, its level select) it clears the texts, the backdrops and the level.
- The early title plays the song that was playing before. `TitleScreen` does not choose music.
- No state can be saved or loaded on any title, the early title or the level select (a HUD message says so), and
  no recording can start there. A state holds the game's objects, not the overlays and lists that `DoMenu` and
  `TitleScreen` set up.
- It ends when `TitleScreen` leaves mode 1 (Continue and New game start a level, the third entry sets mode 2), or when
  anything else changes the mode (a warp). The Warp menu's "Go" from the early title frees its level (the early title
  loaded level 7).
- Back: frees the level, then sets the state that follows the title flythrough (mode 1, level `FLYTHRU`,
  `currentScreen` 1, `booting` 0, `disablePause` 0). `DoMenu` shows the normal title. It works from the early title
  and from its level select.
- The practice menu opens in the early title as everywhere. The ROM's other tools still work there; the early
  title has no level to act on.

## The trick trainers

The Game menu's "Trainers" sub menu (`Run.trainers`, one bit for each trainer: 1 L mash, 2 line keeper, 4 MBZ-L, 8
Moon Pound aim, 16 boss windows, 64 wall clip; `practice/src/trainers.c`, `practice/src/moonaim.c`,
`practice/src/bosswin.c`, `practice/include/trainers.h`, `practice/include/moonaim.h`, `practice/include/bosswin.h`).
Bit 32 is not used. An older Controller Pak note can have it set, and loading the note ignores it. All six trainers are
built. They read the game and draw. They write no game state, call no `Random` and change no input.

- **Hook and logic.** `TrainersGameFrame` runs from `PracticeClearText`, after `ActionsGameFrame`, on every message
  that `RunGame` gets. It takes a snapshot of the state that the last game frame left (the glove's mode, the ball's
  moves, speed, friction and physics, the camera's yaw) and sets `fresh`. `TrainersProcessInput` runs the logic from
  `PracticeProcessInput`, once a game frame, with the pad of that frame (`pad[ActiveController]`) and the snapshot.
  The first `ClearText` run of a game frame often sees the pad of the frame before (docs/testing.md, "Trainers"),
  and the logic runs in `PracticeProcessInput`. With `run.trainers` at 0 both calls return after one test.
- **Counting frames.** Each logic run is one game frame. "Frame n" of an attempt is the n-th run after its start.
  `PracticeProcessInput` runs before `MoveHand`: a press that the logic sees at frame n is acted on by the game in
  frame n. The glove's mode in the snapshot is the mode that `MaintainAction` sees in that frame.
- **Quiet.** `Quiet` is true only in a level (`ActionsInLevel`) with no pause, `controlMethod` 1 (the player moves
  the glove), no `noControlTimer`, no free camera, no noclip and no moon jump, with the glove's and the ball's
  pointers readable. Otherwise the state is cleared and the view is empty. A savestate load, a level start and
  turning a trainer on or off clear the state too.
- **The view.** The game thread sets `trainerViewBegin` to `seq + 1`, fills `trainerViews[(seq + 1) & 1]` and raises
  `trainerViewSeq`. The draw thread (`TrainersDraw`, from `PracticePrintTimers`) reads `trainerViewSeq`, copies
  `trainerViews[seq & 1]` and keeps the copy only if `trainerViewBegin - seq` is at most 1 (a second publish would
  have written the buffer that it copied; two publishes in one game frame happen at a reset). It tries four times
  and draws nothing if it fails. `TrainerView.frame2` is the `frameCount2` of the frame that made it. The draw
  thread reads only the view.
- **L mash.** A press of L in JOINED is a stand press, in BALLWALK a hold press. A press ends the gap since the
  press before it, and the label names the part that the gap measured (a stand press ends the hold part: on time at
  2, late by gap - 2; a hold press ends the stand part: on time at 2, slow by gap - 2, a rest from 9). The target
  is `MashTarget`: a scalar port of `tools/tricks/lmash.py` that runs 140 game frames of a perfect mash and takes the
  mean move of frames 100 to 139. It is made again when the pad's length (in quarter units), the ball's physics or
  the air or ground state changes. The ball counts as in the air after 3 game frames off the ground. A move of
  more than 30 units in a game frame (a teleport) is not counted in the move, the peak or the mean.
- **MBZ-L.** B pressed in JOINED or BOUNCE, or A pressed in JOINED, starts an attempt (frame 0). The aim (THROWAIM
  or PREWHACK) is expected on frame 6 (B) or 4 (A), Z cancels it on that frame, and the L belongs on the next. The
  logic follows the real modes: the aim frame is the first frame in the aim's mode, the cancel is the first frame
  from there with Z down, and the L target is the cancel plus 1. The best speed is `(fb + 2/3 * fh) * V + a` with
  V the ball's speed at the aim and a the pad's push. Speeds are read after the L frame, 3 and 6 frames later.
  An attempt that never reaches the aim shows nothing, except a fresh Z that drops the ball and an L from JOINED
  before the aim that stands Glover on the ball. Z counts as armed when it is down on frame 0; a press after a release
  is fresh. In the aim, the end of the aim is judged before Z (a release shows "RELEASED AT n" even with Z down).
- **Line keeper.** `LineStep` is a port of `tools/tricks/dpad_trainer.py`. The snapshot holds the camera's position,
  velocity, target and modes (`cameraInfo`). The trainer is silent (and drops its line) unless the camera is normal
  (`mode` 1, no override, destination, orbit, angle lock, block or fall), the mover (the ball when Glover holds it or
  stands on it, else the glove, which must be on the ground) moved more than 3 and not more than 30 units in the last
  game frame (a larger move is a teleport: a respawn, a checkpoint, a noclip exit; the reason is JUMPED), and neither
  of the stick's axes is 0 (a diagonal). The line is latched on the 4th frame with the same stick: `ref` is the camera's yaw. Each frame
  `E = wrap(yaw - ref) + pending`, with `pending` the turn that the camera's sideways speed still adds (it keeps
  0.26667 of its speed each frame; the total turn is `speed / 0.73333 / D`). `D` is the distance of the camera from its
  target, read from the game. `tap = 21.82 / D` is what one frame of C turns the view. The C button of the frame
  before is in `camera.vel` now. Its `tap` is taken out of `E - prevE`, and `Edot` is the mean of the last 4 such
  values. `E + 2 Edot` below -0.10 shows C-RIGHT, above 0.10 C-LEFT. "Off the line" sums each frame's move times the
  sine of the yaw's error of the frame before (a fall of the yaw is a drift to the right). The strip and the C share
  keep running when the trainer is silent.
- **Moon Pound aim.** `MoonAimStep` runs while the glove is in JUMP, off the ground, with the glove's radius rule in
  force (not in water, with Hercules or in the last boss). It works on a copy of the faces near the glove.
  - The budget: one step has `STEP_COUNTS` (187,500 ticks of the CPU counter, 4 ms). The slams and the search get the
    first 3 ms (`STRIP_COUNTS`) and stop between two pieces of work when the next would not fit (the longest slam so
    far is the estimate). A cell that has no time is grey and `MOWF_SLOW` is set. The aim's cell is always played. The
    build of the buffer gets what is left, at least 1 ms, and goes on in the next game frame. Without a buffer it gets
    the whole budget. The counter also runs while other threads do. A busy level can show more (levels 12 and 15
    show 30 ms spikes that are not the aim's).
  - The buffer: a build copies the faces of the landscape actors (`collisionList`) and of the platforms with
    `flags & 0x84 == 0` that lie within the reach of the glove (up to 192 faces, 120 units, less when the buffer
    fills) into a staging buffer, then finds the hole candidates, then swaps it with the active one. The active
    buffer serves the slams until then (`MOWF_BUILD`). A build is about 20 ms of work, spread over several game
    frames. It starts at the first frame of each jump, when the glove is 24 units from the centre of the active buffer
    sideways or in height, every 100 game frames, and after a level start or a savestate load; two builds start at
    least 4 game frames apart. The job keeps the position of the actor list between frames and starts again if the
    actor there changed. A face's corners are read one axis at a time (x, then z, then y) and a face that lies outside
    the box on an axis is dropped before the other axes are read. A full buffer keeps the nearest 192 faces.
  - Memory: a buffer holds each face's box once, in `bb` (a face does not carry it as well). The distance keys of the
    build are one array, as only the staging buffer uses them. The strip keeps a 56-byte summary of each slam
    (`CellSum`) and plays every slam into one scratch `Sim`. The aim's bss is 12 KB smaller than before (practice
    RAM left: 15.1 KB to 25.9 KB in the test build, 26.8 KB in the release build).
  - What a verdict needs: `cover` is the distance from the build centre within which every face is in the buffer
    (the reach, less when the buffer is full). A slam whose path, with the glove's radius, reaches beyond `cover`, or
    above 80 or below 330 units from the build centre's height, gets no verdict: its cell is grey and the zone search
    is skipped. A stale buffer shows grey cells, not a false CLIP.
  - The candidates: for each corner of an up-facing face within 72 units of the build centre it collects the faces
    at that vertex (0.05), clips a box by the half plane of every real edge (`cone.py`'s dead cone), keeps the part
    over an up-facing face, and keeps the vertex when some direction in it is caught by no face. The hole's centre
    for a centre one unit above the vertex is the centroid of that part. The list holds 64 (`MOWF_CANDS` when it
    stops there). The aim uses the candidates within 40 units of the column, or fewer when the column is far from the
    build centre (72 minus its distance; `MOWF_CANDS` too).
  - The slam: `Simulate` is `moonpound_trainer.simulate` in floats. `MakePath` gives the glove's centre in 14 passes
    (jump passes keep `(1 - (1 - hand friction) / 4) * 0.7` of the horizontal speed, slam passes half of that, zero
    below 0.1; hover 2 passes; fall 14 a pass) and the radius (it grows by a quarter of the way to
    `16 * levelHandScale / 0.05`). Each pass is tested against the faces that its box touches with
    `CollideActorWithPlane` as a pure function (the 20-step interpolation, the inside test with 0.001, `NearEdges`).
    A contact is LAND. A pass whose centre crossed an up-facing face with no contact is CLIP. A glove that falls too
    fast for the Z gate (`vel.y - 2g <= -9`) is NO_Z. Passes above all faces and below them are skipped.
  - The strip: cell k is the slam with Z read k + 1 game frames after the frame of the hook (the pad of the hook's own
    frame is already read). The state is moved one jump pass for each cell. A cell is grey when A is held and the
    action time is under 5, and when the action lock is more than k + 2. The aim follows the first cell that is not
    grey.
  - Reuse: with no stick input and no contact, the state of the next game frame is what `AdvanceJump` gives, and the
    slam of cell k + 1 now is the slam of cell k then. `MoonAimStep` keeps the eight slams and the state that the next
    frame should start from. When the glove is where it should be (1e-3 in place, 1e-4 in speed), the buffer is the
    same and the physics are the same, the slams shift by one and only the last cell is played. A cell that was cut
    for time is played in a later frame. A move of the stick (or anything else) plays all eight again.
  - The aim: the fall column is the glove's place in pass 5. The nearest candidate within 40 units is chosen with
    `d`, the lowest height of the centre above the vertex among the falling passes that are above it, and the target is
    `vertex + d * centre`. When the column is within 1.5 units and `d` is 2 or less (the window of the 20-step
    interpolation), up to 16 more slams are played along the line from the column to the target, 0.3 units beyond it,
    and the distance to show is where the first one clips. The stick is the shift in the camera's frame divided by
    `2.85 / 36` units per stick unit, plus the dead zone of 5.
  - Cost: a slam is 8,000 to 60,000 ticks (0.2 to 1.3 ms) in the emulator, mostly the 20-step interpolation of the
    contact test, which runs in about 2,400 cycles for a centre behind a face. `MoonView` keeps the numbers of the
    frame (`sims`, `calls`, `iters`, `counts`). Measured by `tests/trainers_moon_cost.py` over the hub's dome (88
    faces, 20 candidates) with the stick moved on every frame (no reuse): at most 206,000 ticks (4.4 ms) in one game
    frame, 90,000 (1.9 ms) on average. The README and the menu say that the aim can add lag.
- **Draw.** (a) L mash at the left (x 8, y 116), (b) the line keeper at the top left (x 8, y 48, under the game's
  garib counter), (c) MBZ-L at the right (x 156, y 92) and (d) the Moon Pound aim in the lower middle (x 80, y 164). Text that would run past x 320
  is cut. The MBZ panel holds 27 characters; its list rows use short verdicts. Colours: green on time, yellow
  slightly off, red lost, grey nothing to say.
- **Boss windows** (`practice/src/bosswin.c`). It has its own conditions (the trick trainers need `Quiet()`, which fails in
  the Space fight where Glover is fixed): the switch, level 13, 28 or 40 with the matching type in `enemies[0]`, no
  pause, no exit, no noclip and no cameo. `TrainersGameFrame` calls `BossRead` at the hook, which copies what the fight
  shows into a snapshot (`SafePointer` and `ReadFloat` guard each read). `TrainersProcessInput` calls `BossStep` once a
  game frame, with the snapshot of the frame before kept for comparison. `bossFresh` and `bossDirty` in `trainers.c`
  are the boss trainer's own flags. `BossStep` works from the numbers in docs/game.md ("The bosses"): the hit pass of
  each part, its window (49, 34 or 20 passes), the touches (the flag `0x04000000`, or `path->moving` on a Space
  shoulder's platform) and the reason a touch was lost, from the reading before the pass of the touch. Events go in a
  ring of 8. The result is `BossView` (0x74 bytes, at `TrainerView.boss`, 0x198), and `BossDraw` draws it.
  Draw: the panel at x 164, y 44 (right of the screen, under the game's counter), the history in two rows at x 8, y 3.
  A line is cut at x 320. It reads the game and writes nothing. With the switch off it costs one test per game frame.
  The draw code is shared by helpers (`WindowText` makes the text of a window for the big line and for a Space row).
  The practice RAM area had 4.6 KB left after it. `make` prints what is left now. `bosswin.o` has 8.6 KB of code.
- **Wall clip** (bit 0x40, `TRAINER_CLIP`; `ClipStep`, `ClipView` at `TrainerView.clip`, 0x20C). `ReadClip` takes the
  ball's position and horizontal velocity and the glove's position at the hook. With the glove in JOINED or PREWHACK and
  `ballControl.type` 3 (bearing), `MoonWallScan` (moonaim.c) walks the landscape actors and static platforms near the
  ball: it reads each face's corners (`LoadFace`), keeps faces with `|normal.y| < 0.3` that face the ball's direction
  (the dot product of the horizontal normal and direction at most -0.2), takes the plane distance `n . p + d` (0 to 60),
  finds where the ball's line meets the plane and keeps the nearest face whose triangle holds that point (tolerance
  0.02). It has no buffer, so it is never stale. The window is `[6, a * S + 2.99]` with `S = -n . v` (speed along the
  normal), `a` 2.95 in the air and 2.6 with `hand.obj->planes[0]` set; below the least speed (3.2, 5.5) the verdict is
  SLOW. The numbers come from `tools/tricks/bearing.py` (the model; the fit is exact to 0.05 for speeds 3.2 to 14). A
  cancel is a frame with mode PREWHACK and Z down in the pad. The state of that frame is the hook's, so its distance is
  the ball's distance after the frame before. The trainer then watches 8 frames for the glove's centre behind the plane
  (more than 0.5) and the first R press. Cost: one scan a frame while the glove holds the bearing; at most 134,720
  counts (2.9 ms) measured in Atlantis 1, where one actor holds the floor and is read every frame.
- **The trainer log and the result hold** (`trainers.c`, `trainers.h`). An attempt ends in the trainer's own step, which
  calls `TrainerResult(bit, color, fmt, ...)`. It pushes a line (a colour, the trainer's bit, 31 characters: a 2 letter
  tag, then the text) on the front of `trainerLog` (10 lines, 0x178 bytes) and sets the trainer's bit in `ended`. The
  log is written with `begin` and `seq` as the views are; `DrawLog` (draw thread) copies it and keeps the copy only if
  `begin` equals `seq` after the copy. What ends an attempt:
  - L mash: 12 game frames after the last press (`LM_RUN_GAP`), when the run had 3 or more presses (`LMashEndRun`). The line
    counts the presses and the gaps of 2 frames (`lm.runPresses`, `lm.runOn`; GOOD is 90 percent or more, OK 70 percent).
  - MBZ-L: `MbzPush`, when the verdict is final and the speeds are in (7 game frames after the L), or when the next
    attempt starts during that collecting.
  - Line keeper: `LineWatch`, the frame on which `lk.live` ends after 20 game frames or more, unless the reason is JUMPED.
  - Moon Pound aim: the first game frame in HAND_SLAM after a live view. The Z was read in the frame before, which is cell 0
    of the view of two frames earlier (`moonOlder`). The line is that cell's prediction (LAND with the distance to the
    hole, CLIP, NO Z, NOT CHECKED). `tests/trainers_moon.py` follows the aim and checks that the line says CLIP, and says
    LAND when it does not follow.
  - Boss windows: each event pushed into the ring (`bosswin.c`, `Push`), with the text of `LastText`.
  - Wall clip: 8 game frames after the cancel (`CLIP_WATCH`), with the R and the outcome known.

  The hold: `ApplyHold`, at the end of `BuildView`, copies the part of the view of each trainer in `ended` into `held`
  (the Moon Pound aim's part is `held.moon`, set by its own step) and sets `holdUntil[bit]` to `frameCount2` plus the
  hold (20, 40 or 60 game frames). While `holdUntil` is ahead, the view that is published takes that trainer's part from
  `held`. The trainer's logic goes on unseen. A later `ended` writes `held` again and moves `holdUntil`, so a hold never
  queues. The MBZ-L part has its newest attempt's `doneFrame` set to the view's frame, so the full result stays. A level
  start, a savestate load, going quiet (a pause) and turning the trainer on or off end the holds. The log stays through
  a level start and a savestate load (they are how a trick is tried again). It clears when `gameInfo.level` changes, and
  `LogDrop` takes a trainer's lines out when it is turned on or off.

  The settings are `trainerLog` (0x4E) and `trainerHold` (0x4F) in `Settings`, stored from 1 (off, 5, 6, 8, 10; off, 1 s,
  2 s, 3 s) with the defaults 5 lines and 1 s. They take two of the three unused bytes at 0x4E, so `sizeof(Settings)` and
  the Controller Pak note keep their size. In the EEPROM block they share the last byte of `hudPlace[12]` (0x1F: the log in
  the low nibble, the hold in the high nibble), which an older ROM wrote as zero; `SettingsFixMenuLook` gives the defaults for a
  zero or a value out of range, so older blocks and notes load. `tests/settings.py`, `settingsload.py`, `settingsold.py` and the
  Controller Pak tests check them. Place of the feed: x 158, y 48, 9 pixels a line (x 8, y 28 in a boss fight, which has
  the right side; 5 lines at most while the MBZ-L panel, at y 92, is on). With 5 lines it ends at y 93.
- **Tests.** `tests/trainers.py`, `tests/trainers2.py`, `tests/trainers3.py` (the wall clip),
  `tests/trainers_moon.py`, `tests/trainers_moon_cost.py`, `tests/bosstrainer.py` and `tests/trainerlog.py` (the log and
  the hold) ([testing.md](testing.md)).

## Camera speed

`Run.cameraSpeed` (0 normal, 1 fast, 2 faster; Game > Movement and camera; not saved), `practice/src/camspeed.c`,
`practice/include/camspeed.h`. The facts about the camera are in [game.md](game.md#how-the-camera-follows).

- **Hook.** `hooks/main.c.patch` replaces the call `MoveCam()` in `MainGameLoop` with `PRACTICE_WRAP(MoveCam)()`.
  `PracticeMoveCam` calls the game's `MoveCam` first. The other calls to `MoveCam` (the 20 and 30 frames of a restart gate,
  platform.c) are not hooked. With the setting at 0 it returns after that call and one test.
- **What it does** (gain g = 1, 2, 3 for normal, fast, faster), after `MoveCam`:
  1. `followTimer = 0` and `followSpeed = 20 g` on every frame. This ends the slow follow that every ball grab starts (the
     mash's cause) and raises the top.
  2. `camera.phys->maxSpeed = 20 g`, so the next `UpdateActor` does not cut the camera's speed.
  3. The body's pull: with `excess` the camera's distance from its target beyond the wanted one, the game added
     `min(excess / 2, 10)`. The hook adds `min(g * excess / 2, 10 g)` minus that, toward the target, and holds the
     speed at `20 g`.
  It does nothing while the camera is not in its normal mode, while an override or a destination runs.
- **Measured** (`tests/camspeed.py`, Atlantis 1, the ball runs straight away from the camera for 110 game frames, all
  actors moved back each frame; lag is the camera's distance from the ball minus its distance at rest, in units, mean over
  frames 60 to 109):

  | Drive | normal | fast | faster |
  | --- | --- | --- | --- |
  | Full mash, 21.9 units a frame | 835 and growing 11 a frame (1,130 at frame 109) | 18.8 | 13.5 |
  | Ball at 14 units a frame (set) | 41.9 (51 at most) | -0.9 | -4.4 (the camera is a little nearer than at rest) |

  The target point is 822 units behind the ball in the normal mash and 1.6 behind with the setting on.
- **Best time and HUD.** `BestTimesTick` adds `BT_OTHER` while the setting is on. `HudDrawStatus` draws the icon
  (`DrawCameraIcon`: 10 by 9 pixels from rectangles, at x 281, y 11, above the last digit of the game's score, which
  is at about x 278 to 293 from y 24; the game's hearts are about 15 by 14). `PracticePrintTimers` draws the HUD text layer
  when the setting is on.
- **Side effects.** The game's own slow follows are ended too: after a respawn at a restart gate (platform.c sets 5),
  at the start of a bonus level (bonus.c sets 0). The camera snaps to its target there. The sideways turns (toward a moving glove, the C taps)
  are not changed, but the higher speed limit applies to them.
- **Size.** The camera hook and icon take 1.0 KB of the practice RAM area. The trainer log and the result hold took 5.3 KB
  (the views in `held` and the moon views, 1.1 KB of it bss); 3.7 KB are left.

## Game demos

The Warp menu's last three items (Demo, Watch demo, Watch all demos), `practice/src/demo.c`. The facts about the
recordings are in [game.md](game.md#demo-recordings).

- A request (`demo.request`) is taken at the start of a game frame, like every action. On a title screen
  (`ActionsOnTitle`) the code does what `DemoKeyRun` does: `LeaveTitle`, then `DemoSetup` (slot 6, Medium,
  `ActivateCheat(22, 0)`, `cheatCount` 1, `StartNewGame`, `autoPlayback` 2) and `StartNewLevel`. In a level it calls
  `ActionsRestart`, the Time Trial's restart exit, and sets `demo.start`. `ActionsGameFrame` calls `DemoSetup` when
  mode 8 has taken the target level, and the game's own `EndOfSubLevelScreen` starts the level. The recording is
  loaded by `StartNewLevel` because `autoPlayback` is 2.
- `PracticeReadControllerFromData` runs the same input layer as in a level (menu, bindings), then `DemoRead`, which calls
  `ReadControllerFromData`. If `autoPlayback` is no longer 2 afterwards, the demo has ended (its end, or A, B or Start):
  `demo.advance` is set. The practice menu zeroes the buttons the game sees, so a menu press cannot leave the demo.
- After the exit the game is in mode 1 with `levelSelectCount` 0 (a quit). If `demo.all` is set, the next demo starts there.
  `demo.all` is cleared when A, B or Start are seen on controller 1 (in `Frame`, also between two demos), by a warp or
  reset (`ActionsRestart` calls `DemoAbort`) and by the early title.
- The cutscene (level 39) leaves with `SUB_LEVEL_EXIT` for level 40 and mode 8. `DemoGameFrame` turns that into the
  state of a quit: mode 1, `levelSelectCount` 0. It is the last of the eight, so Play all ends there.
- `DemoAbort` frees `recordKeys` and resets the playback variables when a level change leaves a running demo
  (a warp or reset in a demo, the early title). The same applies to the title's own attract demos.
- A demo taints the practice best time (`BestTimesTick`, bit `BT_OTHER`). No other practice system is changed.
- With no demo asked for, a game frame costs one byte test (`DemoBusy`) in `ActionsGameFrame`, one in `Frame` for
  `demo.all`. The code and data take 0x590 bytes of the practice segment.

## Warp with lives, garibs and a power-up

`Run.warpLives`, `warpGaribs` and `warpPower` (Warp menu, the last three items). ACTION_WARP calls `ActionsRestart`,
which now returns 1 when a start is under way. If any of the three is set, `ActionsGameFrame` waits for a level start
(`practice.levelStarts` differs from its value at the request) and then for the intro to end (`player.levelFrames` is
above 0, or `frameCount` is 150). It then sets `player.lives` (when chosen), `player.garibs`, calls `GivePickup` and
taints the run (`BT_OTHER`). Setting the power-up earlier fails: `tests/warpgive.py` saw a power ball given at game frame 3
back to the rubber ball by game frame 31, and one given at frame 40 gone a frame later. A level with no intro timer
(bonus levels) uses the 150 frames. The three items are the last of the menu because the tests reach "Go", "Reset", the early title and the demos by the number of presses of down.

## Cost

Every hook costs a few CPU cycles on each game frame, even with every feature off. During a load the game's
own work fills the CPU. A load can end a video frame earlier or later than in retail. Measured: a few dozen
more instructions a frame in `PracticePrintTimers` moved the start of Atlantis 1 by a game frame, and a
different change moved it back. Such small timing differences are allowed (CLAUDE.md), but keep the work with
every feature off as small as possible: each hook checks whether it has anything to do and returns at once. The
faithfulness test reports load times ([testing.md](testing.md)).

## Hooks

A hook replaces a call that the game makes with a call to a practice function of the same type
(`PRACTICE_WRAP` in `hooks/include/practice_hooks.h`). The practice function calls the game's function and
does its own work before or after it. The call stays one instruction, and GCC 2.7.2 compiles the rest of the
function as before. No code or data moves. The hooks are patch files in `hooks/`. gloveworld is a separate
project: the build applies the patches to a copy of its source and never changes the submodule. The patched
files are compiled by GCC 2.7.2 with gloveworld's flags (`-mmips-as`, `-mcpu=r4000`, `-O2`). A hook's code
there is C89.

Code that must be in the game's main segment (the boot hook) takes the place of a function that nothing calls.
The candidates, by size:

| Function | Size | Rodata | Note |
| --- | --- | --- | --- |
| `TitleScreen` (game.c) | 0x5BC | strings | an early title menu; nothing points to it. The practice build keeps it and calls it ([below](#the-early-title-menu)) |
| `ScreenShot` (personal.c) | 0x27C | strings, a double | gloveworld's `debugpad2.py` uses its space |
| `ClearStaticMemory` (main.c) | 0x74 | none | used by the boot hook |

A function with rodata cannot simply be removed: the data holds 1,280 raw pointers into the rodata (for example
the language string tables). The rodata must not move.

### Why `main` does not grow

A build in which the hooks make `main` grow works:

- splat writes 353 symbols as fixed addresses (`undefined_syms_auto.txt`, `symbols.ld`), most of them inside
  an object, such as a field of a struct. They break when the code before them grows. Rewritten as "the
  nearest global label plus an offset", they move with the code. A check after the link confirmed that no such
  symbol had code growth between it and its label.
- The 1,280 raw pointers in the data all point into the rodata, which comes before the code and does not move.
- The asset addresses (0xB0xxxxxx) are all symbols. When `main` grows past 0xF6680, the assets move up, and
  their symbols with them.
- The game booted, played Atlantis 1 and finished it, with the same state on every frame as the retail ROM.

It is not used, for three reasons. Two hooks of 8 bytes moved every later function and every variable by
0x10. Tools that read retail addresses (autosplitters, memory watch lists, GameShark codes) would break.
Almost every call and every global access after the first hook changed. The patch grew from 4 KB to
320 KB. The heap also got smaller by the size of the boot code.

## Code choice

The game's files stay on GCC 2.7.2 with gloveworld's flags. The practice code uses a current GCC
(`mips-linux-gnu-gcc`, version 12 on Ubuntu 24.04) with `-std=gnu11 -Os -march=vr4300 -mfix4300 -mabi=32
-mno-abicalls -fno-pic -G0`. The two link cleanly: both use the o32 calling convention, and the linker script
discards the newer sections (`.MIPS.abiflags`, `.pdr`, `.gnu.attributes`).

Reasons:

- C11 (declarations in loops, `_Static_assert`, `static inline`), and warnings that catch mistakes
  (`-Wall -Wextra -Werror`).
- Smaller and faster code than GCC 2.7.2. RAM in the Expansion Pak is plentiful, but the HUD runs every frame.
- `-mfix4300` works around a multiplication bug of the VR4300 CPU.
- The practice code is not matched against anything. Nothing ties it to the old compiler.

The practice code declares the few game types and functions that it uses itself (`practice/include/game.h`),
and builds display list commands with its own functions (`practice/include/rdp.h`), from the public RDP and
RSP documentation. It does not include the SDK's headers.

`-fno-builtin` and `-fno-tree-loop-distribute-patterns` stop GCC from calling `memset` or `memcpy` on its
own. `-mno-check-zero-division` leaves out the divide-by-zero traps.
